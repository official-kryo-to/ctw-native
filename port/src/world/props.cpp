// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kryo.to
// See LICENSE in the repository root.
#include "props.h"
#include "gfx/assets.h"
#include "os/pak.h"
#include <glad/gl.h>
#include <algorithm>
#include <cmath>
#include <cstring>

static int32_t sinq(int16_t a) { return (int32_t)std::lround(std::sin(a * 3.14159265358979 / 32768.0) * 4096.0); }

void PropToWorld(const Collision::Prop& p, const int32_t l[3], int32_t out[3]) {
    const int64_t c = sinq((int16_t)(p.heading + 0x4000)), s = sinq(p.heading);
    out[0] = p.x + (int32_t)((c * l[0] - s * l[1]) >> 12);
    out[1] = p.y + (int32_t)((s * l[0] + c * l[1]) >> 12);
    out[2] = p.z + l[2];
}

bool PropLibrary::load() {
    std::vector<uint8_t> dir, d;
    uint16_t gd[28];
    if (!Assets_Pak().read(0, dir) || dir.size() < sizeof gd) return false;
    memcpy(gd, dir.data(), sizeof gd);
    if (!Assets_Pak().read(gd[16], d)) return false;
    size_t o = 0;
    auto u32 = [&](size_t at) { uint32_t v = 0; if (at + 4 <= d.size()) memcpy(&v, &d[at], 4); return v; };
    while (o + 8 <= d.size() && u32(o) != 0xDEADBEEFu) {
        Def def;
        memcpy(&def.model, &d[o], 2);
        memcpy(&def.broken, &d[o + 2], 2);
        def.flags = d[o + 6];
        const int n = d[o + 7];
        o += 8;
        for (int k = 0; k < n; ++k) {
            Shape s{u32(o), {}};
            static const int kSize[7] = {0, 0x1C, 0x18, 0x14, 0x10, 0x10, 0x10};
            if (s.type < 1 || s.type > 6 || o + kSize[s.type] > d.size()) return false;
            memcpy(s.v, &d[o + 4], std::min<size_t>(kSize[s.type] - 4, sizeof s.v));
            def.shapes.push_back(s);
            o += kSize[s.type];
        }
        defs_.push_back(std::move(def));
    }
    d.clear();
    if (Assets_Pak().read(gd[19], d))
        for (size_t at = 0; at + 28 <= d.size(); at += 28) {
            Light l;
            memcpy(&l.prop, &d[at], 2);
            if (l.prop == 0xBEEF || l.prop >= defs_.size()) break;
            l.type = d[at + 2];
            memcpy(l.offset, &d[at + 4], 12);
            memcpy(&l.size, &d[at + 16], 4);
            uint32_t colour;
            memcpy(&colour, &d[at + 20], 4);
            l.colour = (uint16_t)colour;
            lights_.push_back(l);
        }
    d.clear();
    if (Assets_Pak().read(gd[17], d))
        for (size_t at = 0; at + 16 <= d.size(); at += 16) {
            uint16_t end;
            memcpy(&end, &d[at + 14], 2);
            if (end != 0xDEAD) break;
            int16_t smash, uproot, mass;
            memcpy(&mass, &d[at + 8], 2);
            memcpy(&smash, &d[at + 10], 2);
            memcpy(&uproot, &d[at + 12], 2);
            kinds_.push_back({d[at], d[at + 2], smash / 4096.f, uproot / 4096.f, d[at + 1], mass, d[at + 7] != 0});
        }
    return !defs_.empty();
}

bool PropLibrary::physics(int prop, Physics& out) const {
    out = {};
    if (prop < 0 || prop >= (int)defs_.size() || defs_[prop].shapes.empty()) return false;
    const Shape& s = defs_[prop].shapes[0];
    if (s.type < 1 || s.type > 3) return false;  // mesh-only definitions have no dynamic primitive
    for (int i = 0; i < 3; ++i) out.centre[i] = out.cg[i] = s.v[i];
    if (s.type == 1) {
        for (int i = 0; i < 3; ++i) out.half[i] = s.v[i + 3] >> 1;
        out.centre[2] += s.v[5] / 2;
        out.cg[2] = out.centre[2];
    } else if (s.type == 2) {
        out.half[0] = out.half[1] = s.v[3];
        out.half[2] = s.v[4] >> 1;
        out.centre[2] += s.v[4] / 2;
        out.cg[2] = out.centre[2];
    } else {
        // The original passes the sphere radius as each full box dimension.
        for (int i = 0; i < 3; ++i) out.half[i] = s.v[3] >> 1;
    }
    return true;
}

const Model* PropLibrary::brokenModel(int prop) {
    if (prop < 0 || prop >= (int)defs_.size() || defs_[prop].broken >= 0xFFFE) return nullptr;
    const int id = defs_[prop].broken;
    auto it = models_.find(id);
    if (it != models_.end()) return it->second.get();
    auto m = std::make_unique<Model>();
    std::vector<uint8_t> raw;
    if (!Assets_Pak().read(id, raw) || !m->parse(raw)) m.reset();
    return (models_[id] = std::move(m)).get();
}

const Model* PropLibrary::smashedModel(int prop) {
    if (prop < 0 || prop >= (int)defs_.size()) return nullptr;
    return defs_[prop].broken == 0xFFFF ? model(prop) : brokenModel(prop);
}

void PropLibrary::footprint(int prop, float& radius, float& height) const {
    radius = height = 0.f;
    if (prop < 0 || prop >= (int)defs_.size()) return;
    int32_t meshOffset[3]{};
    for (const Shape& s : defs_[prop].shapes) {
        if (s.type == 4) { std::copy(s.v, s.v + 3, meshOffset); continue; }
        if (s.type == 6) continue;
        if (s.type == 5) {
            radius = std::max(radius, std::hypot((s.v[0] + meshOffset[0]) / 4096.f, (s.v[1] + meshOffset[1]) / 4096.f));
            height = std::max(height, (s.v[2] + meshOffset[2]) / 4096.f);
            continue;
        }
        const float off = std::sqrt((float)s.v[0] * s.v[0] + (float)s.v[1] * s.v[1]) / 4096.f;
        float r, h;
        if (s.type == 1) { r = std::sqrt((float)s.v[3] * s.v[3] + (float)s.v[4] * s.v[4]) / 8192.f; h = s.v[5] / 4096.f; }
        else if (s.type == 2) { r = s.v[3] / 4096.f; h = s.v[4] / 4096.f; }
        else { r = s.v[3] / 4096.f; h = r; }   // sphere offset is its centre
        radius = std::max(radius, off + r);
        height = std::max(height, s.v[2] / 4096.f + h);
    }
}

const Model* PropLibrary::model(int prop) {
    if (prop < 0 || prop >= (int)defs_.size()) return nullptr;
    const int id = defs_[prop].model;
    auto it = models_.find(id);
    if (it != models_.end()) return it->second.get();
    auto m = std::make_unique<Model>();
    std::vector<uint8_t> raw;
    if (!Assets_Pak().read(id, raw) || !m->parse(raw)) m.reset();
    return (models_[id] = std::move(m)).get();
}

float PropLibrary::radius(int prop) {
    const Model* m = model(prop);
    if (!m) return 1.f;
    float r = 0;
    for (int i = 0; i < 3; ++i) r = std::max({r, std::fabs(m->bboxMin[i]), std::fabs(m->bboxMax[i])});
    return r;
}

void PropLibrary::shapes(const Collision::Prop& p, std::vector<Collision::Box>& boxes, std::vector<Collision::Cyl>& cyls,
                         std::vector<Collision::Mesh>& meshes) const {
    if (p.prop >= defs_.size()) return;
    Collision::Mesh mesh{};
    int32_t offset[3] = {0, 0, 0};
    for (const Shape& s : defs_[p.prop].shapes)
        if (s.type == 4) { std::copy(s.v, s.v + 3, offset); break; }
    for (const Shape& s : defs_[p.prop].shapes) {
        if (s.type >= 4) {
            if (s.type == 5) {
                int32_t local[3], at[3];
                for (int k = 0; k < 3; ++k) local[k] = s.v[k] + offset[k];
                PropToWorld(p, local, at);
                mesh.verts.insert(mesh.verts.end(), at, at + 3);
            }
            continue;
        }
        int32_t at[3];
        PropToWorld(p, s.v, at);
        if (s.type == 1) {   // box: size from the bottom centre
            boxes.push_back({at[0], at[1], at[2] + s.v[5] / 2, s.v[3] / 2, s.v[4] / 2, s.v[5] / 2, p.heading, 0});
        } else if (s.type == 2) {
            cyls.push_back({at[0], at[1], at[2], s.v[3], s.v[4], 0});
        } else {             // sphere: a cylinder as tall as it is wide
            cyls.push_back({at[0], at[1], at[2] - s.v[3], s.v[3], s.v[3] * 2, 0});
        }
    }
    for (const Shape& s : defs_[p.prop].shapes) {
        if (s.type != 6) continue;
        Collision::Tri t{};
        bool valid = true;
        for (int k = 0; k < 3; ++k) {
            // propCollisionPrimMeshTriangle indices are one-based in the resource.
            valid &= s.v[k] >= 1 && s.v[k] <= 256 && (size_t)s.v[k] <= mesh.verts.size() / 3;
            t.v[k] = (uint8_t)(s.v[k] - 1);
        }
        if (valid) { Collision::finishTriangle(t, mesh.verts); mesh.tris.push_back(t); }
    }
    if (!mesh.tris.empty()) {
        mesh.minX = mesh.maxX = mesh.verts[0]; mesh.minY = mesh.maxY = mesh.verts[1];
        for (size_t i = 0; i < mesh.verts.size(); i += 3) {
            mesh.minX = std::min(mesh.minX, mesh.verts[i]); mesh.maxX = std::max(mesh.maxX, mesh.verts[i]);
            mesh.minY = std::min(mesh.minY, mesh.verts[i + 1]); mesh.maxY = std::max(mesh.maxY, mesh.verts[i + 1]);
        }
        meshes.push_back(std::move(mesh));
    }
}

void PropLibrary::lights(const Collision::Prop& p, std::vector<WorldLight>& out) const {
    for (const Light& l : lights_) {
        if (l.prop != p.prop) continue;
        int32_t at[3];
        PropToWorld(p, l.offset, at);
        const int s = (int)std::min<int64_t>(((int64_t)l.size * 0xCCC) >> 12, 0x1000);   // cLight::Initalise
        out.push_back({at[0], at[1], at[2], (int16_t)s, l.colour, (uint16_t)l.type});
    }
}

void PropLibrary::draw(const Collision::Prop& p) {
    const Model* m = model(p.prop);
    if (!m) return;
    const float a = p.heading * 3.14159265f / 32768.f, c = std::cos(a), s = std::sin(a);
    const float M[16] = {c, s, 0, 0, -s, c, 0, 0, 0, 0, 1, 0, p.x / 4096.f, p.y / 4096.f, p.z / 4096.f, 1};
    drawModel(*m, M);
}

void PropLibrary::drawModel(const Model& model, const float M[16]) {
    const Model* m = &model;
    if (m->verts.empty()) return;
    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glMultMatrixf(M);
    const float sc = m->scale;
    for (int pass = 0; pass < 2; ++pass)
        for (const ModelBatch& b : m->batches) {
            if (((b.flags & 0x08) != 0) != (pass == 1)) continue;
            if (b.flags & 0x10) glDisable(GL_CULL_FACE); else glEnable(GL_CULL_FACE);
            if (b.flags & 0x07) glEnable(GL_LIGHTING); else glDisable(GL_LIGHTING);
            glDepthMask((b.flags & 0x20) ? GL_FALSE : GL_TRUE);
            if (pass == 1) { glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA); glEnable(GL_ALPHA_TEST); }
            else { glDisable(GL_BLEND); glDisable(GL_ALPHA_TEST); }
            const GLuint tex = Assets_Texture(b.texture);
            if (tex) { glEnable(GL_TEXTURE_2D); glBindTexture(GL_TEXTURE_2D, tex); } else glDisable(GL_TEXTURE_2D);
            glColor4f(1, 1, 1, (float)((b.alpha + b.alpha * 31) >> 8) / 31.f);
            const NodeMatrix& nm = m->world[b.node < m->world.size() ? b.node : 0];
            glBegin(GL_TRIANGLES);
            for (uint32_t i = 2; i < b.count; ++i) {   // stitched triangle strip, degenerate triangles skipped
                const ModelVertex* v[3] = {&m->verts[b.firstVertex + i - 2], &m->verts[b.firstVertex + i - 1], &m->verts[b.firstVertex + i]};
                if (i & 1) std::swap(v[1], v[2]);
                auto same = [](const ModelVertex* x, const ModelVertex* y) { return x->x == y->x && x->y == y->y && x->z == y->z; };
                if (same(v[0], v[1]) || same(v[1], v[2]) || same(v[0], v[2])) continue;
                for (int k = 0; k < 3; ++k) {
                    const float q[3] = {v[k]->x * sc, v[k]->y * sc, v[k]->z * sc};
                    const float n0[3] = {v[k]->nx / 32767.f, v[k]->ny / 32767.f, v[k]->nz / 32767.f};
                    float pp[3], nn[3];
                    for (int r = 0; r < 3; ++r) {
                        pp[r] = nm.r[r][0] * q[0] + nm.r[r][1] * q[1] + nm.r[r][2] * q[2] + nm.t[r];
                        nn[r] = nm.r[r][0] * n0[0] + nm.r[r][1] * n0[1] + nm.r[r][2] * n0[2];
                    }
                    glNormal3fv(nn);
                    glTexCoord2f(v[k]->u / 2048.f, v[k]->v / 2048.f);   // the game shader: UV * 1/2048
                    glVertex3fv(pp);
                }
            }
            glEnd();
        }
    glPopMatrix();
}
