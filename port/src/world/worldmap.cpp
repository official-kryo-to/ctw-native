// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kryo.to
// See LICENSE in the repository root.
#include "worldmap.h"
#include "gfx/assets.h"
#include "gfx/model.h"
#include "os/pak.h"
#include <algorithm>
#include <cstring>
#include <set>

template <class T> static bool rd(const std::vector<uint8_t>& b, size_t o, T* v) {
    if (o + sizeof(T) > b.size()) return false;
    memcpy(v, &b[o], sizeof(T));
    return true;
}

void WorldAnim::step() {
    current = 0;
    for (Track& t : tracks) {
        if (--t.counter < 1) {
            for (int guard = 0; guard < 32; ++guard) {
                t.slot = (int16_t)((t.slot + 1) & 31);
                t.counter = t.frames[t.slot];
                if (t.counter >= 0) break;
            }
        }
        current |= t.mask[t.slot];
    }
}

bool WorldMap::init() {
    std::vector<uint8_t> h;
    if (!Assets_Pak().read(1, h) || h.size() < 8 + (size_t)kRows * 0x76) return false;
    memcpy(&seaModel_, &h[0], 2);
    memcpy(&lakeModel_, &h[2], 2);
    grid_.resize((size_t)kCols * kRows);
    for (int r = 0; r < kRows; ++r)
        for (int c = 0; c < kCols; ++c) memcpy(&grid_[(size_t)r * kCols + c], &h[8 + (size_t)r * 0x76 + (size_t)c * 2], 2);
    return true;
}

int WorldMap::blockId(int c, int r) const {
    if (c < 0 || r < 0 || c >= kCols || r >= kRows) return -1;
    uint16_t id = grid_[(size_t)r * kCols + c];
    return id == 0xFFFF ? -1 : id;
}

void WorldMap::blockCentre(int c, int r, float* x, float* y) {
    *x = 120.f * c - 3510.f + 60.f;
    *y = 120.f * r - 2490.f + 60.f;
}

bool WorldMap::build(int c, int r, const std::map<WorldObjectKey, int>& skip, int self, WorldBlockMesh& out) const {
    out = WorldBlockMesh();
    int id = blockId(c, r);
    std::vector<uint8_t> b;
    if (id < 0 || !Assets_Pak().read((uint32_t)id, b) || b.size() < 0x40) return false;

    int16_t rot[9];
    int32_t tr[3];
    memcpy(rot, &b[0], 18);
    memcpy(tr, &b[20], 12);
    float M[3][3], T[3];
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) M[i][j] = rot[i * 3 + j] / 4096.f;
        T[i] = tr[i] / 4096.f;
    }

    std::map<uint32_t, bool> seenOffsets;   // an object listed twice in the same block
    std::set<WorldObjectKey> seenKeys;
    std::map<std::pair<uint16_t, uint32_t>, size_t> drawIndex;   // (texture, flags|list|alpha) -> draw
    std::vector<std::vector<WorldVertex>> drawVerts;

    for (int sub = 0; sub < 4; ++sub) {
        uint16_t so;
        if (!rd(b, 0x20 + sub * 2, &so)) return false;
        uint16_t nInst, nGroups, n20a, nLights;
        if (!rd(b, so + 0x2A, &nInst) || !rd(b, so + 0x2E, &nGroups) || !rd(b, so + 0x2C, &n20a) ||
            !rd(b, so + 0x30, &nLights))
            return false;
        size_t inst0 = (size_t)so + 0x34 + (size_t)nGroups * 16;
        size_t light0 = inst0 + (size_t)nInst * 16 + (size_t)n20a * 20;
        for (int k = 0; k < nLights; ++k) {
            size_t lo = light0 + (size_t)k * 20;
            if (lo + 20 > b.size()) break;
            int32_t p[4];
            uint16_t id, col;
            memcpy(p, &b[lo], 16);
            memcpy(&id, &b[lo + 16], 2);
            memcpy(&col, &b[lo + 18], 2);
            int s = (int)(((int64_t)p[3] * 0xCCC) >> 12);   // cLight::Initalise: size * 0.8, capped at 1.0
            out.lights.push_back({p[0], p[1], p[2], (int16_t)std::min(s, 0x1000), col, id});
        }
        for (int k = 0; k < nInst; ++k) {
            size_t io = inst0 + (size_t)k * 16;
            uint16_t slot;
            uint8_t listIdx;
            uint32_t modelId, off;
            if (!rd(b, io, &slot) || !rd(b, io + 2, &listIdx) || !rd(b, io + 4, &modelId) || !rd(b, io + 8, &off)) return false;
            ++out.instances;
            if (off + 0x30 > b.size() || b[off] != 'M' || b[off + 1] != 'G') continue;
            int32_t bb[6];
            memcpy(bb, &b[off + 0x10], sizeof bb);
            WorldObjectKey key = {slot, bb[0], bb[1], bb[2], bb[3], bb[4], bb[5]};
            if (!seenKeys.insert(key).second) continue;
            out.objects.push_back(key);
            auto owner = skip.find(key);
            if (owner != skip.end() && owner->second != self) continue;

            uint16_t mid16 = (uint16_t)modelId;
            if (mid16 == seaModel_ || mid16 == lakeModel_) {   // water: drawn by cWaterRenderBlock, not the model
                // cWorldModelInstance::Render: render pos = bounds min + (max - min) / 2 (fixed point)
                int32_t cx = bb[0] + ((bb[3] - bb[0]) >> 1), cy = bb[1] + ((bb[4] - bb[1]) >> 1);
                bool sea = mid16 == seaModel_;
                out.water.push_back({cx / 4096.f, cy / 4096.f, sea ? -7.5f : -2.5f, sea});
                continue;
            }

            uint16_t A, D;
            memcpy(&A, &b[off + 2], 2);
            memcpy(&D, &b[off + 6], 2);
            size_t need = 16 + (size_t)b[off + 4] * 32 + (size_t)D * 16 + (size_t)b[off + 5] * 12 + (size_t)A * 192;
            if (off + need > b.size()) continue;
            Model m;
            if (!m.parse(std::vector<uint8_t>(b.begin() + off, b.begin() + off + need))) continue;
            ++out.drawn;

            // Visibility mask: static models draw every batch with a non-zero mask; animated models (A > 0: signs,
            // lights...) switch batches on and off per frame (WorldAnim), so their batches keep their own mask.
            int anim = -1;
            if (A > 0) {
                WorldAnim wa;
                size_t ao = off + 16 + (size_t)b[off + 4] * 32 + (size_t)D * 16 + (size_t)b[off + 5] * 12;
                for (int a = 0; a < A; ++a) {
                    WorldAnim::Track t;
                    memcpy(t.mask, &b[ao + (size_t)a * 192], 128);
                    memcpy(t.frames, &b[ao + (size_t)a * 192 + 128], 64);
                    wa.tracks.push_back(t);
                }
                anim = (int)out.anims.size();
                out.anims.push_back(wa);
                out.anims.back().step();
            }
            uint8_t list = listIdx < 9 ? static_cast<uint8_t>(Assets_RenderTables().renderList[listIdx]) : 0;
            const float s = m.scale;
            for (const ModelBatch& mb : m.batches) {
                if (!mb.mask) continue;
                uint8_t dl = (mb.flags & 0x08) ? list : 0;   // opaque materials always go to list 0
                uint32_t alpha5 = mb.alpha >> 3;              // world: Draw(..., alpha >> 3)
                uint32_t mat = (uint32_t)mb.flags | (uint32_t)dl << 8 | alpha5 << 16;
                auto key2 = std::make_pair(mb.texture, mat);
                auto it = anim >= 0 ? drawIndex.end() : drawIndex.find(key2);
                if (it == drawIndex.end()) {
                    size_t di = out.draws.size();
                    if (anim < 0) it = drawIndex.emplace(key2, di).first;
                    WorldDraw d{mb.texture, mb.flags, dl, alpha5 / 31.f, 0, 0};
                    if (anim >= 0) { d.anim = anim; d.mask = mb.mask; }
                    out.draws.push_back(d);
                    drawVerts.emplace_back();
                    if (anim >= 0) it = drawIndex.end();
                }
                std::vector<WorldVertex>& dv = drawVerts[anim >= 0 ? drawVerts.size() - 1 : it->second];
                const NodeMatrix& nm = m.world[mb.node];
                for (uint32_t i = 2; i < mb.count; ++i) {
                    const ModelVertex* v[3] = {&m.verts[mb.firstVertex + i - 2], &m.verts[mb.firstVertex + i - 1],
                                               &m.verts[mb.firstVertex + i]};
                    if (i & 1) std::swap(v[1], v[2]);   // strip winding alternates
                    auto same = [](const ModelVertex* a, const ModelVertex* c2) { return a->x == c2->x && a->y == c2->y && a->z == c2->z; };
                    if (same(v[0], v[1]) || same(v[1], v[2]) || same(v[0], v[2])) continue;
                    for (int q = 0; q < 3; ++q) {
                        float p[3] = {v[q]->x * s, v[q]->y * s, v[q]->z * s};
                        float n[3] = {v[q]->nx / 32767.f, v[q]->ny / 32767.f, v[q]->nz / 32767.f};
                        float lp[3], ln[3], wp[3], wn[3];
                        for (int a = 0; a < 3; ++a) {
                            lp[a] = nm.r[a][0] * p[0] + nm.r[a][1] * p[1] + nm.r[a][2] * p[2] + nm.t[a];
                            ln[a] = nm.r[a][0] * n[0] + nm.r[a][1] * n[1] + nm.r[a][2] * n[2];
                        }
                        for (int a = 0; a < 3; ++a) {
                            wp[a] = M[a][0] * lp[0] + M[a][1] * lp[1] + M[a][2] * lp[2] + T[a];
                            wn[a] = M[a][0] * ln[0] + M[a][1] * ln[1] + M[a][2] * ln[2];
                        }
                        dv.push_back({wp[0], wp[1], wp[2], wn[0], wn[1], wn[2], v[q]->u / 2048.f, v[q]->v / 2048.f});
                    }
                }
            }
        }
    }
    for (size_t i = 0; i < out.draws.size(); ++i) {
        out.draws[i].first = (uint32_t)out.verts.size();
        out.draws[i].count = (uint32_t)drawVerts[i].size();
        out.verts.insert(out.verts.end(), drawVerts[i].begin(), drawVerts[i].end());
    }
    return true;
}
