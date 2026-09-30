#include "pedsprites.h"
#include <utility>
#include "gfx/assets.h"
#include "os/pak.h"
#include "world/timecycle.h"
#include <glad/gl.h>
#include <cmath>
#include <cstring>

// DAT_00586a8c: palette slot of each layer, per body type (upper body = 0/2, legs = 1/3)
static const uint8_t kLayerSlot[4][6] = {{2, 12, 8, 3, 5, 13}, {8, 10, 12, 0, 0, 0}, {2, 12, 8, 3, 5, 13}, {8, 5, 10, 10, 0, 0}};
// cPedSprite::ModifyForward: (cos, sin) for the animation's angle adjustment in 45° steps
static const int kAngle[8][2] = {{4096, 0}, {2895, 2895}, {0, 4096}, {-2895, 2895}, {-4096, 0}, {-2895, -2895}, {0, -4096}, {2895, -2895}};
static const float kLayerStep = -204.f / 4096.f;   // sSpriteOffset

bool PedSprites::init() {
    std::vector<uint8_t> dir;
    uint16_t gd[28];
    if (!Assets_Pak().read(0, dir) || dir.size() < sizeof gd) return false;
    memcpy(gd, dir.data(), sizeof gd);
    if (!Assets_Pak().read(gd[7], anims_) || !Assets_Pak().read(gd[8], palettes_)) { anims_.clear(); return false; }
    return true;
}

const uint8_t* PedSprites::animData(int anim) const {
    if (anim < 0 || anim >= 0x226 || (size_t)(anim + 2) * 2 > anims_.size()) return nullptr;
    uint16_t off;
    memcpy(&off, &anims_[(size_t)(anim + 1) * 2], 2);
    if ((size_t)off * 4 + 8 > anims_.size()) return nullptr;
    return &anims_[(size_t)off * 4];
}

int PedSprites::numFrames(int anim) const { const uint8_t* a = animData(anim); uint16_t n = 0; if (a) memcpy(&n, a, 2); return n; }
int PedSprites::rate(int anim) const { const uint8_t* a = animData(anim); int16_t r = 0; if (a) memcpy(&r, a + 2, 2); return r; }
bool PedSprites::oneShot(int anim) const { const uint8_t* a = animData(anim); return a && (a[6] & 1); }

int PedSprites::advance(int anim, int frame, int step) const {
    int n = numFrames(anim);
    if (n <= 0) return 0;
    int f = frame + (int)(((int64_t)rate(anim) * step) >> 8);
    int end = n << 8;
    if (f >= end) f = oneShot(anim) ? end - 0x100 : f % end;
    if (f < 0) f = 0;
    return f;
}

// DrawPedPrim's corner colour: the palette colour pulled towards the ambient colour by dot(corner dir, light) x day
PedLight PedLight::fromTimeCycle(const TimeCycle& cycle) {
    PedLight light;
    uint32_t t = cycle.time();
    light.on = t >= 0x7000 && t < 0x14000;
    if (light.on) {
        light.day = t < 0x8000 ? (t - 0x7000) / 4096.f :
                    t <= 0x13000 ? 1.f : (0x14000 - t) / 4096.f;
    }
    cycle.sunDirection(light.dir);
    light.ambient = cycle.colour(0xD);
    return light;
}

void PedLight::cornerColour(const float corner[3], const float centre[3], uint32_t base, uint8_t out[4]) const {
    out[0] = (GLubyte)(base & 0xFF); out[1] = (GLubyte)(base >> 8 & 0xFF); out[2] = (GLubyte)(base >> 16 & 0xFF); out[3] = 255;
    if (!on) return;
    float d[3] = {corner[0] - centre[0], corner[1] - centre[1], corner[2] - centre[2]};
    float len = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
    if ((double)len * len * 4294967296.0 > 0x19900000) { d[0] /= len; d[1] /= len; d[2] /= len; }
    int k = (int)((d[0] * dir[0] + d[1] * dir[1] + d[2] * dir[2]) * day * 4096.f);
    if (k <= 0) return;
    if (k > 0x2000) k = 0x2000;
    for (int i = 0; i < 3; ++i) {
        int b = base >> (8 * i) & 0xFF, a = ambient >> (8 * i) & 0xFF;
        out[i] = (GLubyte)((b + (int)(((int64_t)k * ((a - b) * 0x1000)) >> 24)) & 0xFF);
    }
}

bool PedSprites::advanceOneShot(int anim, int& frame, int step) const {
    int n = numFrames(anim);
    if (n <= 0) return true;
    frame += (int)(((int64_t)rate(anim) * step) >> 8);
    if (frame >= n << 8) { frame = (n << 8) - 0x100; return true; }
    return false;
}

void PedSprites::draw(const float pos[3], float heading, int anim, int frame, int palA, int palB, float zA, float zB,
                      const PedLight* light, bool flip) const {
    const uint8_t* a = animData(anim);
    if (!a) return;
    uint16_t nFrames, angleAdj;
    memcpy(&nFrames, a, 2);
    memcpy(&angleAdj, a + 4, 2);
    int body = a[6] >> 4, nc = a[7];
    int f = frame >> 8;
    if (nFrames == 0 || nc == 0) return;
    if (f >= nFrames) f = nFrames - 1;
    if ((size_t)(a - anims_.data()) + 8 + (size_t)(f + 1) * nc * 12 > anims_.size()) return;

    // forward vector (heading) turned by the animation's angle adjustment (x' = c x + s y, y' = c y - s x)
    float fx = -sinf(heading), fy = cosf(heading);
    if (angleAdj) {
        int k = angleAdj >> 13 & 7;
        float c = kAngle[k][0] / 4096.f, s = kAngle[k][1] / 4096.f;
        float nx = c * fx + s * fy, ny = c * fy - s * fx;
        fx = nx; fy = ny;
    }
    float rx = -fy, ry = fx;   // side vector
    int split = anim > 0x112 ? 2 : 1;   // layers below this index use palette A and height zA

    for (int c = nc - 1; c >= 0; --c) {
        const uint8_t* L = a + 8 + ((size_t)f * nc + c) * 12;
        uint16_t tex, u, v, w, h;
        memcpy(&tex, L, 2);
        memcpy(&u, L + 4, 2); memcpy(&v, L + 6, 2); memcpy(&w, L + 8, 2); memcpy(&h, L + 10, 2);
        if (!w || !h) continue;
        int tw, th;
        GLuint gl = Assets_Texture(tex);
        if (!gl || !Assets_TextureSize(tex, &tw, &th)) continue;
        float side = (L[2] - 32) / 16.f, fwd = (L[3] - 32) / 16.f;
        float sw = w / 16.f, sh = h / 16.f;
        if (tex == 0x8A7) { sw = w * 1024.f / 3.f / 8192.f; sh = h * 1024.f / 3.f / 8192.f; }   // DrawPedPrim special case
        bool groupA = c < split;
        int pal = groupA ? palA : palB;
        uint16_t col = 0x7FFF;
        size_t pi = (size_t)pal * 32 + (size_t)kLayerSlot[body & 3][c % 6] * 2;
        if (pi + 2 <= palettes_.size()) memcpy(&col, &palettes_[pi], 2);
        float z = pos[2] + (groupA ? zA : zB) + kLayerStep * c;
        float bx = pos[0] + side * rx + fwd * fx, by = pos[1] + side * ry + fwd * fy;
        float u0 = (float)u / tw, u1 = (float)(u + w) / tw, v0 = (float)v / th, v1 = (float)(v + h) / th;
        if (flip) std::swap(u0, u1);   // DrawPedPrim: a flipped sprite swaps its u coordinates
        glBindTexture(GL_TEXTURE_2D, gl);
        uint32_t base = (uint32_t)((col & 0x1F) << 3) | (uint32_t)((col >> 5 & 0x1F) << 3) << 8 | (uint32_t)((col >> 10 & 0x1F) << 3) << 16;
        float p0[3] = {bx, by, z}, p1[3] = {bx + fx * sh, by + fy * sh, z};
        float p2[3] = {p1[0] + rx * sw, p1[1] + ry * sw, z}, p3[3] = {bx + rx * sw, by + ry * sw, z};
        float mid[3] = {(p0[0] + p2[0]) * 0.5f, (p0[1] + p2[1]) * 0.5f, z};
        GLubyte c0[4], c1[4], c2[4], c3[4];
        // cPedBucketRenderer::Render leaves upper-body component 0 unshaded.
        const PedLight unshaded;
        const PedLight& layerLight = light && PedLight::shadesLayer(body, c) ? *light : unshaded;
        layerLight.cornerColour(p0, mid, base, c0);
        layerLight.cornerColour(p1, mid, base, c1);
        layerLight.cornerColour(p2, mid, base, c2);
        layerLight.cornerColour(p3, mid, base, c3);
        glBegin(GL_TRIANGLES);   // (base, +F, +F+R), (base, +F+R, +R) as DrawPedPrim emits them
        glColor4ubv(c0); glTexCoord2f(u0, v0); glVertex3fv(p0);
        glColor4ubv(c1); glTexCoord2f(u0, v1); glVertex3fv(p1);
        glColor4ubv(c2); glTexCoord2f(u1, v1); glVertex3fv(p2);
        glColor4ubv(c0); glTexCoord2f(u0, v0); glVertex3fv(p0);
        glColor4ubv(c2); glTexCoord2f(u1, v1); glVertex3fv(p2);
        glColor4ubv(c3); glTexCoord2f(u1, v0); glVertex3fv(p3);
        glEnd();
    }
}
