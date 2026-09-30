// Sprite sheet description as stored in SS_*.bin (found in ROM.WAD):
//   u32 count, then count * { u16 id, u16 sheetSlot, u16 x, u16 y, i16 offX, i16 offY, u16 w, u16 h }
// Coordinates are in the original sheet's pixel space; the loose ss_*.png sheets are 2x that (see pngScaleFor).
#pragma once
#include <cstdint>
#include <string>
#include <vector>

struct SpriteDef {
    uint16_t id, sheetSlot, x, y;
    int16_t offX, offY;
    uint16_t w, h;
};

class SpriteSet {
public:
    bool parse(const std::vector<uint8_t>& bin);
    const std::vector<SpriteDef>& sprites() const { return defs_; }
    // Layout coordinates are in the original texture's pixel space; the shipped PNG is `scale` times larger.
    // Rule from the original texture loader: 2 for names containing "SS_", 1 for the Japanese assets and
    // IPhone_Hel_20x20_lrg, 2 for everything else.
    static int pngScaleFor(const std::string& fileName);
private:
    std::vector<SpriteDef> defs_;
};
