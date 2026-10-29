// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kryo.to
// See LICENSE in the repository root.
// (The same drawing as ctw-modkit/include/ctw_ui.h; keep the two alike.)
#include "porterui.h"
#include "hud.h"
#include "plugins.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>

namespace PorterUi {
uint32_t alpha(uint32_t rgba, float a) {
    const float v = (float)(rgba & 0xFFu) * a + 0.5f;
    return (rgba & 0xFFFFFF00u) | (uint32_t)std::min(v, 255.f);
}

namespace {
void span(float x, float y, float w, float inset, uint32_t rgba) {   // a row, with smooth ends
    const float whole = std::ceil(inset), part = whole - inset;
    if (w - 2.f * whole > 0.f) Hud_Rect(x + whole, y, w - 2.f * whole, 1.f, rgba);
    if (part > 0.01f && whole >= 1.f) {
        const uint32_t edge = alpha(rgba, part);
        Hud_Rect(x + whole - 1.f, y, 1.f, 1.f, edge);
        Hud_Rect(x + w - whole, y, 1.f, 1.f, edge);
    }
}
float inset(float r, int row) {
    const float dy = r - (float)row - 0.5f;
    return std::max(0.f, r - std::sqrt(r * r - dy * dy));
}
void snap(float& x, float& y, float& w, float& h) {
    x = std::floor(x + 0.5f); y = std::floor(y + 0.5f); w = std::floor(w + 0.5f); h = std::floor(h + 0.5f);
}
}   // namespace

void roundRect(float x, float y, float w, float h, float r, uint32_t rgba) {
    if (w <= 0.f || h <= 0.f) return;
    snap(x, y, w, h);
    r = std::min({r, w * 0.5f, h * 0.5f});
    const int n = (int)std::ceil(r);
    if (n < 1) { Hud_Rect(x, y, w, h, rgba); return; }
    for (int i = 0; i < n && (float)i < h * 0.5f; ++i) {
        const float in = inset(r, i);
        span(x, y + (float)i, w, in, rgba);
        if (y + h - (float)i - 1.f > y + (float)i) span(x, y + h - (float)i - 1.f, w, in, rgba);
    }
    if (h - 2.f * (float)n > 0.f) Hud_Rect(x, y + (float)n, w, h - 2.f * (float)n, rgba);
}

void frame(float x, float y, float w, float h, float r, float t, uint32_t rgba) {
    if (w <= 0.f || h <= 0.f) return;
    snap(x, y, w, h);
    r = std::min({r, w * 0.5f, h * 0.5f});
    const float ri = std::max(0.f, r - t);
    const int n = std::max((int)std::ceil(r), (int)t);
    for (int i = 0; i < n && (float)i < h * 0.5f; ++i)
        for (int side = 0; side < 2; ++side) {
            const float yy = side ? y + h - (float)i - 1.f : y + (float)i;
            if (side && yy <= y + (float)i) break;
            const float outer = (float)i < r ? inset(r, i) : 0.f;
            if ((float)i < t) { span(x, yy, w, outer, rgba); continue; }
            const int j = i - (int)t;
            const float inner = t + ((float)j < ri ? inset(ri, j) : 0.f);
            const float ow = std::ceil(inner) - std::floor(outer);
            Hud_Rect(x + std::floor(outer), yy, ow, 1.f, rgba);
            Hud_Rect(x + w - std::floor(outer) - ow, yy, ow, 1.f, rgba);
        }
    if (h - 2.f * (float)n > 0.f) {
        Hud_Rect(x, y + (float)n, t, h - 2.f * (float)n, rgba);
        Hud_Rect(x + w - t, y + (float)n, t, h - 2.f * (float)n, rgba);
    }
}

void line(float x0, float y0, float x1, float y1, float w, uint32_t rgba) {
    const float dx = x1 - x0, dy = y1 - y0, len2 = dx * dx + dy * dy;
    if (len2 <= 0.f) return;
    for (float py = std::floor(std::min(y0, y1) - w); py < std::max(y0, y1) + w; py += 1.f)
        for (float px = std::floor(std::min(x0, x1) - w); px < std::max(x0, x1) + w; px += 1.f) {
            int hits = 0;   // 4 x 4 samples against the round-capped stroke
            for (int s = 0; s < 16; ++s) {
                const float sx = px + ((float)(s & 3) + 0.5f) / 4.f, sy = py + ((float)(s >> 2) + 0.5f) / 4.f;
                const float t = std::clamp(((sx - x0) * dx + (sy - y0) * dy) / len2, 0.f, 1.f);
                const float ex = sx - (x0 + dx * t), ey = sy - (y0 + dy * t);
                hits += ex * ex + ey * ey <= w * w * 0.25f;
            }
            if (hits) Hud_Rect(px, py, 1.f, 1.f, alpha(rgba, (float)hits / 16.f));
        }
}

// lucide's icons (24-unit view box)
void iconX(float x, float y, float size, uint32_t rgba) {
    const float k = size / 24.f;
    line(x + 18.f * k, y + 6.f * k, x + 6.f * k, y + 18.f * k, 2.f * k, rgba);
    line(x + 6.f * k, y + 6.f * k, x + 18.f * k, y + 18.f * k, 2.f * k, rgba);
}
void iconCheck(float x, float y, float size, float stroke, uint32_t rgba) {
    const float k = size / 24.f;
    line(x + 20.f * k, y + 6.f * k, x + 9.f * k, y + 17.f * k, stroke * k, rgba);
    line(x + 9.f * k, y + 17.f * k, x + 4.f * k, y + 12.f * k, stroke * k, rgba);
}
void iconChevron(float x, float y, float size, bool right, uint32_t rgba) {
    const float k = size / 24.f, tip = right ? 15.f : 9.f, back = right ? 9.f : 15.f;
    line(x + back * k, y + 18.f * k, x + tip * k, y + 12.f * k, 2.f * k, rgba);
    line(x + tip * k, y + 12.f * k, x + back * k, y + 6.f * k, 2.f * k, rgba);
}

void card(float x, float y, float w, float h) {
    // box-shadow 0 25px 50px -12px black/70, outside the card: a Gaussian (sigma 25) in nested layers
    constexpr int kLayers = 16;
    const float sigma = 25.f, spread = -12.f, peak = 0.7f;
    const float sx = x - spread, sy = y + 25.f - spread, sw = w + 2.f * spread, sh = h + 2.f * spread;
    const float sr = std::max(0.f, Radius + spread);
    float target[kLayers + 1];
    for (int i = 0; i <= kLayers; ++i) {
        const float g = -2.f * sigma + 4.f * sigma * ((float)i + 0.5f) / kLayers;
        target[i] = i == kLayers ? 0.f : peak * 0.5f * std::erfc(g / (sigma * 1.41421356f));
    }
    const float blur = 50.f;
    const float bands[4][4] = {{x - 3.f * blur, y, 3.f * blur, h + 3.f * blur}, {x + w, y, 3.f * blur, h + 3.f * blur},
                               {x, y + h, w, 3.f * blur}, {x - 3.f * blur, y - 3.f * blur, w + 6.f * blur, 3.f * blur}};
    const CtwHostApi& api = Plugins_HostApi();
    for (const auto& b : bands) {
        api.set_clip(b[0], b[1], b[2], b[3]);
        for (int i = kLayers - 1; i >= 0; --i) {
            const float g = -2.f * sigma + 4.f * sigma * (float)(i + 1) / kLayers;
            const float layer = 1.f - (1.f - target[i]) / (1.f - target[i + 1]);
            if (layer <= 0.f || sw + 2.f * g <= 0.f || sh + 2.f * g <= 0.f) continue;
            roundRect(sx - g, sy - g, sw + 2.f * g, sh + 2.f * g, std::max(0.f, sr + g), (uint32_t)(layer * 255.f + 0.5f));
        }
    }
    api.set_clip(0.f, 0.f, 0.f, 0.f);
    roundRect(x, y, w, h, Radius, Card);
    frame(x, y, w, h, Radius, 1.f, Border);
}

void text(float x, float y, float px, float tracking, int style, uint32_t rgba, const char* s) {
    Hud_UiText(x, y, px, tracking, style, rgba, s);
}
float textWidth(float px, float tracking, int style, const char* s) { return Hud_UiTextWidth(px, tracking, style, s); }

void textFit(float x, float y, float px, float tracking, int style, uint32_t rgba, const char* s, float maxW) {
    std::string t(s ? s : "");
    if (textWidth(px, tracking, style, t.c_str()) <= maxW) { text(x, y, px, tracking, style, rgba, t.c_str()); return; }
    while (!t.empty()) {   // `truncate`: whole UTF-8 characters, then an ellipsis
        t.pop_back();
        while (!t.empty() && ((unsigned char)t.back() & 0xC0) == 0x80) t.pop_back();
        if (!t.empty() && ((unsigned char)t.back() & 0xC0) == 0xC0) t.pop_back();
        while (!t.empty() && t.back() == ' ') t.pop_back();
        const std::string u = t + "\xE2\x80\xA6";
        if (textWidth(px, tracking, style, u.c_str()) <= maxW) { text(x, y, px, tracking, style, rgba, u.c_str()); return; }
    }
}

void titleBar(float x, float y, float w, const char* title, const char* note, bool closeHot, float out[4]) {
    const float th = 40.f, bs = 28.f, bx = x + w - 8.f - bs, by = y + (th - bs) * 0.5f;
    Hud_Rect(x + 1.f, y + th - 1.f, w - 2.f, 1.f, Border);
    float tx = x + 16.f;
    textFit(tx, typeY(y, th, 11.f), 11.f, 0.3f, Bold | Upper, Foreground, title, bx - 12.f - tx);
    tx += textWidth(11.f, 0.3f, Bold | Upper, title) + 12.f;
    if (note && *note) {
        const float nw = textWidth(10.f, 0.05f, 0, note) + 16.f, nh = 21.f;
        if (tx + nw < bx - 12.f) {
            const float ny = y + (th - nh) * 0.5f;
            frame(tx, ny, nw, nh, nh * 0.5f, 1.f, Border);
            text(tx + 8.f, ny + 3.f, 10.f, 0.05f, 0, MutedFg, note);
        }
    }
    if (closeHot) roundRect(bx, by, bs, bs, bs * 0.5f, Secondary);
    iconX(bx + 7.f, by + 7.f, 14.f, closeHot ? Foreground : MutedFg);
    out[0] = bx; out[1] = by; out[2] = bs; out[3] = bs;
}

float buttonWidth(int size, const char* label) {
    const float px = size == Sm ? 10.f : size == Lg ? TextSm : 11.f, pad = size == Sm ? 12.f : size == Lg ? 32.f : 16.f;
    return textWidth(px, size == Lg ? 0.2f : 0.05f, Bold | Upper, label) + 2.f * pad;
}

void button(float x, float y, float w, float h, int variant, int size, bool hot, bool pressed, bool disabled, const char* label) {
    const float px = size == Sm ? 10.f : size == Lg ? TextSm : 11.f, tracking = size == Lg ? 0.2f : 0.05f;
    const float k = disabled ? 0.4f : 1.f;
    hot = hot && !disabled;
    if (hot && pressed) { x += w * 0.015f; y += h * 0.015f; w *= 0.97f; h *= 0.97f; }   // active:scale-[0.97]
    uint32_t colour = MutedFg;
    if (variant == PrimaryButton) { roundRect(x, y, w, h, h * 0.5f, alpha(Primary, (hot ? 0.9f : 1.f) * k)); colour = PrimaryFg; }
    else if (variant == Ghost) { if (hot) roundRect(x, y, w, h, h * 0.5f, Secondary); colour = hot ? Foreground : MutedFg; }
    else if (variant == Danger) {
        if (hot) roundRect(x, y, w, h, h * 0.5f, Destructive);
        else frame(x, y, w, h, h * 0.5f, 1.f, alpha(Destructive, 0.5f * k));
        colour = hot ? 0xFCFCFCFFu : Destructive;
    } else { frame(x, y, w, h, h * 0.5f, 1.f, alpha(hot ? Foreground : Border, k)); colour = hot ? Foreground : MutedFg; }
    const float tw = std::min(textWidth(px, tracking, Bold | Upper, label), w - 16.f);
    textFit(x + (w - tw) * 0.5f, typeY(y, h, px), px, tracking, Bold | Upper, alpha(colour, k), label, w - 16.f);
}

void check(float x, float y, float h, bool on, bool hot) {
    const float by = y + (h - 16.f) * 0.5f;
    if (on) { roundRect(x, by, 16.f, 16.f, 5.f, Primary); iconCheck(x + 2.f, by + 2.f, 12.f, 3.f, PrimaryFg); }
    else frame(x, by, 16.f, 16.f, 5.f, 1.f, hot ? MutedFg : Border);
}

void field(float x, float y, float w, float h, bool hot, bool focused) {
    roundRect(x, y, w, h, h * 0.5f, Background);
    frame(x, y, w, h, h * 0.5f, 1.f, hot && !focused ? MutedFg : Border);
    if (focused) frame(x - 4.f, y - 4.f, w + 8.f, h + 8.f, (h + 8.f) * 0.5f, 2.f, Ring);
}
}   // namespace PorterUi
