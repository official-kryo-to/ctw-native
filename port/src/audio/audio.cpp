// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kryo.to
// See LICENSE in the repository root.
#include "audio.h"
#include "os/datafile.h"
#include <SDL.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>
#define MINIMP3_IMPLEMENTATION
#include <minimp3_ex.h>

// Output is always 44.1 kHz stereo s16; tracks at other rates are resampled (linear) and mono is duplicated.
static const int kOutRate = 44100;
static SDL_AudioDeviceID g_dev = 0;

// MP3s are streamed through the data layer, so they can come from a folder or from inside the exe.
struct Mp3Source {
    DataFile file;
    mp3dec_io_t io{};
    bool open(const std::string& path) {
        if (!file.open(path)) return false;
        io.read = [](void* buf, size_t size, void* user) { return ((DataFile*)user)->read(buf, size); };
        io.read_data = &file;
        io.seek = [](uint64_t position, void* user) { return ((DataFile*)user)->seek(position) ? 0 : -1; };
        io.seek_data = &file;
        return true;
    }
};

struct Stream {
    Mp3Source source;
    mp3dec_ex_t dec{};
    bool open = false, loop = false, paused = false;
    int rate = 0, channels = 0;
    double pos = 0;                 // position in source frames (fractional, for resampling)
    std::vector<mp3d_sample_t> buf; // decoded source samples not yet consumed
    size_t bufFrames = 0, bufStart = 0;
    uint64_t framesPlayed = 0;
};
static Stream* g_music = nullptr;   // owned; swapped under the device lock, never moved/copied
static std::atomic<float> g_volume{0.8f};
static std::atomic<float> g_level{0.f};

// Pull more source frames into s.buf. Returns false at end of stream (after looping if enabled).
static bool refill(Stream& s) {
    const size_t want = 4096 * (size_t)s.channels;
    // keep the frame at bufStart (needed for interpolation), drop the rest
    if (s.bufStart > 0) {
        size_t keep = (s.bufFrames - s.bufStart) * s.channels;
        memmove(s.buf.data(), s.buf.data() + s.bufStart * s.channels, keep * sizeof(mp3d_sample_t));
        s.bufFrames -= s.bufStart;
        s.pos -= (double)s.bufStart;
        s.bufStart = 0;
    }
    s.buf.resize(s.bufFrames * s.channels + want);
    size_t got = mp3dec_ex_read(&s.dec, s.buf.data() + s.bufFrames * s.channels, want);
    if (got == 0 && s.loop) {
        mp3dec_ex_seek(&s.dec, 0);
        got = mp3dec_ex_read(&s.dec, s.buf.data() + s.bufFrames * s.channels, want);
    }
    s.bufFrames += got / s.channels;
    s.buf.resize(s.bufFrames * s.channels);
    return got > 0;
}

struct SfxVoice {
    const unsigned char* data = nullptr;
    unsigned len = 0;
    double pos = 0, step = 1;
    float vol = 1, pan = 0, pitch = 1;
    int rate = 22050, id = 0;
    bool loop = false, active = false;
};
static SfxVoice g_sfx[32];
static int g_sfxNext = 1;
static std::atomic<float> g_sfxVolume{0.8f};

static void mixSfx(int16_t* out, int frames) {
    const float master = g_sfxVolume.load();
    for (SfxVoice& v : g_sfx) {
        if (!v.active) continue;
        const double step = (double)v.rate * v.pitch / kOutRate;
        // equal-power pan
        float a = (v.pan + 1.f) * 0.25f * 3.14159265f;
        float gl = cosf(a) * v.vol * master * 256.f, gr = sinf(a) * v.vol * master * 256.f;
        for (int i = 0; i < frames; ++i) {
            unsigned i0 = (unsigned)v.pos;
            if (i0 >= v.len) {
                if (!v.loop || !v.len) { v.active = false; break; }
                v.pos -= v.len * (double)(unsigned)(v.pos / v.len);
                i0 = (unsigned)v.pos;
            }
            float s = (float)v.data[i0] - 128.f;
            int l = out[i * 2] + (int)(s * gl), r = out[i * 2 + 1] + (int)(s * gr);
            out[i * 2] = (int16_t)std::max(-32768, std::min(32767, l));
            out[i * 2 + 1] = (int16_t)std::max(-32768, std::min(32767, r));
            v.pos += step;
        }
    }
}

int Audio_SfxPlay(const unsigned char* pcm, unsigned len, int rate, float volume, float pan, bool loop) {
    if (!g_dev || !pcm || !len) return 0;
    SDL_LockAudioDevice(g_dev);
    int h = 0;
    for (SfxVoice& v : g_sfx)
        if (!v.active) {
            v = SfxVoice{};
            v.data = pcm; v.len = len; v.rate = rate > 0 ? rate : 22050;
            v.vol = volume; v.pan = pan; v.loop = loop; v.active = true;
            v.id = h = g_sfxNext++;
            if (g_sfxNext <= 0) g_sfxNext = 1;
            break;
        }
    SDL_UnlockAudioDevice(g_dev);
    return h;
}

static SfxVoice* findVoice(int h) {
    for (SfxVoice& v : g_sfx)
        if (v.active && v.id == h) return &v;
    return nullptr;
}

void Audio_SfxSet(int h, float volume, float pan, float pitch) {
    if (!g_dev || !h) return;
    SDL_LockAudioDevice(g_dev);
    if (SfxVoice* v = findVoice(h)) { v->vol = volume; v->pan = pan; v->pitch = pitch; }
    SDL_UnlockAudioDevice(g_dev);
}

void Audio_SfxStop(int h) {
    if (!g_dev || !h) return;
    SDL_LockAudioDevice(g_dev);
    if (SfxVoice* v = findVoice(h)) v->active = false;
    SDL_UnlockAudioDevice(g_dev);
}

bool Audio_SfxPlaying(int h) {
    if (!g_dev || !h) return false;
    SDL_LockAudioDevice(g_dev);
    bool r = findVoice(h) != nullptr;
    SDL_UnlockAudioDevice(g_dev);
    return r;
}

void Audio_SetSfxVolume(float v) { g_sfxVolume = v; }

static void SDLCALL mix(void*, Uint8* out8, int len) {
    int16_t* out = (int16_t*)out8;
    int frames = len / 4;
    memset(out8, 0, (size_t)len);
    struct SfxAfter { int16_t* o; int f; ~SfxAfter() { mixSfx(o, f); } } sfxAfter{out, frames};
    if (!g_music || !g_music->open || g_music->paused) { g_level = 0.f; return; }
    Stream& s = *g_music;
    const double step = (double)s.rate / kOutRate;
    const float vol = g_volume.load();
    float peak = 0;
    for (int i = 0; i < frames; ++i) {
        size_t i0 = (size_t)s.pos;
        while (i0 + 1 >= s.bufFrames) {
            if (!refill(s)) {           // end of track
                if (i0 >= s.bufFrames) { s.open = false; g_level = peak; return; }
                break;
            }
            i0 = (size_t)s.pos;
        }
        size_t i1 = std::min(i0 + 1, s.bufFrames - 1);
        float t = (float)(s.pos - (double)i0);
        for (int c = 0; c < 2; ++c) {
            int sc = s.channels == 1 ? 0 : c;
            float a = s.buf[i0 * s.channels + sc], b = s.buf[i1 * s.channels + sc];
            float v = (a + (b - a) * t) * vol;
            v = std::max(-32768.f, std::min(32767.f, v));
            out[i * 2 + c] = (int16_t)v;
            peak = std::max(peak, std::fabs(v) / 32768.f);
        }
        s.pos += step;
        s.bufStart = (size_t)s.pos > 0 ? (size_t)s.pos - 1 : 0;   // frames before this can be dropped
        s.framesPlayed++;
    }
    g_level = peak;
}

bool Audio_Init() {
    if (!(SDL_WasInit(SDL_INIT_AUDIO) & SDL_INIT_AUDIO) && SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) return false;
    SDL_AudioSpec want{}, have{};
    want.freq = kOutRate;
    want.format = AUDIO_S16SYS;
    want.channels = 2;
    want.samples = 2048;
    want.callback = mix;
    g_dev = SDL_OpenAudioDevice(nullptr, 0, &want, &have, 0);   // no allowed changes: SDL converts for us
    if (!g_dev) { fprintf(stderr, "audio: %s\n", SDL_GetError()); return false; }
    SDL_PauseAudioDevice(g_dev, 0);
    return true;
}

static void freeStream(Stream* s) {
    if (!s) return;
    mp3dec_ex_close(&s->dec);
    delete s;
}
static Stream* detachMusic() {   // caller frees outside the lock
    SDL_LockAudioDevice(g_dev);
    Stream* old = g_music;
    g_music = nullptr;
    SDL_UnlockAudioDevice(g_dev);
    return old;
}

void Audio_Shutdown() {
    if (!g_dev) return;
    freeStream(detachMusic());
    SDL_CloseAudioDevice(g_dev);
    g_dev = 0;
}

bool Audio_PlayMusic(const std::string& path, bool loop) {
    if (!g_dev) return false;
    Stream* s = new Stream();
    // Opened (memory-mapped + frame-indexed) outside the audio lock so playback doesn't glitch.
    // MP3D_SEEK_TO_SAMPLE lets looping seek back to sample 0 exactly.
    if (!s->source.open(path) || mp3dec_ex_open_cb(&s->dec, &s->source.io, MP3D_SEEK_TO_SAMPLE) != 0 || s->dec.info.channels <= 0 ||
        s->dec.info.hz <= 0) {
        freeStream(s);
        return false;
    }
    s->open = true;
    s->loop = loop;
    s->rate = s->dec.info.hz;
    s->channels = s->dec.info.channels;
    SDL_LockAudioDevice(g_dev);
    Stream* old = g_music;
    g_music = s;
    SDL_UnlockAudioDevice(g_dev);
    freeStream(old);
    return true;
}

void Audio_StopMusic() {
    if (!g_dev) return;
    freeStream(detachMusic());
}

void Audio_SetMusicPaused(bool p) {
    if (!g_dev) return;
    SDL_LockAudioDevice(g_dev);
    if (g_music) g_music->paused = p;
    SDL_UnlockAudioDevice(g_dev);
}

bool Audio_MusicPlaying() {
    if (!g_dev) return false;
    SDL_LockAudioDevice(g_dev);
    bool r = g_music && g_music->open && !g_music->paused;
    SDL_UnlockAudioDevice(g_dev);
    return r;
}

void Audio_SetMusicVolume(float v) { g_volume = std::max(0.f, std::min(1.f, v)); }
float Audio_MusicLevel() { return g_level.load(); }

double Audio_MusicPosition() {
    if (!g_dev) return 0;
    SDL_LockAudioDevice(g_dev);
    double r = g_music ? (double)g_music->framesPlayed / kOutRate : 0;
    SDL_UnlockAudioDevice(g_dev);
    return r;
}

bool Audio_Probe(const std::string& path, AudioTrackInfo* out) {
    Mp3Source source;
    mp3dec_ex_t d;
    if (!source.open(path) || mp3dec_ex_open_cb(&d, &source.io, MP3D_SEEK_TO_SAMPLE) != 0) return false;
    out->sampleRate = d.info.hz;
    out->channels = d.info.channels;
    out->seconds = (d.info.hz && d.info.channels) ? (double)d.samples / d.info.channels / d.info.hz : 0;
    mp3dec_ex_close(&d);
    return out->sampleRate > 0;
}
