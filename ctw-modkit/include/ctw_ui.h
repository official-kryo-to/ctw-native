/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kryo.to
 * See LICENSE in the repository root.
 *
 * GTA: Chinatown Wars PC port - small immediate-mode UI for mods (header only, plain C).
 *
 * Call everything from an on_draw_hud callback. One CtwUi per window keeps the little state widgets need
 * (the slider being dragged, smooth scrolling). Needs mod API version 3.
 *
 *   static CtwUi ui;
 *   ctw_ui_begin(&ui, api);
 *   ctw_ui_panel(&ui, x, y, w, h, "MY WINDOW");
 *   if (ctw_ui_button(&ui, x + 10, y + 50, 120, 30, "Click me")) ...;
 */
#ifndef CTW_UI_H
#define CTW_UI_H

#include "ctw_mod.h"
#include <stdio.h>
#include <string.h>

#define CTW_UI_ACCENT 0xE0B020FFu
#define CTW_UI_TEXT 0xFFFFFFFFu
#define CTW_UI_DIM 0xA8A8B0FFu
#define CTW_UI_PANEL 0x101014E6u
#define CTW_UI_ROW 0xFFFFFF10u
#define CTW_UI_HOVER 0xFFFFFF22u

typedef struct CtwUi {
    const CtwApi* api;
    float mx, my, wheel;
    int clicked, down;
    int drag;              /* id of the widget being dragged, 0 = none */
    float drag_offset;
    int wheel_used;        /* a scroll area took this frame's wheel */
} CtwUi;

static inline int ctw_ui_in(const CtwUi* ui, float x, float y, float w, float h) {
    return ui->mx >= x && ui->mx < x + w && ui->my >= y && ui->my < y + h;
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
            char tmp[260];
            snprintf(tmp, sizeof tmp, "%s...", buf);
            if (ui->api->text_width(scale, tmp) <= max_w) { ui->api->draw_text(x, y, scale, rgba, tmp); return; }
        }
    }
    ui->api->draw_text(x, y, scale, rgba, buf);
}

static inline float ctw_ui_text_y(const CtwUi* ui, float y, float h, float scale) {
    return y + (h - ui->api->line_height(scale)) * 0.5f;
}

/* Window background with a title bar and an optional dimmer note after the title. Returns 1 when the close
 * button (top right) was clicked. */
static inline int ctw_ui_panel(CtwUi* ui, float x, float y, float w, float h, const char* title, const char* note) {
    const CtwApi* a = ui->api;
    a->draw_rect(x, y, w, h, CTW_UI_PANEL);
    a->draw_rect(x, y, w, 3.f, CTW_UI_ACCENT);
    a->draw_text(x + 14.f, y + 12.f, 1.35f, CTW_UI_TEXT, title);
    if (note) a->draw_text(x + 26.f + a->text_width(1.35f, title), y + 12.f + a->line_height(1.35f) - a->line_height(1.f) - 1.f,
                           1.f, CTW_UI_DIM, note);
    float bx = x + w - 34.f, by = y + 10.f;
    int hot = ctw_ui_in(ui, bx, by, 24.f, 24.f);
    if (hot) a->draw_rect(bx, by, 24.f, 24.f, CTW_UI_HOVER);
    a->draw_text(bx + 7.f, ctw_ui_text_y(ui, by, 24.f, 1.f), 1.f, hot ? CTW_UI_TEXT : CTW_UI_DIM, "X");
    return hot && ui->clicked;
}

static inline int ctw_ui_button(CtwUi* ui, float x, float y, float w, float h, const char* label) {
    int hot = ctw_ui_in(ui, x, y, w, h);
    ui->api->draw_rect(x, y, w, h, hot ? (ui->down ? 0xE0B02090u : 0xE0B02050u) : 0xFFFFFF18u);
    float tw = ui->api->text_width(1.f, label);
    ui->api->draw_text(x + (w - tw) * 0.5f, ctw_ui_text_y(ui, y, h, 1.f), 1.f, CTW_UI_TEXT, label);
    return hot && ui->clicked;
}

/* An on/off switch drawn at the right end of a row. Returns 1 when it changed. */
static inline int ctw_ui_switch(CtwUi* ui, float x, float y, float w, float h, const char* label, int* value) {
    const CtwApi* a = ui->api;
    int hot = ctw_ui_in(ui, x, y, w, h);
    if (hot) a->draw_rect(x, y, w, h, CTW_UI_HOVER);
    ctw_ui_text_fit(ui, x + 10.f, ctw_ui_text_y(ui, y, h, 1.f), 1.f, CTW_UI_TEXT, label, w - 80.f);
    float sw = 40.f, sh = 20.f, sx = x + w - sw - 10.f, sy = y + (h - sh) * 0.5f;
    a->draw_rect(sx, sy, sw, sh, *value ? 0xE0B020D0u : 0x50505AFFu);
    a->draw_rect(*value ? sx + sw - sh + 3.f : sx + 3.f, sy + 3.f, sh - 6.f, sh - 6.f, 0xFFFFFFFFu);
    if (hot && ui->clicked) { *value = !*value; return 1; }
    return 0;
}

/* A slider row: label left, value right, bar below. Drag or use the wheel over it. Returns 1 when it changed. */
static inline int ctw_ui_slider(CtwUi* ui, int id, float x, float y, float w, float h, const char* label,
                                float* value, float min, float max, float step) {
    const CtwApi* a = ui->api;
    float old = *value, bx = x + 10.f, bw = w - 20.f, by = y + h - 12.f;
    int hot = ctw_ui_in(ui, x, y, w, h);
    if (hot) a->draw_rect(x, y, w, h, CTW_UI_HOVER);
    char text[48];
    snprintf(text, sizeof text, "%.2f", *value);
    ctw_ui_text_fit(ui, x + 10.f, y + 5.f, 1.f, CTW_UI_TEXT, label, w - 90.f);
    a->draw_text(x + w - 10.f - a->text_width(1.f, text), y + 5.f, 1.f, CTW_UI_ACCENT, text);
    if (hot && ui->clicked && ctw_ui_in(ui, x, by - 8.f, w, 20.f)) ui->drag = id;
    if (ui->drag == id) {
        float t = (ui->mx - bx) / bw;
        t = t < 0.f ? 0.f : t > 1.f ? 1.f : t;
        float v = min + t * (max - min);
        if (step > 0.f) v = min + (float)(int)((v - min) / step + 0.5f) * step;
        *value = v;
    } else if (hot && ui->wheel != 0.f && !ui->wheel_used) {
        *value += ui->wheel * (step > 0.f ? step : (max - min) / 20.f);
        ui->wheel_used = 1;
    }
    if (*value < min) *value = min;
    if (*value > max) *value = max;
    float t = max > min ? (*value - min) / (max - min) : 0.f;
    a->draw_rect(bx, by, bw, 4.f, 0x50505AFFu);
    a->draw_rect(bx, by, bw * t, 4.f, CTW_UI_ACCENT);
    a->draw_rect(bx + bw * t - 5.f, by - 5.f, 10.f, 14.f, ui->drag == id ? CTW_UI_ACCENT : 0xFFFFFFFFu);
    return *value != old;
}

/* A scrolling area. Call before drawing its contents at y - *scroll, then ctw_ui_scroll_end.
 * *scroll eases toward *target; content_h is the full height of the contents. */
typedef struct CtwUiScroll { float pos, target; } CtwUiScroll;

static inline void ctw_ui_scroll_begin(CtwUi* ui, int id, CtwUiScroll* s, float x, float y, float w, float h, float content_h) {
    float max = content_h > h ? content_h - h : 0.f;
    if (ctw_ui_in(ui, x, y, w, h) && ui->wheel != 0.f && !ui->wheel_used) {
        s->target -= ui->wheel * 90.f;
        ui->wheel_used = 1;
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
        ui->api->draw_rect(bx, y, 4.f, h, 0xFFFFFF10u);
        ui->api->draw_rect(bx, by, 4.f, bar_h, hot || ui->drag == id ? CTW_UI_ACCENT : 0xFFFFFF60u);
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

/* A one-line text field. Click to focus; while focused it takes the typed text (turn the game's keyboard off
 * yourself if the player should stand still). Returns 1 when the text changed. */
static inline int ctw_ui_textbox(CtwUi* ui, float x, float y, float w, float h, char* buf, size_t size,
                                 int* focused, const char* placeholder) {
    const CtwApi* a = ui->api;
    int hot = ctw_ui_in(ui, x, y, w, h), changed = 0;
    if (ui->clicked) *focused = hot;
    a->draw_rect(x, y, w, h, *focused ? 0xFFFFFF30u : hot ? 0xFFFFFF22u : 0xFFFFFF16u);
    if (*focused) a->draw_rect(x, y + h - 2.f, w, 2.f, CTW_UI_ACCENT);
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
    float ty = ctw_ui_text_y(ui, y, h, 1.f);
    if (buf[0]) a->draw_text(x + 10.f, ty, 1.f, CTW_UI_TEXT, buf);
    else if (placeholder) a->draw_text(x + 10.f, ty, 1.f, 0x808088FFu, placeholder);
    if (*focused && (a->frame_count() / 15) % 2 == 0)
        a->draw_rect(x + 11.f + a->text_width(1.f, buf), y + 7.f, 2.f, h - 14.f, CTW_UI_TEXT);
    return changed;
}

#endif /* CTW_UI_H */
