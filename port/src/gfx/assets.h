// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kryo.to
// See LICENSE in the repository root.
// Shared game.pak / dxt.bin handles and the GL texture cache (resource id -> GL texture), used by the model and
// world views. The game itself keys textures by resource id too (cTexture uses the id as its GL name).
#pragma once
#include "rendertables.h"
#include <string>

class Pak;

bool Assets_Open(const std::string& dataDir);   // opens game.pak (required) and dxt.bin (optional)
Pak& Assets_Pak();
const RenderTables& Assets_RenderTables();
bool Assets_EffectSprite(int sprite, int& texture, uint16_t rect[4]);
unsigned Assets_Texture(int resourceId);        // 0 if the id has no texture data
bool Assets_TextureSize(int resourceId, int* width, int* height);   // loads the texture if needed
// Replaces a game texture with a PNG (mods). UVs are normalised, so any resolution works. A PNG with transparent
// pixels is a layer instead: it is drawn over the game's texture (stretched to its size), and where it is
// transparent the game's artwork shows. Returns false if the PNG cannot be read.
bool Assets_OverrideTexturePNG(int resourceId, const std::string& pngPath);
// Draws a PNG over a rectangle of a game texture (x, y, w, h as fractions of the texture). Transparent PNG pixels
// keep the game's artwork. Returns false if the PNG or the texture is unavailable.
bool Assets_PatchTexturePNG(int resourceId, float x, float y, float w, float h, const std::string& pngPath);
// Writes a game texture as a PNG (the texture as the game has it, before any mod). False if it has none.
bool Assets_ExportTexturePNG(int resourceId, const std::string& pngPath);
int Assets_TextureIdLimit();                    // texture ids are below this
// Undoes Assets_OverrideTexturePNG and Assets_PatchTexturePNG: the game's own texture is loaded again on next use.
void Assets_RestoreTexture(int resourceId);
