// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kryo.to
// See LICENSE in the repository root.
// Creates GL textures for game resource ids, following cResourceManager::Load + cTexture::TexImage:
//  1. dxt.bin replacement if one exists (GetDXTData)
//  2. otherwise the game.pak resource: 12-byte header {u16 w, u16 h, u16 format, u8, u8, u32 dataSize} + data
//     format 0xBEEF          PNG file (PNGDecompress)
//     0x8033 / 0x8034        RGBA 4444 / 5551 (16-bit)
//     0x8363                 RGB 565
//     0x1909 / 0x190A        luminance / luminance-alpha (8-bit)
//     0x83F0..0x83F3         DXT1/DXT3/DXT5 (compressed upload)
//     0x8C00..0x8C03         PVRTC 4/2 bpp (decoded in software here; desktop GPUs lack PVRTC)
#pragma once
#include <cstdint>
#include <string>

class Pak;
class DxtBin;

struct TexInfo { unsigned gl = 0; int width = 0, height = 0; uint16_t format = 0; bool fromDxt = false; };

class TextureLoader {
public:
    void init(const Pak* pak, const DxtBin* dxt) { pak_ = pak; dxt_ = dxt; }
    // Uploads resource `id` into a new GL texture (wrap = repeat). Returns false if the id has no texture data.
    bool load(uint32_t id, TexInfo* out) const;
private:
    const Pak* pak_ = nullptr;
    const DxtBin* dxt_ = nullptr;
};

// PVRTC1 decoder (2 or 4 bpp) to RGBA8.
void DecodePVRTC(const uint8_t* data, int width, int height, bool twoBpp, uint8_t* rgbaOut);
