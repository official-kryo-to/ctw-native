// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kryo.to
// See LICENSE in the repository root.
#include "collision.h"
#include "props.h"
#include "os/gamefs.h"
#include <stb_image.h>
#include <glad/gl.h>
#include <algorithm>
#include <cmath>
#include <cstring>

static int fastsin(int a) { return (int)(sinf((float)a * 9.587378e-05f) * 4096.f); }
static int32_t fx(float v) { return (int32_t)lroundf(v * 4096.f); }

bool Collision::init(const std::string& dataDir) {
    GameFs fs;
    return fs.open(dataDir) && fs.read("world.bin", world_) && world_.size() > 4;
}

// cWorldSector::DataLoaded: per triangle, the plane point (centroid), bounding radius, face normal and the three
// inward edge normals are computed from the vertices when the sector loads.
void Collision::finishTriangle(Tri& t, const std::vector<int32_t>& V) {
    const int32_t* a = &V[t.v[0] * 3];
    const int32_t* b = &V[t.v[1] * 3];
    const int32_t* c = &V[t.v[2] * 3];
    int64_t e1x = b[0] - a[0], e1y = b[1] - a[1], e1z = b[2] - a[2];
    int64_t e2x = c[0] - a[0], e2y = c[1] - a[1], e2z = c[2] - a[2];
    int32_t ox = (int32_t)(((e2x + e1x) * 0x555) >> 12), oy = (int32_t)(((e2y + e1y) * 0x555) >> 12),
            oz = (int32_t)(((e2z + e1z) * 0x555) >> 12);
    t.p[0] = a[0] + ox; t.p[1] = a[1] + oy; t.p[2] = a[2] + oz;
    auto len = [](int64_t x, int64_t y, int64_t z) { return (int64_t)std::sqrt((double)(x * x + y * y + z * z)); };
    int64_t r = std::max({len(ox, oy, oz), len(e1x, e1y, e1z), len(e2x, e2y, e2z)});
    t.radius = (uint8_t)((r + 0x1000) >> 12);
    auto norm = [](int64_t x, int64_t y, int64_t z, int16_t out[3]) {   // Normalise(tv3d): length 1.0 = 4096
        double l = std::sqrt((double)(x * x + y * y + z * z));
        if (l <= 0) { out[0] = out[1] = out[2] = 0; return; }
        out[0] = (int16_t)lround(x * 4096.0 / l); out[1] = (int16_t)lround(y * 4096.0 / l); out[2] = (int16_t)lround(z * 4096.0 / l);
    };
    norm((e2z * e1y - e2y * e1z) >> 12, (e2x * e1z - e2z * e1x) >> 12, (e2y * e1x - e2x * e1y) >> 12, t.n);
    int64_t nx = t.n[0], ny = t.n[1], nz = t.n[2];
    auto edge = [&](int64_t dx, int64_t dy, int64_t dz, int16_t out[3]) {
        norm((nz * dy - ny * dz) >> 12, (nx * dz - nz * dx) >> 12, (ny * dx - nx * dy) >> 12, out);
    };
    edge(e1x, e1y, e1z, t.en[0]);
    edge(e2x - e1x, e2y - e1y, e2z - e1z, t.en[1]);
    edge(-e2x, -e2y, -e2z, t.en[2]);
}

const Collision::Cell* Collision::cell(int cx, int cy) {
    if (cx < 0 || cy < 0 || cy >= 100) return nullptr;
    int key = cx * 100 + cy;
    auto it = cells_.find(key);
    if (it != cells_.end()) return it->second->loaded ? it->second.get() : nullptr;
    auto c = std::make_unique<Cell>();
    Cell& C = *c;
    cells_[key] = std::move(c);
    if ((size_t)(key + 1) * 4 > world_.size()) return nullptr;
    uint32_t e;
    memcpy(&e, &world_[(size_t)key * 4], 4);
    uint32_t off = (e & 0x1FFFF) * 64, csz = ((e >> 17) & 0x7F) * 64, usz = csz + ((e >> 24) & 0x7F) * 64;
    if (!csz || off + csz > world_.size()) return nullptr;
    std::vector<uint8_t> d(usz + 64);
    int n = stbi_zlib_decode_buffer((char*)d.data(), (int)d.size(), (const char*)&world_[off], (int)csz);
    if (n <= 0) return nullptr;
    d.resize((size_t)n);

    size_t p = 0;
    auto section = [&](std::vector<uint8_t>& out) {
        uint32_t len = 0;
        if (p + 4 > d.size()) return;
        memcpy(&len, &d[p], 4);
        p += 4;
        if (p + len > d.size()) len = (uint32_t)(d.size() - p);
        out.assign(d.begin() + p, d.begin() + p + len);
        p += len;
    };
    std::vector<uint8_t> sec[11];
    for (auto& s : sec) section(s);
    auto count = [](const std::vector<uint8_t>& s) { uint32_t k = 0; if (s.size() >= 4) memcpy(&k, s.data(), 4); return k; };
    if (uint32_t k = count(sec[0]); k && sec[0].size() >= 4 + k * 28) {
        C.boxes.resize(k);
        memcpy(C.boxes.data(), &sec[0][4], k * 28);
    }
    if (uint32_t k = count(sec[1]); k && sec[1].size() >= 4 + k * 24) {
        C.cyls.resize(k);
        memcpy(C.cyls.data(), &sec[1][4], k * 24);
    }
    if (uint32_t k = count(sec[2])) {
        const std::vector<uint8_t>& s = sec[2];
        size_t q = 4;
        for (uint32_t i = 0; i < k && q + 20 <= s.size(); ++i) {
            Mesh m;
            memcpy(&m.minX, &s[q], 16);
            uint16_t nv, nt;
            memcpy(&nv, &s[q + 16], 2);
            memcpy(&nt, &s[q + 18], 2);
            q += 20;
            if (q + nv * 12 + nt * 40 > s.size()) break;
            m.verts.resize((size_t)nv * 3);
            memcpy(m.verts.data(), &s[q], (size_t)nv * 12);
            q += (size_t)nv * 12;
            m.tris.resize(nt);
            for (uint16_t t = 0; t < nt; ++t) {
                memcpy(&m.tris[t], &s[q + (size_t)t * 40], 40);
                Tri& tr = m.tris[t];
                if (tr.v[0] < nv && tr.v[1] < nv && tr.v[2] < nv) finishTriangle(tr, m.verts);
                else tr.n[0] = tr.n[1] = tr.n[2] = 0;
            }
            q += (size_t)nt * 40;
            C.meshes.push_back(std::move(m));
        }
    }
    if (sec[10].size() >= 400) C.groundMap.assign(sec[10].begin(), sec[10].begin() + 400);
    if (uint32_t k = count(sec[6]); k && sec[6].size() >= 4 + k * 20) {
        C.props.resize(k);
        memcpy(C.props.data(), &sec[6][4], k * 20);
        if (propLibrary_) {   // street furniture is solid too (cDynamicProp collision)
            C.propShapes.resize(k);
            for (uint32_t i = 0; i < k; ++i) {
                Cell::PropShapes& ps = C.propShapes[i];
                ps.box0 = (uint32_t)C.boxes.size();
                ps.cyl0 = (uint32_t)C.cyls.size();
                ps.mesh0 = (uint32_t)C.meshes.size();
                propLibrary_->shapes(C.props[i], C.boxes, C.cyls, C.meshes);
                ps.boxes = (uint32_t)C.boxes.size() - ps.box0;
                ps.cyls = (uint32_t)C.cyls.size() - ps.cyl0;
                ps.meshes = (uint32_t)C.meshes.size() - ps.mesh0;
            }
        }
    }
    if (uint32_t k = count(sec[8]); k && sec[8].size() >= 4 + k * 16) {
        C.emitters.resize(k);
        memcpy(C.emitters.data(), &sec[8][4], k * 16);
    }
    // cWorldSector::DataLoaded: u32 node table offset at +0x20, u16 link count +0x24, u16 node count +0x26,
    // u8 bridge count +0x2A; bridges {u16 node, u16 sector} from +0; u16 links from +0x2C.
    if (const std::vector<uint8_t>& s = sec[3]; s.size() >= 0x2C) {
        uint32_t nodeOffset; uint16_t links, nodes;
        memcpy(&nodeOffset, &s[0x20], 4); memcpy(&links, &s[0x24], 2); memcpy(&nodes, &s[0x26], 2);
        const unsigned bridges = std::min<unsigned>(s[0x2A], 8);
        if (0x2C + (size_t)links * 2 <= s.size() && nodeOffset + (size_t)nodes * 10 <= s.size()) {
            C.pedPaths.links.resize(links);
            memcpy(C.pedPaths.links.data(), &s[0x2C], (size_t)links * 2);
            C.pedPaths.nodes.resize(nodes);
            memcpy(C.pedPaths.nodes.data(), &s[nodeOffset], (size_t)nodes * 10);
            for (unsigned b = 0; b < bridges; ++b) {
                uint16_t node, sector;
                memcpy(&node, &s[b * 4], 2); memcpy(&sector, &s[b * 4 + 2], 2);
                C.pedPaths.bridges.push_back({node, (uint16_t)(sector & 0x3FFF)});
            }
        }
    }
    if (uint32_t k = count(sec[7]); k && sec[7].size() >= 4 + k * 20) {
        C.carGens.resize(k);
        memcpy(C.carGens.data(), &sec[7][4], k * 20);
    }
    C.loaded = true;
    return &C;
}

void Collision::setPropState(int cx, int cy, int index, uint16_t state) {
    Cell* C = mutableCell(cx, cy);
    if (C && index >= 0 && index < (int)C->props.size()) C->props[index].state = state;
}

void Collision::setPropSolid(int cx, int cy, int index, bool solid) {
    Cell* C = mutableCell(cx, cy);
    if (!C || index < 0 || index >= (int)C->propShapes.size()) return;
    Cell::PropShapes& ps = C->propShapes[index];
    if (ps.solid == solid) return;
    ps.solid = solid;
    for (uint32_t i = 0; i < ps.meshes; ++i) {
        Mesh& m = C->meshes[ps.mesh0 + i];
        const int32_t dz = solid ? 0x20000000 : -0x20000000;
        for (size_t k = 2; k < m.verts.size(); k += 3) m.verts[k] += dz;
        for (Tri& t : m.tris) t.p[2] += dz;
    }
    if (!solid && ps.savedBoxes.empty() && ps.savedCyls.empty()) {   // park the shapes far below the world
        for (uint32_t i = 0; i < ps.boxes; ++i) {
            ps.savedBoxes.push_back(C->boxes[ps.box0 + i]);
            C->boxes[ps.box0 + i].cz = -0x40000000;
            C->boxes[ps.box0 + i].hz = 0;
        }
        for (uint32_t i = 0; i < ps.cyls; ++i) {
            ps.savedCyls.push_back(C->cyls[ps.cyl0 + i]);
            C->cyls[ps.cyl0 + i].z = -0x40000000;
            C->cyls[ps.cyl0 + i].h = 0;
        }
    } else if (solid) {
        for (size_t i = 0; i < ps.savedBoxes.size(); ++i) C->boxes[ps.box0 + i] = ps.savedBoxes[i];
        for (size_t i = 0; i < ps.savedCyls.size(); ++i) C->cyls[ps.cyl0 + i] = ps.savedCyls[i];
        ps.savedBoxes.clear();
        ps.savedCyls.clear();
    }
}

const std::vector<Collision::CityEmitter>* Collision::emitters(int cx, int cy) {
    const Cell* C = cell(cx, cy);
    return C && !C->emitters.empty() ? &C->emitters : nullptr;
}

const std::vector<Collision::Prop>* Collision::props(int cx, int cy) {
    const Cell* C = cell(cx, cy);
    return C && !C->props.empty() ? &C->props : nullptr;
}

const std::vector<Collision::CarGen>* Collision::carGens(int cx, int cy) {
    const Cell* C = cell(cx, cy);
    return C && !C->carGens.empty() ? &C->carGens : nullptr;
}

const Collision::PedPaths* Collision::pedPaths(int cx, int cy) {
    const Cell* C = cell(cx, cy);
    return C && !C->pedPaths.nodes.empty() ? &C->pedPaths : nullptr;
}

static void cellOf(int32_t x, int32_t y, int* cx, int* cy);
void Collision::cellOfPos(int32_t x, int32_t y, int& cx, int& cy) { cellOf(x, y, &cx, &cy); }

static void cellOf(int32_t x, int32_t y, int* cx, int* cy) {
    *cx = (int)std::floor((x + 0xDAC000) / (double)0x32000);
    *cy = (int)std::floor((y + 0x9C4000) / (double)0x32000);
}

// Box-local point (CCollision::GetGround / CircleVBox): rotate the offset from the centre by -angle.
static void toBoxLocal(const int32_t* b, int16_t angle, int32_t x, int32_t y, int32_t* lx, int32_t* ly) {
    if (!angle) { *lx = x; *ly = y; return; }
    int64_t c = fastsin(angle + 0x4000), s = fastsin(angle), dx = x - b[0], dy = y - b[1];
    *lx = (int32_t)(((int64_t)b[0] * 0x1000 + c * dx + s * dy) >> 12);
    *ly = (int32_t)(((int64_t)b[1] * 0x1000 + c * dy - s * dx) >> 12);
}

Collision::Ground Collision::ground(float xf, float yf, float zf) {
    Ground g{zf, 0, {0, 0, 1}};
    int32_t x = fx(xf), y = fx(yf), z = fx(zf);
    int cx, cy;
    cellOf(x, y, &cx, &cy);
    const Cell* C = cell(cx, cy);
    if (!C) return g;
    int32_t gz = (int32_t)0xFFFF8800;   // sea, -7.5
    g.surface = 2;
    if (!C->groundMap.empty()) {
        uint32_t col = (uint32_t)(((int64_t)(x - cx * 0x32000 + 0xDAC000) * 0xCCC) >> 24);
        uint32_t row = (uint32_t)(((int64_t)(y - cy * 0x32000 + 0x9C4000) * 0xCCC) >> 24);
        uint32_t idx = std::min(col, 39u) + std::min(row, 39u) * 40;
        int type = C->groundMap[idx >> 2] >> ((col & 3) * 2) & 3;
        if (type == 2) { gz = (int32_t)0xFFFFD800; g.surface = 2; }        // lake, -2.5
        else if (type == 1 && z > -0x801) { gz = 0; g.surface = 0; }        // land
    }
    // meshes: the first triangle per mesh that the vertical line through (x, y) hits between gz and z
    for (const Mesh& m : C->meshes) {
        if (!(m.minX <= x && m.minY <= y && x < m.maxX && y < m.maxY)) continue;
        for (const Tri& t : m.tris) {
            if (!t.n[2]) continue;
            // CCollision::VerticleLineSegmentToTriangle
            int64_t hz = t.p[2] - ((int64_t)t.n[0] * (x - t.p[0]) + (int64_t)t.n[1] * (y - t.p[1])) / t.n[2];
            bool inside = true;
            for (int e = 0; e < 3 && inside; ++e) {
                const int32_t* v = &m.verts[t.v[e] * 3];
                int64_t d = (int64_t)t.en[e][0] * (x - v[0]) + (int64_t)t.en[e][1] * (y - v[1]) + (int64_t)t.en[e][2] * (hz - v[2]);
                inside = d < 1;
            }
            if (inside && gz < hz && hz < z) {
                gz = (int32_t)hz;
                g.surface = 0;
                for (int i = 0; i < 3; ++i) g.normal[i] = t.n[i] / 4096.f;
                break;
            }
        }
    }
    // boxes: tops no more than 0.5 above z
    for (const Box& b : C->boxes) {
        int32_t top = b.cz + b.hz;
        if (!(gz < top && top - z < 0x800)) continue;
        int32_t lx, ly;
        toBoxLocal(&b.cx, b.angle, x, y, &lx, &ly);
        if (std::abs(lx - b.cx) < b.hx && std::abs(ly - b.cy) < b.hy) {
            gz = top;
            g.surface = 0;
            g.normal[0] = g.normal[1] = 0; g.normal[2] = 1;
        }
    }
    g.z = gz / 4096.f;
    return g;
}

void Collision::debugDraw(float xf, float yf, float range) {
    glDisable(GL_TEXTURE_2D);
    glDisable(GL_LIGHTING);
    glDisable(GL_CULL_FACE);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    int cx0, cy0, cx1, cy1;
    cellOf(fx(xf - range), fx(yf - range), &cx0, &cy0);
    cellOf(fx(xf + range), fx(yf + range), &cx1, &cy1);
    glBegin(GL_LINES);
    for (int cx = cx0; cx <= cx1; ++cx)
        for (int cy = cy0; cy <= cy1; ++cy) {
            const Cell* C = cell(cx, cy);
            if (!C) continue;
            glColor3f(1.f, 0.9f, 0.1f);
            for (const Box& b : C->boxes) {
                float c = cosf(b.angle * 9.587378e-05f), s = sinf(b.angle * 9.587378e-05f);
                float corner[4][2];
                const float sx[4] = {-1, 1, 1, -1}, sy[4] = {-1, -1, 1, 1};
                for (int k = 0; k < 4; ++k) {   // local -> world: rotate the local offset by +angle
                    float lx = sx[k] * b.hx / 4096.f, ly = sy[k] * b.hy / 4096.f;
                    corner[k][0] = b.cx / 4096.f + c * lx - s * ly;
                    corner[k][1] = b.cy / 4096.f + s * lx + c * ly;
                }
                float z0 = (b.cz - b.hz) / 4096.f, z1 = (b.cz + b.hz) / 4096.f;
                for (int k = 0; k < 4; ++k) {
                    int n = (k + 1) & 3;
                    glVertex3f(corner[k][0], corner[k][1], z0); glVertex3f(corner[n][0], corner[n][1], z0);
                    glVertex3f(corner[k][0], corner[k][1], z1); glVertex3f(corner[n][0], corner[n][1], z1);
                    glVertex3f(corner[k][0], corner[k][1], z0); glVertex3f(corner[k][0], corner[k][1], z1);
                }
            }
            glColor3f(0.1f, 0.9f, 1.f);
            for (const Cyl& c : C->cyls) {
                float x = c.x / 4096.f, y = c.y / 4096.f, r = c.r / 4096.f, z0 = c.z / 4096.f, z1 = (c.z + c.h) / 4096.f;
                for (int k = 0; k < 12; ++k) {
                    float a0 = k * 0.5236f, a1 = (k + 1) * 0.5236f;
                    glVertex3f(x + r * cosf(a0), y + r * sinf(a0), z0); glVertex3f(x + r * cosf(a1), y + r * sinf(a1), z0);
                    glVertex3f(x + r * cosf(a0), y + r * sinf(a0), z1); glVertex3f(x + r * cosf(a1), y + r * sinf(a1), z1);
                }
                glVertex3f(x + r, y, z0); glVertex3f(x + r, y, z1);
            }
            glColor3f(1.f, 0.2f, 1.f);
            for (const Mesh& m : C->meshes)
                for (const Tri& t : m.tris)
                    for (int e = 0; e < 3; ++e) {
                        const int32_t* a = &m.verts[t.v[e] * 3];
                        const int32_t* b = &m.verts[t.v[(e + 1) % 3] * 3];
                        glVertex3f(a[0] / 4096.f, a[1] / 4096.f, a[2] / 4096.f);
                        glVertex3f(b[0] / 4096.f, b[1] / 4096.f, b[2] / 4096.f);
                    }
        }
    glEnd();
    glDepthFunc(GL_LESS);
}

bool Collision::pushOut(float& xf, float& yf, float zf, float radius, float height) {
    bool hit = false;
    int32_t x = fx(xf), y = fx(yf), z = fx(zf), r = fx(radius), h = fx(height);
    int cx0, cy0, cx1, cy1;
    cellOf(x - r, y - r, &cx0, &cy0);
    cellOf(x + r, y + r, &cx1, &cy1);
    for (int cx = cx0; cx <= cx1; ++cx)
        for (int cy = cy0; cy <= cy1; ++cy) {
            const Cell* C = cell(cx, cy);
            if (!C) continue;
            for (const Box& b : C->boxes) {   // CCollision::CircleVBox
                if (!(z < b.cz + b.hz - 0xCC && b.cz - (b.hz + h) < z)) continue;
                int32_t lx, ly;
                toBoxLocal(&b.cx, b.angle, x, y, &lx, &ly);
                int32_t qx = std::clamp(lx, b.cx - b.hx, b.cx + b.hx), qy = std::clamp(ly, b.cy - b.hy, b.cy + b.hy);
                int64_t dx = lx - qx, dy = ly - qy, d2 = dx * dx + dy * dy;
                int64_t nlx, nly, depth;
                if (d2 == 0) {   // centre inside the box: leave through the nearest side
                    int64_t px = b.hx - std::abs(lx - b.cx), py = b.hy - std::abs(ly - b.cy);
                    if (px < py) { nlx = lx >= b.cx ? 4096 : -4096; nly = 0; depth = px + r; }
                    else { nlx = 0; nly = ly >= b.cy ? 4096 : -4096; depth = py + r; }
                } else if (d2 < (int64_t)r * r) {
                    double d = std::sqrt((double)d2);
                    nlx = (int64_t)(dx * 4096.0 / d); nly = (int64_t)(dy * 4096.0 / d);
                    depth = r - (int64_t)d;
                } else continue;
                int64_t nx = nlx, ny = nly;
                if (b.angle) {   // back to world orientation
                    int64_t c = fastsin(b.angle + 0x4000), s = fastsin(b.angle);
                    nx = (c * nlx - s * nly) >> 12;
                    ny = (s * nlx + c * nly) >> 12;
                }
                x += (int32_t)((nx * depth) >> 12);
                y += (int32_t)((ny * depth) >> 12);
                hit = true;
            }
            for (const Cyl& c : C->cyls) {
                if (!(z < c.z + c.h && c.z < z + h)) continue;
                int64_t dx = x - c.x, dy = y - c.y, rr = (int64_t)r + c.r, d2 = dx * dx + dy * dy;
                if (d2 >= rr * rr) continue;
                double d = std::sqrt((double)d2);
                if (d < 1) { dx = 4096; dy = 0; d = 4096; }
                x = c.x + (int32_t)(dx * rr / d);
                y = c.y + (int32_t)(dy * rr / d);
                hit = true;
            }
            for (const Mesh& m : C->meshes) {   // steep triangles act as walls (sphere at knee and head height)
                if (x + r < m.minX || y + r < m.minY || x - r > m.maxX || y - r > m.maxY) continue;
                for (const Tri& t : m.tris) {
                    if (std::abs(t.n[2]) > 0xB50) continue;   // walkable (< ~45 degrees) - handled by ground()
                    int64_t hn = (int64_t)t.n[0] * t.n[0] + (int64_t)t.n[1] * t.n[1];
                    if (!hn) continue;
                    for (int32_t sz : {z + r + 0x800, z + h - r}) {
                        int64_t dist = ((int64_t)t.n[0] * (x - t.p[0]) + (int64_t)t.n[1] * (y - t.p[1]) + (int64_t)t.n[2] * (sz - t.p[2])) >> 12;
                        if (std::abs(dist) >= r) continue;
                        // project onto the plane and check the edges
                        int64_t px = x - ((dist * t.n[0]) >> 12), py = y - ((dist * t.n[1]) >> 12), pz = sz - ((dist * t.n[2]) >> 12);
                        bool inside = true;
                        for (int e = 0; e < 3 && inside; ++e) {
                            const int32_t* v = &m.verts[t.v[e] * 3];
                            inside = (int64_t)t.en[e][0] * (px - v[0]) + (int64_t)t.en[e][1] * (py - v[1]) + (int64_t)t.en[e][2] * (pz - v[2]) < 1;
                        }
                        if (!inside) continue;
                        int64_t push = (dist >= 0 ? r - dist : -(r + dist));
                        double hl = std::sqrt((double)hn);
                        x += (int32_t)(t.n[0] * push / hl);
                        y += (int32_t)(t.n[1] * push / hl);
                        hit = true;
                        break;
                    }
                }
            }
        }
    xf = x / 4096.f;
    yf = y / 4096.f;
    return hit;
}
