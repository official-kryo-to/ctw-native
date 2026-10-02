/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kryo.to
 * See LICENSE in the repository root.
 *
 * GTA: Chinatown Wars PC port - small immediate-mode UI for mods (header only, plain C).
 *
 * Call everything from an on_draw_hud callback. One CtwUi per window keeps the little state widgets need
 * (the slider being dragged, smooth scrolling). Needs mod API version 3. Sizes follow the game font
 * (ctw_ui_row_h and friends), and ctw_ui_side_area gives a window a place clear of the game's HUD and the mod menu.
 *
 *   static CtwUi ui;
 *   ctw_ui_begin(&ui, api);
 *   CtwUiRect r = ctw_ui_side_area(api, 480.f);
 *   ctw_ui_panel(&ui, r.x, r.y, r.w, r.h, "MY WINDOW", NULL);
 *   if (ctw_ui_button(&ui, r.x + 16, r.y + ctw_ui_title_h(&ui) + 10, 140, ctw_ui_row_h(&ui), "Click me")) ...;
 */
#ifndef CTW_UI_H
#define CTW_UI_H

#include "ctw_mod.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

#define CTW_UI_ACCENT 0xF2C230FFu
#define CTW_UI_TEXT 0xF4F5F8FFu
#define CTW_UI_DIM 0x9A9DAAFFu
#define CTW_UI_ERROR 0xF2675AFFu
#define CTW_UI_OK 0x5BD48AFFu
#define CTW_UI_PANEL 0x14151BF0u
#define CTW_UI_SURFACE 0xFFFFFF0Au   /* cards and fields on a panel */
#define CTW_UI_ROW 0xFFFFFF08u
#define CTW_UI_HOVER 0xFFFFFF14u
#define CTW_UI_LINE 0xFFFFFF12u
#define CTW_UI_TRACK 0x3A3C46FFu
#define CTW_UI_GAP 12.f          /* between windows, and from the screen edge */

typedef struct CtwUi {
    const CtwApi* api;
    float mx, my, wheel;
    int clicked, down;
    int drag;              /* id of the widget being dragged, 0 = none */
    float drag_offset;
    int wheel_used;        /* a scroll area took this frame's wheel */
} CtwUi;

typedef struct CtwUiRect { float x, y, w, h; } CtwUiRect;

static inline int ctw_ui_in(const CtwUi* ui, float x, float y, float w, float h) {
    return ui->mx >= x && ui->mx < x + w && ui->my >= y && ui->my < y + h;
}

/* A rectangle with rounded corners (radius in pixels), drawn as non-overlapping strips so translucent colours stay even. */
static inline void ctw_ui_round_rect(const CtwApi* a, float x, float y, float w, float h, float r, uint32_t rgba) {
    if (w <= 0.f || h <= 0.f) return;
    if (r > w * 0.5f) r = w * 0.5f;
    if (r > h * 0.5f) r = h * 0.5f;
    const int n = (int)r;
    if (n < 1) { a->draw_rect(x, y, w, h, rgba); return; }
    for (int i = 0; i < n; ++i) {   /* inset of each 1-pixel row of the corner circle */
        const float dy = r - (float)i - 0.5f;
        float inset = r - sqrtf(r * r - dy * dy);
        if (inset < 0.f) inset = 0.f;
        a->draw_rect(x + inset, y + (float)i, w - 2.f * inset, 1.f, rgba);
        a->draw_rect(x + inset, y + h - (float)i - 1.f, w - 2.f * inset, 1.f, rgba);
    }
    a->draw_rect(x, y + (float)n, w, h - 2.f * (float)n, rgba);
}

static inline void ctw_ui_begin(CtwUi* ui, const CtwApi* api) {
    ui->api = api;
    api->get_mouse(&ui->mx, &ui->my);
    ui->clicked = api->mouse_clicked(0);
    ui->down = api->mouse_down(0);
    ui->wheel = api->mouse_wheel();
    ui->wheel_used = 0;
    if (!ui->down) ui->drag = 0;
}

/* ---- sizes: everything follows the game font's line height, so nothing collides at any font size ------------ */

static inline float ctw_ui_lh(const CtwUi* ui, float scale) { return ui->api->line_height(scale); }
/* A one-line row (switches, buttons). */
static inline float ctw_ui_row_h(const CtwUi* ui) { return ctw_ui_lh(ui, 1.f) + 18.f; }
/* A slider row: its label line and the bar under it. */
static inline float ctw_ui_slider_h(const CtwUi* ui) { return ctw_ui_lh(ui, 1.f) + 30.f; }
/* A window's title bar. */
static inline float ctw_ui_title_h(const CtwUi* ui) { return ctw_ui_lh(ui, 1.35f) + 26.f; }
/* The hint line at the bottom of a window. */
static inline float ctw_ui_footer_h(const CtwUi* ui) { return ctw_ui_lh(ui, 0.9f) + 20.f; }

/* ---- where windows go ------------------------------------------------------------------------------------------ */
/* The game draws its clock at the top right and a line of help at the bottom; windows keep clear of both. The mod
 * menu (F4) is on the left; other windows go on the right, beside the menu while it is open. */

static inline float ctw_ui_screen_bottom(const CtwApi* a) { return (float)a->screen_height() - 30.f - CTW_UI_GAP; }

static inline float ctw_ui_menu_width(const CtwApi* a) {
    float w = (float)a->screen_width() * 0.6f;
    if (w < 520.f) w = 520.f;
    if (w > 860.f) w = 860.f;
    if (w > (float)a->screen_width() - 2.f * CTW_UI_GAP) w = (float)a->screen_width() - 2.f * CTW_UI_GAP;
    return w;
}

/* The mod menu's place: the left edge, full height. */
static inline CtwUiRect ctw_ui_menu_area(const CtwApi* a) {
    CtwUiRect r = {CTW_UI_GAP, CTW_UI_GAP, ctw_ui_menu_width(a), 0.f};
    r.h = ctw_ui_screen_bottom(a) - r.y;
    return r;
}

/* A mod's window: on the right, below the game's clock, at most max_w wide, and never under the open mod menu. */
static inline CtwUiRect ctw_ui_side_area(const CtwApi* a, float max_w) {
    const float W = (float)a->screen_width();
    const float left = a->menu_is_open() ? CTW_UI_GAP + ctw_ui_menu_width(a) + CTW_UI_GAP : W * 0.5f;
    CtwUiRect r;
    r.w = W - CTW_UI_GAP - left;
    if (r.w > max_w) r.w = max_w;
    if (r.w < 300.f) r.w = W - 2.f * CTW_UI_GAP < 300.f ? W - 2.f * CTW_UI_GAP : 300.f;   /* a very small window */
    r.x = W - CTW_UI_GAP - r.w;
    r.y = 16.f + a->line_height(1.5f) + CTW_UI_GAP;   /* the clock: 1.5x text at y 16 */
    r.h = ctw_ui_screen_bottom(a) - r.y;
    return r;
}

/* ---- text -------------------------------------------------------------------------------------------------------- */

static inline void ctw_ui_text(const CtwUi* ui, float x, float y, float scale, uint32_t rgba, const char* text) {
    ui->api->draw_text(x, y, scale, rgba, text);
}

/* Text cut to max_w pixels with "..." */
static inline void ctw_ui_text_fit(const CtwUi* ui, float x, float y, float scale, uint32_t rgba, const char* text, float max_w) {
    char buf[256];
    size_t n = strlen(text);
    if (n >= sizeof buf) n = sizeof buf - 1;
    memcpy(buf, text, n);
    buf[n] = 0;
    if (ui->api->text_width(scale, buf) > max_w) {
        while (n > 0) {
            buf[--n] = 0;
            while (n > 0 && ((unsigned char)buf[n] & 0xC0) == 0x80) buf[--n] = 0;   /* whole UTF-8 characters */
            char tmp[260];
            snprintf(tmp, sizeof tmp, "%s...", buf);
            if (ui->api->text_width(scale, tmp) <= max_w) { ui->api->draw_text(x, y, scale, rgba, tmp); return; }
        }
        return;
    }
    ui->api->draw_text(x, y, scale, rgba, buf);
}

/* Text wrapped at spaces to max_w pixels. Draws it when draw is set; returns its height either way, so a layout can
 * measure first. */
static inline float ctw_ui_text_wrap(const CtwUi* ui, float x, float y, float scale, uint32_t rgba, const char* text,
                                     float max_w, int draw) {
    const float lh = ctw_ui_lh(ui, scale);
    float h = 0.f;
    char line[256];
    while (text && *text) {
        size_t take = 0;
        while (text[take] && text[take] != '\n') {   /* add words while the line still fits */
            size_t next = take;
            while (text[next] == ' ') ++next;
            while (text[next] && text[next] != ' ' && text[next] != '\n') ++next;
            if (next >= sizeof line) break;
            memcpy(line, text, next);
            line[next] = 0;
            if (take > 0 && ui->api->text_width(scale, line) > max_w) break;
            take = next;
            if (ui->api->text_width(scale, line) > max_w) break;   /* one word wider than the line: it gets cut */
        }
        if (take == 0 && *text != '\n') take = strlen(text) < sizeof line - 1 ? strlen(text) : sizeof line - 1;
        memcpy(line, text, take);
        line[take] = 0;
        if (draw) ctw_ui_text_fit(ui, x, y + h, scale, rgba, line, max_w);
        h += lh;
        text += take;
        while (*text == ' ') ++text;
        if (*text == '\n') ++text;
    }
    return h;
}

static inline float ctw_ui_text_y(const CtwUi* ui, float y, float h, float scale) {
    return y + (h - ui->api->line_height(scale)) * 0.5f;
}

/* ---- windows ----------------------------------------------------------------------------------------------------- */

/* Window background with a title bar and an optional dimmer note after the title. Returns 1 when the close
 * button (top right) was clicked. The contents start ctw_ui_title_h() below y. */
static inline int ctw_ui_panel(CtwUi* ui, float x, float y, float w, float h, const char* title, const char* note) {
    const CtwApi* a = ui->api;
    const float th = ctw_ui_title_h(ui), bs = ctw_ui_lh(ui, 1.f) + 12.f;
    ctw_ui_round_rect(a, x, y, w, h, 12.f, CTW_UI_PANEL);
    a->draw_rect(x + 12.f, y, w - 24.f, 2.f, CTW_UI_ACCENT);
    a->draw_rect(x + 16.f, y + th - 1.f, w - 32.f, 1.f, CTW_UI_LINE);
    const float bx = x + w - bs - 12.f, by = y + (th - bs) * 0.5f;
    const float ty = ctw_ui_text_y(ui, y + 1.5f, th, 1.35f), title_w = bx - x - 30.f;
    ctw_ui_text_fit(ui, x + 20.f, ty, 1.35f, CTW_UI_TEXT, title, title_w);
    float used = a->text_width(1.35f, title);
    if (note && used + 14.f < title_w)
        ctw_ui_text_fit(ui, x + 20.f + used + 14.f, ty + ctw_ui_lh(ui, 1.35f) - ctw_ui_lh(ui, 1.f), 1.f, CTW_UI_DIM, note,
                        title_w - used - 14.f);
    int hot = ctw_ui_in(ui, bx, by, bs, bs);
    ctw_ui_round_rect(a, bx, by, bs, bs, bs * 0.5f, hot ? 0xF2675A50u : 0xFFFFFF0Cu);
    a->draw_text(bx + (bs - a->text_width(1.f, "X")) * 0.5f, ctw_ui_text_y(ui, by, bs, 1.f), 1.f, hot ? CTW_UI_TEXT : CTW_UI_DIM, "X");
    return hot && ui->clicked;
}

/* The hint line at the bottom of a window (x, y, w, h: the window). */
static inline void ctw_ui_footer(CtwUi* ui, float x, float y, float w, float h, uint32_t rgba, const char* text) {
    const float fh = ctw_ui_footer_h(ui);
    ui->api->draw_rect(x + 16.f, y + h - fh, w - 32.f, 1.f, CTW_UI_LINE);
    ctw_ui_text_fit(ui, x + 20.f, ctw_ui_text_y(ui, y + h - fh, fh, 0.9f), 0.9f, rgba, text, w - 40.f);
}

/* ---- widgets ----------------------------------------------------------------------------------------------------- */

static inline int ctw_ui_button(CtwUi* ui, float x, float y, float w, float h, const char* label) {
    int hot = ctw_ui_in(ui, x, y, w, h);
    ctw_ui_round_rect(ui->api, x, y, w, h, 8.f, hot ? (ui->down ? 0xF2C230A0u : 0xF2C23058u) : 0xFFFFFF14u);
    float tw = ui->api->text_width(1.f, label);
    if (tw > w - 20.f) tw = w - 20.f;
    ctw_ui_text_fit(ui, x + (w - tw) * 0.5f, ctw_ui_text_y(ui, y, h, 1.f), 1.f, CTW_UI_TEXT, label, w - 20.f);
    return hot && ui->clicked;
}

/* An on/off switch drawn at the right end of a row. Returns 1 when it changed. */
static inline int ctw_ui_switch(CtwUi* ui, float x, float y, float w, float h, const char* label, int* value) {
    const CtwApi* a = ui->api;
    int hot = ctw_ui_in(ui, x, y, w, h);
    if (hot) ctw_ui_round_rect(a, x, y, w, h, 8.f, CTW_UI_HOVER);
    float sh = ctw_ui_lh(ui, 1.f) + 4.f, sw = sh * 1.85f, sx = x + w - sw - 12.f, sy = y + (h - sh) * 0.5f;
    if (label && label[0]) ctw_ui_text_fit(ui, x + 12.f, ctw_ui_text_y(ui, y, h, 1.f), 1.f, CTW_UI_TEXT, label, sx - x - 22.f);
    ctw_ui_round_rect(a, sx, sy, sw, sh, sh * 0.5f, *value ? CTW_UI_ACCENT : CTW_UI_TRACK);
    const float k = sh - 6.f;
    ctw_ui_round_rect(a, *value ? sx + sw - k - 3.f : sx + 3.f, sy + 3.f, k, k, k * 0.5f, 0xFFFFFFFFu);
    if (hot && ui->clicked) { *value = !*value; return 1; }
    return 0;
}

/* A slider row: label left, value right, bar below (make h ctw_ui_slider_h()). Drag or use the wheel over it.
 * Returns 1 when it changed. */
static inline int ctw_ui_slider(CtwUi* ui, int id, float x, float y, float w, float h, const char* label,
                                float* value, float min, float max, float step) {
    const CtwApi* a = ui->api;
    const float lh = ctw_ui_lh(ui, 1.f);
    float old = *value, bx = x + 10.f, bw = w - 20.f, ty = y + (h - lh - 20.f) * 0.5f, by = ty + lh + 11.f;
    int hot = ctw_ui_in(ui, x, y, w, h);
    if (hot) ctw_ui_round_rect(a, x, y, w, h, 8.f, CTW_UI_HOVER);
    char text[48];
    /* as many decimals as the step needs: 0.25 -> "1.25", 1 -> "240" */
    const int decimals = step >= 1.f ? 0 : step >= 0.1f && (float)(int)(step * 10.f + 0.5f) == step * 10.f ? 1 : 2;
    snprintf(text, sizeof text, "%.*f", decimals, *value);
    const float vw = a->text_width(1.f, text);
    ctw_ui_text_fit(ui, x + 10.f, ty, 1.f, CTW_UI_TEXT, label, w - vw - 36.f);
    a->draw_text(x + w - 10.f - vw, ty, 1.f, CTW_UI_ACCENT, text);
    if (hot && ui->clicked && ctw_ui_in(ui, x, by - 9.f, w, 22.f)) ui->drag = id;
    if (ui->drag == id) {
        float t = (ui->mx - bx) / bw;
        t = t < 0.f ? 0.f : t > 1.f ? 1.f : t;
        float v = min + t * (max - min);
        if (step > 0.f) v = min + (float)(int)((v - min) / step + 0.5f) * step;
        *value = v;
    } else if (hot && ui->wheel != 0.f && !ui->wheel_used) {
        *value += ui->wheel * (step > 0.f ? step : (max - min) / 20.f);
        ui->wheel_used = 1;
        if (CTW_MOD_HAS(ui->api, consume_mouse_wheel)) ui->api->consume_mouse_wheel();
    }
    if (*value < min) *value = min;
    if (*value > max) *value = max;
    float t = max > min ? (*value - min) / (max - min) : 0.f;
    ctw_ui_round_rect(a, bx, by, bw, 4.f, 2.f, CTW_UI_TRACK);
    ctw_ui_round_rect(a, bx, by, bw * t, 4.f, 2.f, CTW_UI_ACCENT);
    ctw_ui_round_rect(a, bx + bw * t - 7.f, by - 5.f, 14.f, 14.f, 7.f, ui->drag == id ? CTW_UI_ACCENT : 0xFFFFFFFFu);
    return *value != old;
}

/* A row that picks one of several options: label left, "<  option  >" right. Click the left or right half of the
 * picker, or use the wheel over the row, to step through them. Returns 1 when it changed. */
static inline int ctw_ui_choice(CtwUi* ui, float x, float y, float w, float h, const char* label, int* value,
                                const char* const* options, int count) {
    const CtwApi* a = ui->api;
    if (count <= 0) return 0;
    const int old = *value;
    if (*value < 0 || *value >= count) *value = 0;
    const int hot = ctw_ui_in(ui, x, y, w, h);
    if (hot) ctw_ui_round_rect(a, x, y, w, h, 8.f, CTW_UI_HOVER);
    const float pw = w * 0.42f > 180.f ? 180.f : w * 0.42f, px = x + w - pw - 10.f, ty = ctw_ui_text_y(ui, y, h, 1.f);
    ctw_ui_text_fit(ui, x + 10.f, ty, 1.f, CTW_UI_TEXT, label, px - x - 20.f);
    const float ph = ctw_ui_lh(ui, 1.f) + 6.f, py = y + (h - ph) * 0.5f, aw = a->text_width(1.f, ">") + 14.f;
    ctw_ui_round_rect(a, px, py, pw, ph, 8.f, 0xFFFFFF14u);
    const int left = ctw_ui_in(ui, px, py, pw * 0.5f, ph), right = ctw_ui_in(ui, px + pw * 0.5f, py, pw * 0.5f, ph);
    a->draw_text(px + 7.f, ty, 1.f, left ? CTW_UI_ACCENT : CTW_UI_DIM, "<");
    a->draw_text(px + pw - aw + 7.f, ty, 1.f, right ? CTW_UI_ACCENT : CTW_UI_DIM, ">");
    const char* text = options[*value] ? options[*value] : "";
    float tw = a->text_width(1.f, text);
    if (tw > pw - 2.f * aw) tw = pw - 2.f * aw;
    ctw_ui_text_fit(ui, px + (pw - tw) * 0.5f, ty, 1.f, CTW_UI_TEXT, text, pw - 2.f * aw);
    if (ui->clicked && (left || right)) *value = (*value + (right ? 1 : count - 1)) % count;
    else if (hot && ui->wheel != 0.f && !ui->wheel_used) {
        *value = (*value + (ui->wheel > 0.f ? 1 : count - 1)) % count;
        ui->wheel_used = 1;
        if (CTW_MOD_HAS(ui->api, consume_mouse_wheel)) ui->api->consume_mouse_wheel();
    }
    return *value != old;
}

/* A small coloured tag ("CODE", "ERROR"). Returns its width, so tags can be placed side by side. */
static inline float ctw_ui_badge(const CtwUi* ui, float x, float y, uint32_t rgba, const char* text) {
    const float w = ui->api->text_width(0.75f, text) + 12.f, h = ctw_ui_lh(ui, 0.75f) + 4.f;
    ctw_ui_round_rect(ui->api, x, y, w, h, h * 0.5f, (rgba & 0xFFFFFF00u) | 0x30u);
    ui->api->draw_text(x + 6.f, y + 2.f, 0.75f, rgba, text);
    return w;
}

/* A scrolling area. Call before drawing its contents at y - *scroll, then ctw_ui_scroll_end.
 * *scroll eases toward *target; content_h is the full height of the contents. */
typedef struct CtwUiScroll { float pos, target; } CtwUiScroll;

static inline void ctw_ui_scroll_begin(CtwUi* ui, int id, CtwUiScroll* s, float x, float y, float w, float h, float content_h) {
    float max = content_h > h ? content_h - h : 0.f;
    if (ctw_ui_in(ui, x, y, w, h) && ui->wheel != 0.f && !ui->wheel_used) {
        s->target -= ui->wheel * 90.f;
        ui->wheel_used = 1;
        if (CTW_MOD_HAS(ui->api, consume_mouse_wheel)) ui->api->consume_mouse_wheel();
    }
    /* scrollbar */
    if (max > 0.f) {
        float bar_h = h * h / content_h, track = h - bar_h, bx = x + w - 6.f;
        if (bar_h < 30.f) { bar_h = 30.f; track = h - bar_h; }
        float by = y + (s->target / max) * track;
        int hot = ctw_ui_in(ui, bx - 6.f, y, 18.f, h);
        if (hot && ui->clicked) { ui->drag = id; ui->drag_offset = ctw_ui_in(ui, bx - 6.f, by, 18.f, bar_h) ? ui->my - by : bar_h * 0.5f; }
        if (ui->drag == id && track > 0.f) s->target = (ui->my - ui->drag_offset - y) / track * max;
        if (s->target < 0.f) s->target = 0.f;
        if (s->target > max) s->target = max;
        by = y + (s->pos / max) * track;
        ctw_ui_round_rect(ui->api, bx, y, 4.f, h, 2.f, 0xFFFFFF0Cu);
        ctw_ui_round_rect(ui->api, bx, by, 4.f, bar_h, 2.f, hot || ui->drag == id ? CTW_UI_ACCENT : 0xFFFFFF50u);
    } else {
        s->target = 0.f;
    }
    if (s->target < 0.f) s->target = 0.f;
    if (s->target > max) s->target = max;
    s->pos += (s->target - s->pos) * 0.35f;
    if (s->pos - s->target < 0.5f && s->target - s->pos < 0.5f) s->pos = s->target;
    ui->api->set_clip(x, y, w, h);
}

static inline void ctw_ui_scroll_end(CtwUi* ui) { ui->api->set_clip(0.f, 0.f, 0.f, 0.f); }

/* A one-line text field (make h ctw_ui_row_h()). Click to focus; while focused it takes the typed text (turn the game's keyboard off
 * yourself if the player should stand still). Returns 1 when the text changed. */
static inline int ctw_ui_textbox(CtwUi* ui, float x, float y, float w, float h, char* buf, size_t size,
                                 int* focused, const char* placeholder) {
    const CtwApi* a = ui->api;
    int hot = ctw_ui_in(ui, x, y, w, h), changed = 0;
    if (ui->clicked) *focused = hot;
    ctw_ui_round_rect(a, x, y, w, h, 8.f, *focused ? 0xFFFFFF22u : hot ? 0xFFFFFF18u : 0xFFFFFF10u);
    if (*focused) a->draw_rect(x + 8.f, y + h - 2.f, w - 16.f, 2.f, CTW_UI_ACCENT);
    if (*focused) {
        const char* t = a->text_input();
        for (; t && *t; ++t) {
            size_t n = strlen(buf);
            if (*t == '\b') {
                while (n > 0 && ((unsigned char)buf[n - 1] & 0xC0) == 0x80) buf[--n] = 0;   /* UTF-8 continuation */
                if (n > 0) buf[--n] = 0;
                changed = 1;
            } else if ((unsigned char)*t >= 0x20 && n + 1 < size) {
                buf[n] = *t;
                buf[n + 1] = 0;
                changed = 1;
            }
        }
    }
    float ty = ctw_ui_text_y(ui, y, h, 1.f), tw = a->text_width(1.f, buf);
    if (tw > w - 24.f) tw = w - 24.f;
    if (buf[0]) ctw_ui_text_fit(ui, x + 10.f, ty, 1.f, CTW_UI_TEXT, buf, w - 24.f);
    else if (placeholder) ctw_ui_text_fit(ui, x + 10.f, ty, 1.f, 0x808088FFu, placeholder, w - 20.f);
    if (*focused && (a->frame_count() / 15) % 2 == 0) a->draw_rect(x + 11.f + tw, ty, 2.f, ctw_ui_lh(ui, 1.f), CTW_UI_TEXT);
    return changed;
}

#endif /* CTW_UI_H */
