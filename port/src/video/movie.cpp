// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kryo.to
// See LICENSE in the repository root.
#include "movie.h"
#include "audio/audio.h"
#include "os/datafile.h"
#include <algorithm>

#ifdef _WIN32
#if !defined(_WIN32_WINNT) || _WIN32_WINNT < 0x0601   // MFCreateMFByteStreamOnStream: Windows 7
#undef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif
#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mferror.h>
#include <mfreadwrite.h>
#include <shlwapi.h>

// mfplat exports this (Windows 7+), but mingw-w64's mfidl.h does not declare it
extern "C" HRESULT WINAPI MFCreateMFByteStreamOnStream(IStream* stream, IMFByteStream** byteStream);

namespace {
template <class T> void release(T*& p) { if (p) { p->Release(); p = nullptr; } }

// One source reader per stream, so the soundtrack can run ahead of the picture without buffering frames.
// The file is read through the data layer (it may be packed into the exe) and decoded from memory.
IMFSourceReader* openReader(const std::vector<uint8_t>& file, bool video) {
    IStream* memory = SHCreateMemStream(file.data(), (UINT)file.size());
    IMFByteStream* bytes = nullptr;
    if (!memory || FAILED(MFCreateMFByteStreamOnStream(memory, &bytes))) { release(memory); return nullptr; }
    release(memory);
    IMFAttributes* attributes = nullptr;
    if (FAILED(MFCreateAttributes(&attributes, 1))) { release(bytes); return nullptr; }
    attributes->SetUINT32(MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING, TRUE);   // decoder output converted to RGB32
    IMFSourceReader* reader = nullptr;
    const HRESULT hr = MFCreateSourceReaderFromByteStream(bytes, attributes, &reader);
    release(attributes);
    release(bytes);
    if (FAILED(hr)) return nullptr;
    const DWORD stream = video ? (DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM : (DWORD)MF_SOURCE_READER_FIRST_AUDIO_STREAM;
    reader->SetStreamSelection((DWORD)MF_SOURCE_READER_ALL_STREAMS, FALSE);
    IMFMediaType* type = nullptr;
    bool ok = SUCCEEDED(reader->SetStreamSelection(stream, TRUE)) && SUCCEEDED(MFCreateMediaType(&type));
    if (ok) {
        type->SetGUID(MF_MT_MAJOR_TYPE, video ? MFMediaType_Video : MFMediaType_Audio);
        type->SetGUID(MF_MT_SUBTYPE, video ? MFVideoFormat_RGB32 : MFAudioFormat_PCM);
        if (!video) type->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 16);
        ok = SUCCEEDED(reader->SetCurrentMediaType(stream, nullptr, type));
    }
    release(type);
    if (!ok) release(reader);
    return reader;
}
}

struct Movie::Impl {
    bool comStarted = false, mfStarted = false;
    IMFSourceReader* video = nullptr;
    IMFSourceReader* audio = nullptr;
    int stride = 0, rate = 0, channels = 0;
    bool videoEnded = false, audioEnded = true;
    IMFSample* pending = nullptr;   // the next picture, decoded but not yet due
    LONGLONG pendingTime = 0;
    ~Impl() {
        release(pending);
        release(video);
        release(audio);
        if (rate) Audio_PcmEnd();
        if (mfStarted) MFShutdown();
        if (comStarted) CoUninitialize();
    }
};

Movie::Movie() = default;
Movie::~Movie() { close(); }

void Movie::close() {
    impl_.reset();
    rgba_.clear();
    width_ = height_ = 0;
    clock_ = 0;
    pictures_ = 0;
}

bool Movie::open(const std::string& path) {
    close();
    auto impl = std::make_unique<Impl>();
    const HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    impl->comStarted = SUCCEEDED(com);   // (RPC_E_CHANGED_MODE: COM is already up on this thread)
    if (FAILED(MFStartup(MF_VERSION, MFSTARTUP_FULL))) return false;
    impl->mfStarted = true;
    std::vector<uint8_t> file;
    if (!Data_ReadAll(path, file) || file.empty()) return false;
    impl->video = openReader(file, true);
    if (!impl->video) return false;
    IMFMediaType* type = nullptr;
    if (FAILED(impl->video->GetCurrentMediaType((DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, &type))) return false;
    UINT32 w = 0, h = 0;
    MFGetAttributeSize(type, MF_MT_FRAME_SIZE, &w, &h);
    UINT32 stride = 0;
    impl->stride = SUCCEEDED(type->GetUINT32(MF_MT_DEFAULT_STRIDE, &stride)) ? (int)(INT32)stride : (int)w * 4;
    release(type);
    if (!w || !h) return false;
    width_ = (int)w; height_ = (int)h;
    impl->audio = openReader(file, false);
    if (impl->audio && SUCCEEDED(impl->audio->GetCurrentMediaType((DWORD)MF_SOURCE_READER_FIRST_AUDIO_STREAM, &type))) {
        impl->rate = (int)MFGetAttributeUINT32(type, MF_MT_AUDIO_SAMPLES_PER_SECOND, 0);
        impl->channels = (int)MFGetAttributeUINT32(type, MF_MT_AUDIO_NUM_CHANNELS, 0);
        release(type);
    }
    if (impl->rate > 0 && impl->channels > 0) {
        Audio_PcmBegin(impl->rate, impl->channels);
        impl->audioEnded = false;
    } else {
        impl->rate = 0;
        release(impl->audio);
    }
    impl_ = std::move(impl);
    return true;
}

bool Movie::update(double elapsed) {
    if (!impl_) return false;
    Impl& m = *impl_;
    // keep about half a second of sound queued
    while (!m.audioEnded && Audio_PcmQueued() < m.rate / 2) {
        DWORD flags = 0;
        IMFSample* sample = nullptr;
        if (FAILED(m.audio->ReadSample((DWORD)MF_SOURCE_READER_FIRST_AUDIO_STREAM, 0, nullptr, &flags, nullptr, &sample)) ||
            (flags & MF_SOURCE_READERF_ENDOFSTREAM)) {
            release(sample);
            m.audioEnded = true;
            break;
        }
        if (!sample) continue;
        IMFMediaBuffer* buffer = nullptr;
        if (SUCCEEDED(sample->ConvertToContiguousBuffer(&buffer))) {
            BYTE* data = nullptr;
            DWORD length = 0;
            if (SUCCEEDED(buffer->Lock(&data, nullptr, &length))) {
                Audio_PcmPush((const int16_t*)data, (int)(length / (2u * m.channels)));
                buffer->Unlock();
            }
        }
        release(buffer);
        release(sample);
    }
    const bool soundLeft = m.rate && (!m.audioEnded || Audio_PcmQueued() > 1);
    clock_ = soundLeft ? Audio_PcmPosition() : std::max(clock_, elapsed);
    const LONGLONG now = (LONGLONG)(clock_ * 1e7);
    while (!m.videoEnded) {
        if (!m.pending) {
            DWORD flags = 0;
            IMFSample* sample = nullptr;
            if (FAILED(m.video->ReadSample((DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, 0, nullptr, &flags, &m.pendingTime,
                                           &sample)) || (flags & MF_SOURCE_READERF_ENDOFSTREAM)) {
                release(sample);
                m.videoEnded = true;
                break;
            }
            if (!sample) continue;
            m.pending = sample;
        }
        if (m.pendingTime > now) break;
        IMFMediaBuffer* buffer = nullptr;
        if (SUCCEEDED(m.pending->ConvertToContiguousBuffer(&buffer))) {
            BYTE* data = nullptr;
            DWORD length = 0;
            if (SUCCEEDED(buffer->Lock(&data, nullptr, &length))) {
                const int stride = m.stride ? m.stride : width_ * 4;
                const int row = std::abs(stride);
                if ((DWORD)row * (DWORD)height_ <= length) {
                    rgba_.resize((size_t)width_ * height_ * 4);
                    for (int y = 0; y < height_; ++y) {   // BGRX rows (bottom-up when the stride is negative)
                        const BYTE* src = data + (size_t)(stride < 0 ? height_ - 1 - y : y) * row;
                        uint8_t* dst = rgba_.data() + (size_t)y * width_ * 4;
                        for (int x = 0; x < width_; ++x) {
                            dst[x * 4] = src[x * 4 + 2];
                            dst[x * 4 + 1] = src[x * 4 + 1];
                            dst[x * 4 + 2] = src[x * 4];
                            dst[x * 4 + 3] = 255;
                        }
                    }
                    ++pictures_;
                }
                buffer->Unlock();
            }
        }
        release(buffer);
        release(m.pending);
    }
    return !m.videoEnded || m.pending || soundLeft;
}

#else

struct Movie::Impl {};
Movie::Movie() = default;
Movie::~Movie() { close(); }
void Movie::close() { impl_.reset(); rgba_.clear(); width_ = height_ = 0; clock_ = 0; pictures_ = 0; }
bool Movie::open(const std::string&) { return false; }   // no system decoder wired up on this platform yet
bool Movie::update(double) { return false; }

#endif
