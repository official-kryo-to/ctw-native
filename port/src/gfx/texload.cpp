#include "texload.h"
#include "os/pak.h"
#include "os/dxtbin.h"
#include <glad/gl.h>
#include <stb_image.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

// ------------------------------------------------------------------ PVRTC1
// Blocks are 8x4 (2bpp) or 4x4 (4bpp) pixels, 64 bits each (u32 modulation, u32 colours), stored in Morton order.
// Colours A and B are upscaled bilinearly from block centres; each pixel blends them by a modulation weight 0..8.
static uint32_t twiddle(uint32_t x, uint32_t y, uint32_t nx, uint32_t ny) {
    uint32_t mn = std::min(nx, ny), t = 0, bit = 1, dst = 1, shift = 0;
    while (bit < mn) {
        if (y & bit) t |= dst;
        if (x & bit) t |= dst << 1;
        bit <<= 1; dst <<= 2; ++shift;
    }
    return t | ((std::max(x, y) >> shift) << (2 * shift));
}

static inline float expand(uint32_t v, int bits) { return v * 255.f / (float)((1u << bits) - 1); }

static void colours(uint32_t cw, float A[4], float B[4]) {
    uint32_t a = cw & 0xFFFF, b = cw >> 16;
    if (a & 0x8000) { A[0] = expand((a >> 10) & 31, 5); A[1] = expand((a >> 5) & 31, 5); A[2] = expand((a >> 1) & 15, 4); A[3] = 255; }
    else { A[0] = expand((a >> 8) & 15, 4); A[1] = expand((a >> 4) & 15, 4); A[2] = expand((a >> 1) & 7, 3); A[3] = expand((a >> 12) & 7, 3); }
    if (b & 0x8000) { B[0] = expand((b >> 10) & 31, 5); B[1] = expand((b >> 5) & 31, 5); B[2] = expand(b & 31, 5); B[3] = 255; }
    else { B[0] = expand((b >> 8) & 15, 4); B[1] = expand((b >> 4) & 15, 4); B[2] = expand(b & 15, 4); B[3] = expand((b >> 12) & 7, 3); }
}

void DecodePVRTC(const uint8_t* data, int w, int h, bool twoBpp, uint8_t* out) {
    const int bw = twoBpp ? 8 : 4, bh = 4, nbx = std::max(1, w / bw), nby = std::max(1, h / bh);
    std::vector<float> CA((size_t)nbx * nby * 4), CB((size_t)nbx * nby * 4);
    std::vector<uint32_t> mod((size_t)nbx * nby), col((size_t)nbx * nby);
    for (int by = 0; by < nby; ++by)
        for (int bx = 0; bx < nbx; ++bx) {
            size_t k = (size_t)by * nbx + bx, src = (size_t)twiddle(bx, by, nbx, nby) * 8;
            memcpy(&mod[k], data + src, 4);
            memcpy(&col[k], data + src + 4, 4);
            colours(col[k], &CA[k * 4], &CB[k * 4]);
        }
    // per-pixel modulation weights
    std::vector<int> W((size_t)w * h, 0);
    static const int rep[4] = {0, 3, 5, 8};
    if (!twoBpp) {
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x) {
                uint32_t m = mod[(size_t)(y / 4) * nbx + x / 4];
                W[(size_t)y * w + x] = rep[(m >> (2 * ((y & 3) * 4 + (x & 3)))) & 3];
            }
    } else {
        std::vector<int> stored((size_t)w * h, -1), sub((size_t)nbx * nby, 0);
        std::vector<char> interp((size_t)nbx * nby, 0);
        for (int by = 0; by < nby; ++by)
            for (int bx = 0; bx < nbx; ++bx) {
                size_t k = (size_t)by * nbx + bx;
                uint64_t m = mod[k];
                if (!(col[k] & 1)) {   // direct: 1 bit per pixel
                    for (int py = 0; py < 4; ++py)
                        for (int px = 0; px < 8; ++px)
                            W[(size_t)(by * 4 + py) * w + bx * 8 + px] = ((m >> (py * 8 + px)) & 1) ? 8 : 0;
                    continue;
                }
                interp[k] = 1;
                int s = 1;                                   // 1: average of 4 neighbours, 2: horizontal, 3: vertical
                if (m & 1) {
                    s = (m & (1u << 20)) ? 3 : 2;
                    if (m & (1u << 21)) m |= (1u << 20); else m &= ~(uint64_t)(1u << 20);
                }
                if (m & 2) m |= 1; else m &= ~(uint64_t)1;
                sub[k] = s;
                int n = 0;
                for (int py = 0; py < 4; ++py)
                    for (int px = 0; px < 8; ++px)
                        if (((px ^ py) & 1) == 0) stored[(size_t)(by * 4 + py) * w + bx * 8 + px] = rep[(m >> (2 * n++)) & 3];
            }
        auto at = [&](int x, int y) { return stored[(size_t)((y + h) % h) * w + (x + w) % w]; };
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x) {
                size_t k = (size_t)(y / 4) * nbx + x / 8;
                if (!interp[k]) continue;
                int v = stored[(size_t)y * w + x];
                if (v < 0) {
                    int s = sub[k];
                    if (s == 1) v = (at(x, y - 1) + at(x, y + 1) + at(x - 1, y) + at(x + 1, y) + 2) / 4;
                    else if (s == 2) v = (at(x - 1, y) + at(x + 1, y) + 1) / 2;
                    else v = (at(x, y - 1) + at(x, y + 1) + 1) / 2;
                }
                W[(size_t)y * w + x] = v;
            }
    }
    for (int y = 0; y < h; ++y) {
        float fy = (y - bh / 2.f) / bh;
        int y0 = (int)std::floor(fy);
        float ty = fy - y0;
        for (int x = 0; x < w; ++x) {
            float fx = (x - bw / 2.f) / bw;
            int x0 = (int)std::floor(fx);
            float tx = fx - x0;
            size_t k00 = (size_t)((y0 % nby + nby) % nby) * nbx + (x0 % nbx + nbx) % nbx;
            size_t k01 = (size_t)((y0 % nby + nby) % nby) * nbx + ((x0 + 1) % nbx + nbx) % nbx;
            size_t k10 = (size_t)(((y0 + 1) % nby + nby) % nby) * nbx + (x0 % nbx + nbx) % nbx;
            size_t k11 = (size_t)(((y0 + 1) % nby + nby) % nby) * nbx + ((x0 + 1) % nbx + nbx) % nbx;
            float wt = W[(size_t)y * w + x] / 8.f;
            for (int c = 0; c < 4; ++c) {
                auto bil = [&](const std::vector<float>& C) {
                    return (C[k00 * 4 + c] * (1 - tx) + C[k01 * 4 + c] * tx) * (1 - ty) +
                           (C[k10 * 4 + c] * (1 - tx) + C[k11 * 4 + c] * tx) * ty;
                };
                float v = bil(CA) * (1 - wt) + bil(CB) * wt;
                out[((size_t)y * w + x) * 4 + c] = (uint8_t)std::max(0.f, std::min(255.f, v + 0.5f));
            }
        }
    }
}

// ------------------------------------------------------------------ loader
static unsigned newTex() {
    GLuint t;
    glGenTextures(1, &t);
    glBindTexture(GL_TEXTURE_2D, t);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);   // single level, no mipmaps
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    return t;
}

bool TextureLoader::load(uint32_t id, TexInfo* out) const {
    DxtTexture d;
    if (dxt_ && dxt_->get((int)id, d)) {
        out->gl = newTex();
        out->width = d.width; out->height = d.height; out->format = d.glFormat; out->fromDxt = true;
        glCompressedTexImage2D(GL_TEXTURE_2D, 0, d.glFormat, d.width, d.height, 0, (GLsizei)d.data.size(), d.data.data());
        return true;
    }
    std::vector<uint8_t> r;
    if (!pak_ || !pak_->read(id, r) || r.size() < 12) return false;
    uint16_t w, h, fmt;
    uint32_t size;
    memcpy(&w, &r[0], 2); memcpy(&h, &r[2], 2); memcpy(&fmt, &r[4], 2); memcpy(&size, &r[8], 4);
    if (w == 0 || h == 0 || w > 4096 || h > 4096) return false;
    const uint8_t* px = r.data() + 12;
    size_t avail = r.size() - 12;
    out->width = w; out->height = h; out->format = fmt; out->fromDxt = false;
    switch (fmt) {
        case 0xBEEF: {
            int iw, ih, n;
            unsigned char* img = stbi_load_from_memory(px, (int)std::min<size_t>(size, avail), &iw, &ih, &n, 4);
            if (!img) return false;
            out->gl = newTex(); out->width = iw; out->height = ih;
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, iw, ih, 0, GL_RGBA, GL_UNSIGNED_BYTE, img);
            stbi_image_free(img);
            return true;
        }
        case 0x8033: case 0x8034:
            if (avail < (size_t)w * h * 2) return false;
            out->gl = newTex();
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, fmt, px);
            return true;
        case 0x8363:
            if (avail < (size_t)w * h * 2) return false;
            out->gl = newTex();
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, w, h, 0, GL_RGB, GL_UNSIGNED_SHORT_5_6_5, px);
            return true;
        case 0x1909: case 0x190A: {
            size_t bpp = fmt == 0x190A ? 2 : 1;
            if (avail < (size_t)w * h * bpp) return false;
            out->gl = newTex();
            glTexImage2D(GL_TEXTURE_2D, 0, fmt, w, h, 0, fmt, GL_UNSIGNED_BYTE, px);
            return true;
        }
        case 0x83F0: case 0x83F1: case 0x83F2: case 0x83F3:
            out->gl = newTex();
            glCompressedTexImage2D(GL_TEXTURE_2D, 0, fmt, w, h, 0, (GLsizei)std::min<size_t>(size, avail), px);
            return true;
        case 0x8C00: case 0x8C01: case 0x8C02: case 0x8C03: {
            bool two = (fmt & 1) != 0;   // 0x8C01 / 0x8C03 are the 2bpp variants
            size_t need = (size_t)w * h * (two ? 2 : 4) / 8;
            if (avail < need) return false;
            std::vector<uint8_t> rgba((size_t)w * h * 4);
            DecodePVRTC(px, w, h, two, rgba.data());
            out->gl = newTex();
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
            return true;
        }
        default:
            return false;
    }
}
