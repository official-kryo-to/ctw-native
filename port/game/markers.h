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
#pragma once
#include <cstdint>
#include <string>

class Markers {
public:
    bool load(const std::string& dataDir);   // the colours (marker_tables.bin) and the texture
    uint16_t colour(int index) const;        // RGB555 (red in the low bits), white past the table
    // sMarker3D::Render at centre (20.12) for game frame `frame`
    void renderMarker3D(const int32_t centre[3], int colourIndex, int32_t size, uint32_t frame) const;

private:
    uint16_t colours_[11] = {};
    bool coloursOk_ = false;
    int texture_ = -1;   // resource id
};
