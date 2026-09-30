// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kryo.to
// See LICENSE in the repository root.
// Native audio output: SDL2 device + streaming MP3 playback (minimp3).
// The original plays its radio stations / music MP3s through OpenAL + mpg123 (cAudioManager); this is the
// platform side of that, the game's own mixing logic comes with the audio-manager port.
#pragma once
#include <string>

bool Audio_Init();
void Audio_Shutdown();

// Streams an MP3 from disk (decoded on the fly, not loaded whole). Returns false if it can't be opened.
bool Audio_PlayMusic(const std::string& path, bool loop);
void Audio_StopMusic();
void Audio_SetMusicPaused(bool paused);
bool Audio_MusicPlaying();
void Audio_SetMusicVolume(float v);      // 0..1

struct AudioTrackInfo { int sampleRate = 0, channels = 0; double seconds = 0; };
bool Audio_Probe(const std::string& path, AudioTrackInfo* out);   // reads header/length without playing
double Audio_MusicPosition();            // seconds into the current track
float Audio_MusicLevel();                // recent peak level 0..1 (for a simple meter)

// Sound effects: the game's samples are 8-bit unsigned mono PCM (cAudioBaseOAL plays them as AL_FORMAT_MONO8).
// A voice plays one sample (optionally looping) at volume 0..1, pan -1 (left) .. 1 (right) and a pitch factor.
// The data must stay valid while the voice plays. Handles are > 0; 0 = no voice free.
int Audio_SfxPlay(const unsigned char* pcm8, unsigned len, int rate, float volume, float pan, bool loop);
void Audio_SfxSet(int voice, float volume, float pan, float pitch);
void Audio_SfxStop(int voice);
bool Audio_SfxPlaying(int voice);
void Audio_SetSfxVolume(float v);        // master 0..1
