// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kryo.to
// See LICENSE in the repository root.
// Movie playback (the original's OS_MoviePlayinWindow, which Android hands to its MediaPlayer): the user's own
// H.264 / AAC MP4 is decoded by the operating system (Media Foundation on Windows). The soundtrack is pushed into
// the audio mixer (Audio_Pcm*) and the picture follows the sound's clock. Elsewhere open() fails and callers skip
// the movie.
#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

class Movie {
public:
    Movie();
    ~Movie();
    Movie(const Movie&) = delete;
    Movie& operator=(const Movie&) = delete;
    bool open(const std::string& path);   // false: missing file or no decoder on this platform
    void close();
    // Decodes up to the playback clock (the sound's position, or `elapsed` seconds without sound) and keeps the
    // soundtrack fed. False once both streams have ended.
    bool update(double elapsed);
    bool frameReady() const { return !rgba_.empty(); }
    const std::vector<uint8_t>& rgba() const { return rgba_; }   // the current picture, top row first
    int width() const { return width_; }
    int height() const { return height_; }
    double clock() const { return clock_; }
    unsigned pictures() const { return pictures_; }   // pictures shown so far (a new one needs uploading)
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    std::vector<uint8_t> rgba_;
    int width_ = 0, height_ = 0;
    double clock_ = 0;
    unsigned pictures_ = 0;
};
