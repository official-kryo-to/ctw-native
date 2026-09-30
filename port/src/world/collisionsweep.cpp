// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kryo.to
// See LICENSE in the repository root.
// The CCollision swept-sphere / overlap routines peds and cameras use, ported from the game's fixed-point code
// (20.12 positions, Q12 normals). Quirks of the original are kept on purpose (noted where they matter).
#include "collision.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>

static int fastsin(int a) { return (int)(sinf((float)a * 9.587378e-05f) * 4096.f); }
static inline int32_t mulq(int64_t a, int64_t b) { return (int32_t)((a * b) >> 12); }
static inline int64_t isqrt(int64_t v) { return v <= 0 ? 0 : (int64_t)std::sqrt((double)v); }

static void cellOf(int32_t x, int32_t y, int* cx, int* cy) {
    *cx = (int)std::floor((x + 0xDAC000) / (double)0x32000);
    *cy = (int)std::floor((y + 0x9C4000) / (double)0x32000);
}

// Normalise(tv3d): float 1/sqrt, rounded to Q12 (a zero vector stays zero)
static void normalise(int32_t v[3]) {
    double l2 = (double)v[0] * v[0] + (double)v[1] * v[1] + (double)v[2] * v[2];
    if (l2 <= 0) return;
    float inv = 1.f / std::sqrt((float)l2);
    for (int i = 0; i < 3; ++i) {
        float f = inv * (float)v[i];
        v[i] = (int32_t)((f >= 0 ? 0.5f : -0.5f) + f * 4096.f);
    }
}

// ------------------------------------------------------------------------------------------------ candidates
void Collision::candidates(const int32_t p[3], int32_t R, bool groundSlab, Candidates& out) {
    out.boxes.clear(); out.cyls.clear(); out.tris.clear();
    int cx, cy;
    cellOf(p[0], p[1], &cx, &cy);
    const Cell* C = cell(cx, cy);
    if (C) {
        for (const Box& b : C->boxes) {   // GenerateBoxCandidateList (63 + the slab)
            if (out.boxes.size() >= 63) break;
            int32_t dx = p[0] - b.cx, dy = p[1] - b.cy;
            if (b.angle == 0) {
                if (std::abs(dx) <= b.hx + R && std::abs(dy) <= b.hy + R) out.boxes.push_back(&b);
            } else {
                int64_t rr = (int64_t)b.hx + R + b.hy;
                if ((int64_t)dx * dx + (int64_t)dy * dy < rr * rr) out.boxes.push_back(&b);
            }
        }
        for (const Cyl& c : C->cyls) {    // GenerateCylinderCandidateList
            if (out.cyls.size() >= 32) break;
            int64_t dx = p[0] - c.x, dy = p[1] - c.y, rr = (int64_t)c.r + R;
            if (dx * dx + dy * dy < rr * rr) out.cyls.push_back(&c);
        }
        for (const Mesh& m : C->meshes) { // GenerateMeshCandidateList
            if (!(m.minX <= p[0] + R && p[0] - R <= m.maxX && m.minY <= p[1] + R && p[1] - R <= m.maxY)) continue;
            for (const Tri& t : m.tris) {
                if (out.tris.size() >= 64) break;
                if (t.v[0] * 3 + 2 >= (int)m.verts.size() || t.v[1] * 3 + 2 >= (int)m.verts.size() || t.v[2] * 3 + 2 >= (int)m.verts.size()) continue;
                int64_t dx = t.p[0] - p[0], dy = t.p[1] - p[1], dz = t.p[2] - p[2], rr = (int64_t)R + t.radius * 0x1000;
                if (dx * dx + dy * dy + dz * dz < rr * rr) out.tris.push_back({&t, m.verts.data()});
            }
        }
    }
    if (groundSlab) {   // CCollision::mBox
        Ground g = ground(p[0] / 4096.f, p[1] / 4096.f, p[2] / 4096.f);
        out.slab = {p[0], p[1], (int32_t)lroundf(g.z * 4096.f) - 0x800, 0x64000, 0x64000, 0x800, 0, 0x20};
        out.boxes.push_back(&out.slab);
    }
}

// ------------------------------------------------------------------------------------------------ primitives
// CCollision::RayVAABB: segment p0 -> p1 against an AABB {min xyz, max xyz}; t = entry fraction (Q12).
static bool rayVAABB(const int32_t* p0, const int32_t* p1, const int32_t* bb, int32_t* q, int32_t& t) {
    t = 0;
    int32_t d[3] = {p1[0] - p0[0], p1[1] - p0[1], p1[2] - p0[2]};
    int32_t tmin = 0, tmax = 0x7fffffff;
    for (int a = 0; a < 3; ++a) {
        if (std::abs(d[a]) < 8) {
            if (p0[a] < bb[a] || bb[a + 3] < p0[a]) return false;
            continue;
        }
        int64_t inv = (0x100000000000LL / d[a]) >> 20;
        int32_t t1 = (int32_t)((inv * (int64_t)(bb[a + 3] - p0[a])) >> 12);
        int32_t t2 = (int32_t)((inv * (int64_t)(bb[a] - p0[a])) >> 12);
        int32_t lo = std::min(t1, t2), hi = std::max(t1, t2);
        if (a == 0) {
            tmin = std::max(lo, 0);
            tmax = hi;
            t = tmin;
            if (tmax < 0) return false;
        } else {
            tmin = std::max(lo, tmin);
            tmax = std::min(hi, tmax);
            t = tmin;
            if (tmax < tmin) return false;
        }
    }
    if ((uint32_t)tmin > 0x1000) return false;
    for (int a = 0; a < 3; ++a) q[a] = p0[a] + (int32_t)(((int64_t)tmin * d[a]) >> 12);
    t = tmin;
    return true;
}

// CCollision::SegVSphere: note the game treats the (unnormalised) move as unit length here.
static bool segVSphere(const int32_t* p0, const int32_t* p1, const int32_t* c, int32_t r, int32_t* out, int32_t& t) {
    int32_t D[3] = {p1[0] - p0[0], p1[1] - p0[1], p1[2] - p0[2]};
    int32_t P[3] = {p0[0] - c[0], p0[1] - c[1], p0[2] - c[2]};
    int32_t cc = (int32_t)((((int64_t)P[0] * P[0] + (int64_t)P[1] * P[1] + (int64_t)P[2] * P[2]) - (int64_t)r * r) >> 12);
    int32_t b = (int32_t)(((int64_t)P[0] * D[0] + (int64_t)P[1] * D[1] + (int64_t)P[2] * D[2]) >> 12);
    if (!(cc < 1 || b < 1)) return false;
    int64_t disc = (int64_t)b * b - ((int64_t)cc << 12);
    if (disc < 0) return false;
    int32_t tt = (int32_t)(-isqrt(disc) - b);
    if (tt < 0) tt = 0;
    t = tt;
    if ((uint32_t)tt >= 0x1001) return false;
    out[0] = c[0]; out[1] = c[1]; out[2] = c[2];
    int64_t dot = 0;
    for (int i = 0; i < 3; ++i) dot += (int64_t)D[i] * ((p0[i] + (int32_t)(((int64_t)tt * D[i]) >> 12)) - c[i]);
    return dot < 0;
}

// CCollision::SweptVertVSphere (same unit-length quirk)
static bool sweptVertVSphere(const int32_t* p0, const int32_t* p1, const int32_t* c, int32_t r, int32_t* out, int32_t& t) {
    int32_t D[3] = {p1[0] - p0[0], p1[1] - p0[1], p1[2] - p0[2]};
    int32_t P[3] = {p0[0] - c[0], p0[1] - c[1], p0[2] - c[2]};
    int64_t cc = ((int64_t)P[0] * P[0] + (int64_t)P[1] * P[1] + (int64_t)P[2] * P[2]) - (int64_t)r * r;
    int64_t b = (int64_t)P[0] * D[0] + (int64_t)P[1] * D[1] + (int64_t)P[2] * D[2];
    if (!(cc < 1 || b < 1)) return false;
    int64_t disc = ((b * b) >> 24) - cc;
    if (disc < 0) return false;
    int32_t tt = (int32_t)((isqrt(disc) * -4096 - b) >> 12);
    if (tt < 0) tt = 0;
    t = tt;
    if ((uint32_t)tt >= 0x1001) return false;
    int64_t dot = 0;
    for (int i = 0; i < 3; ++i) {
        out[i] = p0[i] + (int32_t)(((int64_t)tt * D[i]) >> 12);
        dot += (int64_t)D[i] * (out[i] - c[i]);
    }
    return dot < 0;
}

// CCollision::SegV{X,Y,Z}Capsule: the move against a capsule along `axis` from A to B, tested in the plane
// perpendicular to the axis. (The height along the axis at t uses t * d / len like the original.)
static bool segVAxisCapsule(int axis, const int32_t* p0, const int32_t* p1, int32_t r, const int32_t* A, const int32_t* B,
                            int32_t* out, int32_t& t) {
    int u = axis == 2 ? 0 : (axis == 1 ? 0 : 1), v = axis == 2 ? 1 : 2;   // the two other axes
    int32_t du = p1[u] - p0[u], dv = p1[v] - p0[v];
    int64_t L = isqrt((int64_t)du * du + (int64_t)dv * dv);
    if (L == 0) return false;
    int32_t nu = (int32_t)((((int64_t)du << 32) / L) >> 20), nv = (int32_t)((((int64_t)dv << 32) / L) >> 20);
    int32_t Pu = p0[u] - A[u], Pv = p0[v] - A[v];
    int32_t proj = (int32_t)(((int64_t)nu * Pu + (int64_t)nv * Pv) >> 12);
    if (proj >= 1) return false;
    int64_t cc = ((int64_t)Pu * Pu + (int64_t)Pv * Pv - (int64_t)r * r) & ~0xFFFLL;
    int64_t disc = (int64_t)proj * proj - cc;
    if (disc < 0) return false;
    int32_t dist = (int32_t)(-isqrt(disc & ~0xFFFLL) - proj);
    t = dist;
    if (dist > L) return false;
    int32_t tt = (int32_t)((((int64_t)std::max(dist, 0)) << 32) / L >> 20);
    t = tt;
    int32_t h = p0[axis] + (int32_t)(((int64_t)tt * (p1[axis] - p0[axis])) / L);
    if ((int64_t)(B[axis] - A[axis]) * (h - A[axis]) < 0) return segVSphere(p0, p1, A, r, out, t);
    if ((int64_t)(A[axis] - B[axis]) * (h - B[axis]) >= 0) {
        out[0] = A[0]; out[1] = A[1]; out[2] = A[2];
        out[axis] = h;
        return true;
    }
    return segVSphere(p0, p1, B, r, out, t);
}

// CCollision::SweptSphereVAABB: expand the box by r, ray-test, then classify the hit region (face / edge /
// corner). In the corner case the game builds degenerate capsules (both ends the same corner), so each of the
// three tests is effectively an infinite cylinder through the corner - kept as is.
static bool sweptSphereVAABB(const int32_t* p0, const int32_t* p1, int32_t r, const int32_t* bb, int32_t* out, int32_t& t) {
    int32_t e[6] = {bb[0] - r, bb[1] - r, bb[2] - r, bb[3] + r, bb[4] + r, bb[5] + r};
    if (!(std::min(p0[0], p1[0]) <= e[3] && std::min(p0[1], p1[1]) <= e[4])) return false;
    if (!(e[0] <= std::max(p0[0], p1[0]) && e[1] <= std::max(p0[1], p1[1]))) return false;
    int32_t q[3];
    if (!rayVAABB(p0, p1, e, q, t) || t > 0x1000) return false;
    out[0] = q[0]; out[1] = q[1]; out[2] = q[2];
    int u = (q[0] < bb[0] ? 1 : 0) | (q[1] < bb[1] ? 2 : 0) | (q[2] < bb[2] ? 4 : 0);
    int v = (bb[3] < q[0] ? 1 : 0) | (bb[4] < q[1] ? 2 : 0) | (bb[5] < q[2] ? 4 : 0);
    int m = u + v;
    auto corner = [&](int maxMask, int32_t* c) {
        c[0] = maxMask & 1 ? bb[3] : bb[0];
        c[1] = maxMask & 2 ? bb[4] : bb[1];
        c[2] = maxMask & 4 ? bb[5] : bb[2];
    };
    if (m == 7) {
        int32_t c[3];
        corner(v, c);
        const int32_t kNone = 0x2710000;
        int32_t best = kNone, tt;
        if (segVAxisCapsule(2, p0, p1, r, c, c, out, tt)) best = std::min(tt, kNone);
        if (segVAxisCapsule(1, p0, p1, r, c, c, out, tt) && tt < best) best = tt;
        if (segVAxisCapsule(0, p0, p1, r, c, c, out, tt) && tt < best) best = tt;
        if (best == kNone) return false;
        t = best;
        return true;
    }
    if (m & (m - 1)) {   // edge
        int32_t A[3], B[3];
        corner(u ^ 7, A);
        corner(v, B);
        int axis = A[0] != B[0] ? 0 : (A[1] != B[1] ? 1 : 2);
        return segVAxisCapsule(axis, p0, p1, r, A, B, out, t);
    }
    // face (or inside: m == 0, no normal -> no hit)
    int32_t n[3] = {0, 0, 0};
    for (int a = 0; a < 3; ++a)
        if (m == (1 << a)) n[a] = u == 0 ? 0x1000 : -0x1000;
    int64_t dot = (int64_t)(p1[1] - p0[1]) * n[1] + (int64_t)(p1[0] - p0[0]) * n[0] + (int64_t)(p1[2] - p0[2]) * n[2];
    if (dot >= 0) return false;
    for (int a = 0; a < 3; ++a) out[a] = q[a] + mulq(n[a], -r);
    return true;
}

// ------------------------------------------------------------------------------------------------ shapes
bool Collision::sweptSphereVBox(const int32_t a[3], const int32_t b[3], int32_t r, const Box& box, int32_t hit[3], int32_t& t) {
    // CCollision::SweptSphereVBoxC: into the box frame (rotate about the centre by -angle), test, rotate back
    int32_t bb[6] = {box.cx - box.hx, box.cy - box.hy, box.cz - box.hz, box.cx + box.hx, box.cy + box.hy, box.cz + box.hz};
    int32_t la[3] = {a[0], a[1], a[2]}, lb[3] = {b[0], b[1], b[2]};
    int64_t s = 0, c = 0;
    if (box.angle) {
        s = fastsin(box.angle);
        c = fastsin(box.angle + 0x4000);
        auto toLocal = [&](const int32_t* p, int32_t* o) {
            int64_t dx = p[0] - box.cx, dy = p[1] - box.cy;
            o[0] = (int32_t)(((int64_t)box.cx * 0x1000 + dx * c + dy * s) >> 12);
            o[1] = (int32_t)(((int64_t)box.cy * 0x1000 - dx * s + dy * c) >> 12);
        };
        toLocal(a, la);
        toLocal(b, lb);
    }
    if (!sweptSphereVAABB(la, lb, r, bb, hit, t)) return false;
    if (box.angle) {
        int64_t dx = hit[0] - box.cx, dy = hit[1] - box.cy;
        hit[0] = (int32_t)(((int64_t)box.cx * 0x1000 + c * dx - s * dy) >> 12);
        hit[1] = box.cy + (int32_t)((s * dx + c * dy) >> 12);
    }
    return true;
}

// CCollision::SweptVertVCylinder (the cylinder has no bottom; above the top it is capped by a sphere)
static bool sweptVertVCylinder(const int32_t* p0, const int32_t* p1, const Collision::Cyl& cy, int32_t* out, int32_t* n, int32_t& t) {
    int32_t dy0 = p0[1] - cy.y, dx0 = p0[0] - cy.x, R = cy.r;
    int32_t dx = p1[0] - p0[0], dy = p1[1] - p0[1], dz = p1[2] - p0[2];
    int64_t d2 = (int64_t)dy0 * dy0 + (int64_t)dx0 * dx0;
    if (d2 < (int64_t)R * R) {
        int32_t z0 = p0[2] - cy.z;
        if (z0 < cy.h + cy.z) {
            if ((int64_t)dx0 * dx + (int64_t)dy0 * dy < 0) {
                t = 0;
                out[0] = p0[0]; out[1] = p0[1]; out[2] = p0[2];
                n[0] = dx0; n[1] = dy0; n[2] = 0;
                normalise(n);
                return true;
            }
        } else {
            int32_t zt = z0 - cy.h;
            if ((int64_t)R * R > d2 + (int64_t)zt * zt && (int64_t)dx0 * dx + (int64_t)dy0 * dy + (int64_t)zt * dz < 0) {
                t = 0;
                out[0] = p0[0]; out[1] = p0[1]; out[2] = p0[2];
                n[0] = dx0; n[1] = dy0; n[2] = zt;
                normalise(n);
                return true;
            }
        }
    }
    int64_t a = (int64_t)dy * dy + (int64_t)dx * dx;
    if ((a & 0xffffffff000LL) == 0) return false;
    int32_t a12 = (int32_t)((a * 0x100000) >> 32);
    int32_t b = (int32_t)(((int64_t)dx * dx0 + (int64_t)dy * dy0) >> 12);
    int32_t cc = (int32_t)((((int64_t)dx0 * dx0 + (int64_t)(dy0 - R) * (dy0 + R)) * 0x100000) >> 32);
    int64_t disc = (int64_t)b * b - (int64_t)a12 * cc;
    if (disc < 0 || a12 == 0) return false;
    int32_t tt = (int32_t)((((int64_t)(int32_t)(-(int32_t)isqrt(disc) - b)) << 32) / a12 >> 20);
    t = tt;
    if ((uint32_t)tt > 0x1000) return false;
    out[0] = p0[0] + (int32_t)(((int64_t)tt * dx) >> 12);
    out[1] = p0[1] + (int32_t)(((int64_t)tt * dy) >> 12);
    out[2] = p0[2] + (int32_t)(((int64_t)tt * dz) >> 12);
    int32_t top = cy.z + cy.h;
    if (top < out[2]) {
        int32_t c[3] = {cy.x, cy.y, top};
        if (!sweptVertVSphere(p0, p1, c, R, out, t)) return false;
        n[0] = out[0] - c[0]; n[1] = out[1] - c[1]; n[2] = out[2] - top;
        normalise(n);
        return true;
    }
    n[0] = out[0] - cy.x; n[1] = out[1] - cy.y; n[2] = 0;
    normalise(n);
    return true;
}

bool Collision::sweptSphereVCylinder(const int32_t a[3], const int32_t b[3], int32_t r, const Cyl& c, int32_t hit[3], int32_t n[3], int32_t& t) {
    Cyl big = c;
    big.r = c.r + r;
    if (!sweptVertVCylinder(a, b, big, hit, n, t)) return false;
    for (int i = 0; i < 3; ++i) hit[i] -= mulq(n[i], r);   // back onto the cylinder surface
    return true;
}

// CCollision::SweptSphereVTri2: the sphere against the edge point nearest to where it met the plane.
static bool sweptSphereVTri2(const int32_t* p0, int32_t r, const int32_t* P, const int32_t* Din, const int32_t* A, const int32_t* B,
                             int32_t* out, int32_t* n, int32_t& t) {
    int32_t Q[3];
    if ((int64_t)(B[1] - A[1]) * (P[1] - A[1]) + (int64_t)(B[0] - A[0]) * (P[0] - A[0]) + (int64_t)(B[2] - A[2]) * (P[2] - A[2]) < 0) {
        Q[0] = A[0]; Q[1] = A[1]; Q[2] = A[2];
    } else if ((int64_t)(P[1] - B[1]) * (A[1] - B[1]) + (int64_t)(P[0] - B[0]) * (A[0] - B[0]) + (int64_t)(P[2] - B[2]) * (A[2] - B[2]) < 0) {
        Q[0] = B[0]; Q[1] = B[1]; Q[2] = B[2];
    } else {
        int32_t e[3] = {B[0] - A[0], B[1] - A[1], B[2] - A[2]};
        normalise(e);
        int32_t k = (int32_t)((((int64_t)e[0] * (P[0] - A[0]) + (int64_t)e[1] * (P[1] - A[1]) + (int64_t)e[2] * (P[2] - A[2])) * 0x100000) >> 32);
        for (int i = 0; i < 3; ++i) Q[i] = A[i] + mulq(k, e[i]);
    }
    out[0] = Q[0]; out[1] = Q[1]; out[2] = Q[2];
    int32_t W[3] = {Q[0] - p0[0], Q[1] - p0[1], Q[2] - p0[2]};
    int32_t D[3] = {Din[0], Din[1], Din[2]};
    int64_t L = isqrt((int64_t)D[0] * D[0] + (int64_t)D[1] * D[1] + (int64_t)D[2] * D[2]);
    int32_t proj = 0;
    if (L >= 1) {
        for (int i = 0; i < 3; ++i) D[i] = (int32_t)((((int64_t)D[i] << 32) / L) >> 20);
        proj = (int32_t)(((int64_t)D[0] * W[0] + (int64_t)D[1] * W[1] + (int64_t)D[2] * W[2]) >> 12);
    }
    int64_t r2 = (int64_t)r * r;
    int64_t cc = ((int64_t)W[0] * W[0] + (int64_t)W[1] * W[1] + (int64_t)W[2] * W[2] - (int64_t)proj * proj) & ~0xFFFLL;
    int64_t disc = r2 - cc;
    if (disc == 0 || r2 < cc) return false;
    int32_t tt = 0;
    if (L >= 1) {
        tt = (int32_t)((((int64_t)(int32_t)(proj - (int32_t)isqrt(disc))) << 32) / L >> 20);
        if ((uint32_t)tt > 0x1000) return false;
    }
    t = tt;
    int64_t m = ((int64_t)tt * L) >> 12;
    for (int i = 0; i < 3; ++i) n[i] = p0[i] + (int32_t)((m * D[i]) >> 12) - Q[i];
    normalise(n);
    return true;
}

bool Collision::sweptSphereVTri(const int32_t a[3], const int32_t b[3], int32_t r, const TriRef& tr, int32_t hit[3], int32_t n[3], int32_t& t) {
    const Tri& T = *tr.tri;
    int32_t off[3] = {mulq(r, T.n[0]), mulq(r, T.n[1]), mulq(r, T.n[2])};
    int32_t s[3] = {a[0] - off[0], a[1] - off[1], a[2] - off[2]};   // the sphere's point nearest the plane
    int32_t e[3] = {b[0] - off[0], b[1] - off[1], b[2] - off[2]};
    int32_t de = (int32_t)(((int64_t)T.n[0] * e[0] + (int64_t)T.n[1] * e[1] + (int64_t)T.n[2] * e[2]) >> 12);
    int32_t ds = (int32_t)(((int64_t)T.n[0] * s[0] + (int64_t)T.n[1] * s[1] + (int64_t)T.n[2] * s[2]) >> 12);
    if (!(de < ds)) return false;   // only when moving towards the front face
    const int32_t* V[4] = {tr.verts + T.v[0] * 3, tr.verts + T.v[1] * 3, tr.verts + T.v[2] * 3, tr.verts + T.v[0] * 3};
    int32_t dv = (int32_t)(((int64_t)T.n[0] * V[0][0] + (int64_t)T.n[1] * V[0][1] + (int64_t)V[0][2] * T.n[2]) >> 12);
    int32_t da = ds - dv, db = de - dv;
    if ((da | db) >= 0) return false;
    if (!(-r <= da || -r <= db)) return false;
    da = std::max(da, 0);
    int64_t den = (int32_t)(da - db);
    int64_t inv = den ? (0x100000000000LL / den) >> 20 : 0;
    for (int i = 0; i < 3; ++i)
        s[i] += (int32_t)((inv * ((((int64_t)(e[i] - s[i]) * da) * 0x100000) >> 32)) >> 12);
    int edge = -1;
    for (int k = 0; k < 3 && edge < 0; ++k)
        if ((int64_t)T.en[k][0] * (s[0] - V[k][0]) + (int64_t)T.en[k][1] * (s[1] - V[k][1]) + (int64_t)T.en[k][2] * (s[2] - V[k][2]) >= 1)
            edge = k;
    if (edge < 0) {
        t = (int32_t)((den ? (((int64_t)da << 32) / den) : 0) >> 20);
        hit[0] = s[0]; hit[1] = s[1]; hit[2] = s[2];
        n[0] = T.n[0]; n[1] = T.n[1]; n[2] = T.n[2];
        return true;
    }
    int32_t D[3] = {b[0] - a[0], b[1] - a[1], b[2] - a[2]};
    return sweptSphereVTri2(a, r, s, D, V[edge], V[edge + 1], hit, n, t);
}

bool Collision::sphereVBox(const int32_t c[3], int32_t r, const Box& box, int32_t contact[3], int32_t n[3], int32_t& depth) {
    // CCollision::SphereVBox -> SphereIntersectBox with the box matrix RotZ(-angle)
    int64_t s = 0, co = 0x1000;
    if (box.angle) { s = fastsin(box.angle); co = fastsin(box.angle + 0x4000); }
    int32_t d[3] = {c[0] - box.cx, c[1] - box.cy, c[2] - box.cz};
    int32_t lx = (int32_t)((co * d[0] + s * d[1]) >> 12), ly = (int32_t)((-s * d[0] + co * d[1]) >> 12), lz = d[2];
    lx = std::clamp(lx, -box.hx, box.hx);
    ly = std::clamp(ly, -box.hy, box.hy);
    lz = std::clamp(lz, -box.hz, box.hz);
    int32_t wx = (int32_t)((co * lx) >> 12) + (int32_t)((-s * ly) >> 12);
    int32_t wy = (int32_t)((s * lx) >> 12) + (int32_t)((co * ly) >> 12);
    int32_t wz = lz;
    int32_t D[3] = {d[0] - wx, d[1] - wy, d[2] - wz};
    int64_t d2 = (int64_t)D[0] * D[0] + (int64_t)D[1] * D[1] + (int64_t)D[2] * D[2];
    if (d2 > (int64_t)r * r) return false;
    contact[0] = wx + box.cx; contact[1] = wy + box.cy; contact[2] = wz + box.cz;
    uint32_t dist = (uint32_t)isqrt(d2);
    if ((int32_t)dist < 0x29) { n[0] = n[1] = n[2] = 0; }
    else {
        int32_t inv = (int32_t)((0x100000000000ULL / dist) >> 20);
        for (int i = 0; i < 3; ++i) n[i] = (int16_t)mulq(inv, D[i]);
    }
    depth = r - (int32_t)dist;
    return true;
}

bool Collision::sphereVTri(const int32_t c[3], int32_t r, const TriRef& tr, int32_t contact[3], int32_t n[3], int32_t& depth) {
    const Tri& T = *tr.tri;
    const int32_t* V[4] = {tr.verts + T.v[0] * 3, tr.verts + T.v[1] * 3, tr.verts + T.v[2] * 3, tr.verts + T.v[0] * 3};
    int64_t nv = ((int64_t)T.n[0] * V[0][0] + (int64_t)T.n[1] * V[0][1] + (int64_t)T.n[2] * V[0][2]) & 0xffffffff000LL;
    int32_t d = (int32_t)((((int64_t)c[0] * T.n[0] + (int64_t)c[1] * T.n[1]) - nv + (int64_t)c[2] * T.n[2]) >> 12);
    if (d < 1 || r < d) return false;   // only in front of the plane, within r
    int32_t P[3] = {c[0] - mulq(d, T.n[0]), c[1] - mulq(d, T.n[1]), c[2] - mulq(d, T.n[2])};
    int edge = -1;
    if ((int64_t)T.en[0][0] * (P[0] - V[0][0]) + (int64_t)T.en[0][1] * (P[1] - V[0][1]) + (int64_t)T.en[0][2] * (P[2] - V[0][2]) >= 1) edge = 0;
    else if ((int64_t)T.en[1][0] * (P[0] - V[1][0]) + (int64_t)T.en[1][1] * (P[1] - V[1][1]) + (int64_t)T.en[1][2] * (P[2] - V[1][2]) > 0) edge = 1;
    else if ((int64_t)T.en[2][0] * (P[0] - V[2][0]) + (int64_t)T.en[2][1] * (P[1] - V[2][1]) + (int64_t)T.en[2][2] * (P[2] - V[2][2]) > 0) edge = 2;
    if (edge < 0) {
        contact[0] = P[0]; contact[1] = P[1]; contact[2] = P[2];
        n[0] = T.n[0]; n[1] = T.n[1]; n[2] = T.n[2];
        depth = r - d;
        return true;
    }
    const int32_t* A = V[edge];
    const int32_t* B = V[edge + 1];
    int32_t Q[3] = {A[0], A[1], A[2]};
    if ((int64_t)(B[1] - A[1]) * (P[1] - A[1]) + (int64_t)(B[0] - A[0]) * (P[0] - A[0]) + (int64_t)(B[2] - A[2]) * (P[2] - A[2]) >= 0) {
        Q[0] = B[0]; Q[1] = B[1]; Q[2] = B[2];
        if ((int64_t)(P[1] - B[1]) * (A[1] - B[1]) + (int64_t)(P[0] - B[0]) * (A[0] - B[0]) + (int64_t)(P[2] - B[2]) * (A[2] - B[2]) >= 0) {
            int32_t e[3] = {B[0] - A[0], B[1] - A[1], B[2] - A[2]};
            normalise(e);
            int32_t k = (int32_t)((((int64_t)e[0] * (P[0] - A[0]) + (int64_t)e[1] * (P[1] - A[1]) + (int64_t)e[2] * (P[2] - A[2])) * 0x100000) >> 32);
            for (int i = 0; i < 3; ++i) Q[i] = A[i] + mulq(k, e[i]);
        }
    }
    contact[0] = Q[0]; contact[1] = Q[1]; contact[2] = Q[2];
    int32_t W[3] = {c[0] - Q[0], c[1] - Q[1], c[2] - Q[2]};
    int32_t dist = (int32_t)isqrt((int64_t)W[0] * W[0] + (int64_t)W[1] * W[1] + (int64_t)W[2] * W[2]);
    if (r < dist) return false;
    for (int i = 0; i < 3; ++i) n[i] = dist ? (int32_t)((((int64_t)W[i] << 32) / dist) >> 20) : 0;
    depth = r - dist;
    return true;
}

// ------------------------------------------------------------------------------------------------ camera queries
// CCollision::GetLineIntersectWithStatics: the cells whose centres are within ~70.7 units of the segment.
static void cellsNearSegment(const int32_t* a, const int32_t* b, std::vector<std::pair<int, int>>& out) {
    int ax, ay, bx, by;
    cellOf(a[0], a[1], &ax, &ay);
    cellOf(b[0], b[1], &bx, &by);
    double sx = a[0] / 4096.0, sy = a[1] / 4096.0, dx = (b[0] - a[0]) / 4096.0, dy = (b[1] - a[1]) / 4096.0;
    double len2 = dx * dx + dy * dy;
    for (int cx = std::min(ax, bx); cx <= std::max(ax, bx); ++cx)
        for (int cy = std::min(ay, by); cy <= std::max(ay, by); ++cy) {
            double px = cx * 50.0 - 3475.0, py = cy * 50.0 - 2475.0;   // cell centre
            double k = len2 > 0 ? std::clamp(((px - sx) * dx + (py - sy) * dy) / len2, 0.0, 1.0) : 0.0;
            double ex = sx + dx * k - px, ey = sy + dy * k - py;
            if (ex * ex + ey * ey <= 5000.0) out.push_back({cx, cy});
        }
}

bool Collision::lineHitsBoxes(const int32_t a[3], const int32_t b[3], bool skipFlagged) {
    std::vector<std::pair<int, int>> cells;
    cellsNearSegment(a, b, cells);
    for (auto [cx, cy] : cells) {
        const Cell* C = cell(cx, cy);
        if (!C) continue;
        for (const Box& box : C->boxes) {   // SweptVertVBox: the segment against the box
            if (skipFlagged && (box.flags & 2)) continue;
            int32_t bb[6] = {box.cx - box.hx, box.cy - box.hy, box.cz - box.hz, box.cx + box.hx, box.cy + box.hy, box.cz + box.hz};
            int32_t la[3] = {a[0], a[1], a[2]}, lb[3] = {b[0], b[1], b[2]};
            if (box.angle) {
                int64_t s = fastsin(box.angle), c = fastsin(box.angle + 0x4000);
                auto toLocal = [&](const int32_t* p, int32_t* o) {
                    int64_t dx = p[0] - box.cx, dy = p[1] - box.cy;
                    o[0] = (int32_t)(((int64_t)box.cx * 0x1000 + dx * c + dy * s) >> 12);
                    o[1] = (int32_t)(((int64_t)box.cy * 0x1000 - dx * s + dy * c) >> 12);
                };
                toLocal(a, la);
                toLocal(b, lb);
            }
            int32_t q[3], t;
            if (rayVAABB(la, lb, bb, q, t)) return true;
        }
    }
    return false;
}

bool Collision::sweptSphereHitsBoxes(const int32_t a[3], const int32_t b[3], int32_t r, int32_t contact[3], int32_t n[3]) {
    // GetSphereIntersectWithStatics, boxes only (flag 0x200): the earliest hit; the normal points from the
    // contact to the sphere centre at that moment.
    int64_t mx = b[0] - a[0], my = b[1] - a[1], mz = b[2] - a[2];
    int32_t R = (int32_t)isqrt(mx * mx + my * my + mz * mz) + r;
    Candidates cand;
    candidates(a, R, true, cand);
    int32_t best = 0x7fffffff;
    for (const Box* box : cand.boxes) {
        int32_t hit[3], t;
        if (sweptSphereVBox(a, b, r, *box, hit, t) && t < best) {
            best = t;
            contact[0] = hit[0]; contact[1] = hit[1]; contact[2] = hit[2];
        }
    }
    if (best == 0x7fffffff) return false;
    for (int i = 0; i < 3; ++i) n[i] = (a[i] - contact[i]) + (int32_t)(((int64_t)(b[i] - a[i]) * best) >> 12);
    normalise(n);
    return true;
}

// ------------------------------------------------------------------------------------------------ vehicles
static void boxFrame(const Collision::Box& b, int64_t& s, int64_t& c) {
    s = b.angle ? fastsin(b.angle) : 0;
    c = b.angle ? fastsin(b.angle + 0x4000) : 0x1000;
}
static void toBoxFrame(const Collision::Box& b, int64_t s, int64_t c, const int32_t* p, int32_t* o) {
    o[2] = p[2];
    if (!b.angle) { o[0] = p[0]; o[1] = p[1]; return; }
    int64_t dx = p[0] - b.cx, dy = p[1] - b.cy;
    o[0] = (int32_t)(((int64_t)b.cx * 0x1000 + dx * c + dy * s) >> 12);
    o[1] = (int32_t)(((int64_t)b.cy * 0x1000 - dx * s + dy * c) >> 12);
}
static void fromBoxFrame(const Collision::Box& b, int64_t s, int64_t c, int32_t* p) {
    if (!b.angle) return;
    int64_t dx = p[0] - b.cx, dy = p[1] - b.cy;
    p[0] = (int32_t)(((int64_t)b.cx * 0x1000 + c * dx - s * dy) >> 12);
    p[1] = (int32_t)(((int64_t)b.cy * 0x1000 + s * dx + c * dy) >> 12);
}

bool Collision::circleVBox(const int32_t cIn[3], int32_t r, int32_t h, const Box& box, int32_t contact[3], int32_t n[3], int32_t& depth) {
    if (!(cIn[2] < box.cz + box.hz - 0xCC && box.cz - (box.hz + h) < cIn[2])) return false;
    int64_t s, c;
    boxFrame(box, s, c);
    int32_t l[3];
    toBoxFrame(box, s, c, cIn, l);
    int32_t q[3] = {std::clamp(l[0], box.cx - box.hx, box.cx + box.hx), std::clamp(l[1], box.cy - box.hy, box.cy + box.hy), cIn[2]};
    int32_t d[3] = {l[0] - q[0], l[1] - q[1], 0};
    int64_t d2 = (int64_t)d[0] * d[0] + (int64_t)d[1] * d[1];
    if (!(0 < d2 && d2 < (int64_t)r * r)) return false;
    normalise(d);
    depth = r - (int32_t)isqrt(d2);
    q[0] += mulq(d[0], -r);   // (the game puts the contact r further in along -n)
    q[1] += mulq(d[1], -r);
    if (box.angle) {
        fromBoxFrame(box, s, c, q);
        int32_t nx = d[0], ny = d[1];
        d[0] = (int32_t)((c * nx - s * ny) >> 12);
        d[1] = (int32_t)((s * nx + c * ny) >> 12);
    }
    contact[0] = q[0]; contact[1] = q[1]; contact[2] = q[2];
    n[0] = d[0]; n[1] = d[1]; n[2] = 0;
    return true;
}

bool Collision::sweptCircleVBox(const int32_t a[3], const int32_t b[3], int32_t r, int32_t h, const Box& box, int32_t hit[3], int32_t& t) {
    if (!(a[2] < box.cz + box.hz - 0xCC && box.cz - (box.hz + h) < a[2])) return false;
    int64_t s, c;
    boxFrame(box, s, c);
    int32_t la[3], lb[3];
    toBoxFrame(box, s, c, a, la);
    toBoxFrame(box, s, c, b, lb);
    la[2] = lb[2] = 0;   // in the box's plane (SweptCircleVAABB)
    int32_t bb[6] = {box.cx - box.hx, box.cy - box.hy, -0x40000000, box.cx + box.hx, box.cy + box.hy, 0x40000000};
    if (!sweptSphereVAABB(la, lb, r, bb, hit, t)) return false;
    fromBoxFrame(box, s, c, hit);
    hit[2] = a[2] + mulq(t, b[2] - a[2]);
    return true;
}

bool Collision::sweptVertVBox(const int32_t a[3], const int32_t b[3], const Box& box, int32_t hit[3], int32_t n[3], int32_t& t) {
    int64_t s, c;
    boxFrame(box, s, c);
    int32_t la[3], lb[3];
    toBoxFrame(box, s, c, a, la);
    toBoxFrame(box, s, c, b, lb);
    int32_t mn[3] = {box.cx - box.hx, box.cy - box.hy, box.cz - box.hz}, mx[3] = {box.cx + box.hx, box.cy + box.hy, box.cz + box.hz};
    // CCollision::LineVAABB: slab test; the entry face (0-2 = min faces, 3-5 = max faces) and t (< 0 if inside)
    double tmin = -1e30, tmax = 1e30;
    int face = 0;
    for (int k = 0; k < 3; ++k) {
        double d = (double)lb[k] - la[k];
        if (d == 0) {
            if (la[k] < mn[k] || la[k] > mx[k]) return false;
            continue;
        }
        double t1 = (mn[k] - la[k]) / d, t2 = (mx[k] - la[k]) / d;
        int f1 = k, f2 = k + 3;
        if (t1 > t2) { std::swap(t1, t2); std::swap(f1, f2); }
        if (t1 > tmin) { tmin = t1; face = f1; }
        tmax = std::min(tmax, t2);
    }
    if (tmin > tmax || tmax < 0 || tmin > 1.0) return false;
    t = (int32_t)(tmin * 4096.0);
    if (t < 0) {   // started inside: only counts within 0.2 of the entry face
        t = 0;
        int32_t gap = face < 3 ? la[face] - mn[face] : mx[face - 3] - la[face - 3];
        if (gap > 0x333) return false;
        hit[0] = la[0]; hit[1] = la[1]; hit[2] = la[2];
    } else {
        for (int k = 0; k < 3; ++k) hit[k] = la[k] + (int32_t)((lb[k] - la[k]) * tmin);
    }
    int32_t ln[3] = {0, 0, 0};
    ln[face % 3] = face < 3 ? -0x1000 : 0x1000;
    if ((int64_t)ln[0] * (lb[0] - la[0]) + (int64_t)ln[1] * (lb[1] - la[1]) + (int64_t)ln[2] * (lb[2] - la[2]) >= 1) return false;
    fromBoxFrame(box, s, c, hit);
    n[0] = box.angle ? (int32_t)((c * ln[0] - s * ln[1]) >> 12) : ln[0];
    n[1] = box.angle ? (int32_t)((s * ln[0] + c * ln[1]) >> 12) : ln[1];
    n[2] = ln[2];
    return true;
}

bool Collision::sweptVertVTri(const int32_t a[3], const int32_t b[3], const TriRef& tr, int32_t hit[3], int32_t n[3], int32_t& t) {
    const Tri& T = *tr.tri;
    const int32_t* V[3] = {tr.verts + T.v[0] * 3, tr.verts + T.v[1] * 3, tr.verts + T.v[2] * 3};
    int32_t de = (int32_t)(((int64_t)b[0] * T.n[0] + (int64_t)b[1] * T.n[1] + (int64_t)b[2] * T.n[2]) >> 12);
    int32_t ds = (int32_t)(((int64_t)a[0] * T.n[0] + (int64_t)a[1] * T.n[1] + (int64_t)a[2] * T.n[2]) >> 12);
    if (de > ds) return false;
    int32_t dv = (int32_t)(((int64_t)V[0][2] * T.n[2] + (int64_t)V[0][1] * T.n[1] + (int64_t)V[0][0] * T.n[0]) >> 12);
    int32_t da = ds - dv, db = de - dv;
    if (da < 0) {
        if ((uint32_t)da < 0xFFFFFE67u) { da = 0; if (db < -0x199) return false; }
        else da = 0;
    } else if (db >= 0) return false;
    int64_t den = (int32_t)(da - db);
    int64_t inv = den ? (0x100000000000LL / den) >> 20 : 0;
    int32_t p[3];
    for (int i = 0; i < 3; ++i) p[i] = a[i] + (int32_t)((((((int64_t)(b[i] - a[i]) * da) * 0x100000) >> 32) * inv) >> 12);
    for (int k = 0; k < 3; ++k)
        if ((int64_t)T.en[k][0] * (p[0] - V[k][0]) + (int64_t)T.en[k][1] * (p[1] - V[k][1]) + (int64_t)T.en[k][2] * (p[2] - V[k][2]) >= 1) return false;
    int32_t top = std::max({V[0][2], V[1][2], V[2][2]});
    if (top < -0x270FFFF) top = -0x2710000;
    if (!(top - p[2] > 0x332 || std::abs(T.n[2]) > 0x27)) return false;
    hit[0] = p[0]; hit[1] = p[1]; hit[2] = p[2];
    n[0] = T.n[0]; n[1] = T.n[1]; n[2] = T.n[2];
    t = (int32_t)((den ? (((int64_t)da << 32) / den) : 0) >> 20);
    return true;
}

bool Collision::meshNear(const int32_t p[3], int32_t r) {
    int cx, cy;
    cellOf(p[0], p[1], &cx, &cy);
    const Cell* C = cell(cx, cy);
    if (!C) return false;
    for (const Mesh& m : C->meshes)   // CCollisionMesh::IsNear: the mesh's rectangle within r
        if (m.minX - r <= p[0] && p[0] <= m.maxX + r && m.minY - r <= p[1] && p[1] <= m.maxY + r) return true;
    return false;
}
