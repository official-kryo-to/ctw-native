// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kryo.to
// See LICENSE in the repository root.
// The mission markers' world side (iPosition and its eMarkerRepresentation shapes).
//
// sMarker3D (representation 1, HUDImpl::AddBlip's usual one) glows on the ground at its centre: two flat squares of
// the second texture of gGlobalAssetMgr +0x248 (gGameDir[13]), drawn additively without depth writes in the marker's
// colour (iPosition's 11 RGB555 colours: marker_tables.bin from setup). With s = 0.8 x its size (3 by default) and
// p = the game frame & 15, one has the half-width s (2 - p / 16), 0.6 up, with alpha 1 + 1.875 p (of 31), and the
// other the half-width s, 0.7 up, with 31 minus that alpha: a glow that closes in on the spot twice a second.
//
// sMarkerChevron (representation 5, CreateChevron) points the way in: two or four untextured chevrons in the marker
// colour, turned to its angle (Marker::SetAngle: the script's degrees less 180, drawn turned back by 180), doubled
// in size and 0.36 above the centre. Each is two arms from half its width (size / 2, or 3 / 4 of it for type 0)
// out at the back to the middle a unit further on; they are a unit apart and creep forward a unit every 8 frames,
// the first fading in and the last out (alpha 16 of 31 between), and for types 0 and 1 a brighter copy of the
// colour (each component capped at 16, plus 15) moves along them one every 4 frames. (The GL state the original
// leaves to cAreaManager::Render is taken to be alpha blending without depth writes.)
#pragma once
#include <cstdint>
#include <string>

class Markers {
public:
    bool load(const std::string& dataDir);   // the colours (marker_tables.bin) and the texture
    uint16_t colour(int index) const;        // RGB555 (red in the low bits), white past the table
    // iPosition::ColourSpriteBlips: the colour a blip palette tints its radar sprite (palettes 4..12 from the table,
    // any other 2)
    uint16_t paletteColour(int palette) const;
    // sMarker3D::Render at centre (20.12) for game frame `frame`
    void renderMarker3D(const int32_t centre[3], int colourIndex, int32_t size, uint32_t frame) const;
    // sMarkerChevron::Render (CreateChevron's eChevronType: 0 four large, 1 four, 2 two that do not light up)
    void renderChevrons(const int32_t centre[3], int colourIndex, int16_t angle, int type, int32_t size,
                        uint32_t frame) const;

private:
    uint16_t colours_[11] = {};
    uint8_t palettes_[9] = {};
    bool coloursOk_ = false;
    int texture_ = -1;   // resource id
};
