// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kryo.to
// See LICENSE in the repository root.
// The PDA's death and arrest app (Gui::cDeathArrestApp): the frame the player died on breaks into shards that fly
// apart over a second (Gui::RenderScreenShards, one of the nine Gui::ShardEffect sets from shard_tables.bin), with
// cAudioManager::PlayWasted's jingle. The arrest variant (PlayBusted) waits for the arrest task.
#pragma once
#include "lookups.h"
#include <cstdint>
#include <string>

class DeathScreen {
public:
    bool init(const std::string& dataDir);   // shard_tables.bin
    bool ok() const { return tables_.loaded; }
    // cDeathArrestApp::Init: pick the shard set (Rand32NonCritical(0) % 9), play the jingle; the next render
    // captures the frame (cRenderer::GenerateRenderToTextureTarget)
    void open(bool wasted);
    void close();
    bool active() const { return open_; }
    void tick() { if (open_) ++counter_; }   // cDeathArrestApp::Process: +0x194
    int counter() const { return counter_; }
    // after the world has been drawn: the first frame is captured and shown as it is; then the shards over black
    void render(int width, int height);
    void shutdown();

private:
    ShardTables tables_;
    bool open_ = false, captured_ = false;
    int counter_ = 0, set_ = 0;
    unsigned texture_ = 0;
    int texW_ = 0, texH_ = 0;
};
