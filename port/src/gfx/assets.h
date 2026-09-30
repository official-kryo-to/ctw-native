// Shared game.pak / dxt.bin handles and the GL texture cache (resource id -> GL texture), used by the model and
// world views. The game itself keys textures by resource id too (cTexture uses the id as its GL name).
#pragma once
#include <string>

class Pak;

bool Assets_Open(const std::string& dataDir);   // opens game.pak (required) and dxt.bin (optional)
Pak& Assets_Pak();
unsigned Assets_Texture(int resourceId);        // 0 if the id has no texture data
bool Assets_TextureSize(int resourceId, int* width, int* height);   // loads the texture if needed
// Replaces a game texture with a PNG (mods). UVs are normalised, so any resolution works. Returns false if the
// PNG cannot be read.
bool Assets_OverrideTexturePNG(int resourceId, const std::string& pngPath);
// Undoes Assets_OverrideTexturePNG: the game's own texture is loaded again on next use.
void Assets_RestoreTexture(int resourceId);
