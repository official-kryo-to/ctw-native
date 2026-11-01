/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kryo.to
 * See LICENSE in the repository root.
 *
 * GTA: Chinatown Wars PC port - small immediate-mode UI for mods (header only, plain C).
 *
 * The Porter's interface, copied: Kryoto's design tokens (porter/src/styles.css, the OLED palette with pill radii)
 * and its components (porter/src/ui: Label, Caption, Button, Check, Tag; the card, title bar and field of
 * porter/src/App.tsx), in the same CSS pixel sizes and set in the same typeface (draw_ui_text). A game without the
 * typeface draws the same layout in its own font.
 *
 * Call everything from an on_draw_hud callback. One CtwUi per window keeps the little state widgets need
 * (the slider being dragged, smooth scrolling). ctw_ui_side_area gives a window a place clear of the game's HUD and
 * the mod menu.
 *
 *   static CtwUi ui;
 *   ctw_ui_begin(&ui, api);
 *   CtwUiRect r = ctw_ui_side_area(api, 480.f);
 *   ctw_ui_panel(&ui, r.x, r.y, r.w, r.h, "My window", NULL);
 *   if (ctw_ui_button(&ui, r.x + 24, r.y + ctw_ui_title_h(&ui) + 24, 140, ctw_ui_row_h(&ui), "Click me")) ...;
 */
#ifndef CTW_UI_H
#define CTW_UI_H

#include "ctw_mod.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

/* ---- the tokens: Kryoto's OLED palette (oklch in styles.css, here in sRGB), RGBA --------------------------------- */
#define CTW_UI_BACKGROUND 0x000000FFu     /* oklch(0.02 0 0) */
#define CTW_UI_FOREGROUND 0xF8F8F8FFu     /* oklch(0.98 0 0) */
#define CTW_UI_CARD 0x010101F2u           /* oklch(0.06 0 0), as the Porter's panel: bg-card/95 */
#define CTW_UI_PRIMARY 0xF8F8F8FFu        /* oklch(0.98 0 0) */
#define CTW_UI_PRIMARY_FG 0x000000FFu     /* oklch(0.05 0 0) */
#define CTW_UI_SECONDARY 0x060606FFu      /* oklch(0.12 0 0) */
#define CTW_UI_MUTED 0x090909FFu          /* oklch(0.14 0 0) */
#define CTW_UI_MUTED_FG 0x808080FFu       /* oklch(0.6 0 0) */
#define CTW_UI_BORDER 0x121212FFu         /* oklch(0.18 0 0) */
#define CTW_UI_INPUT 0x0D0D0DFFu          /* oklch(0.16 0 0) */
#define CTW_UI_RING 0x9E9E9EFFu           /* oklch(0.7 0 0) */
#define CTW_UI_DESTRUCTIVE 0xCF4040FFu    /* oklch(0.58 0.18 25) */
#define CTW_UI_WARNING 0xE1A536FFu        /* oklch(0.76 0.14 78) */
#define CTW_UI_SUCCESS 0x4DBF74FFu        /* oklch(0.72 0.15 152) */
#define CTW_UI_INFO 0x52B5F4FFu           /* oklch(0.74 0.13 240) */
#define CTW_UI_RADIUS 16.f                /* --kryo-radius (data-radius="pill"); pills are fully round */
/* the earlier names, on the same tokens */
#define CTW_UI_ACCENT CTW_UI_PRIMARY
#define CTW_UI_TEXT CTW_UI_FOREGROUND
#define CTW_UI_DIM CTW_UI_MUTED_FG
#define CTW_UI_ERROR CTW_UI_DESTRUCTIVE
#define CTW_UI_OK CTW_UI_SUCCESS
#define CTW_UI_PANEL CTW_UI_CARD
#define CTW_UI_SURFACE 0x00000066u        /* bg-background/40 */
#define CTW_UI_ROW 0x00000000u
#define CTW_UI_HOVER CTW_UI_SECONDARY
#define CTW_UI_LINE CTW_UI_BORDER
#define CTW_UI_TRACK CTW_UI_BORDER
#define CTW_UI_GAP 12.f          /* between windows, and from the screen edge */

/* text styles (draw_ui_text) and the Tailwind sizes the Porter uses */
#define CTW_UI_BOLD 1
#define CTW_UI_UPPER 2
#define CTW_UI_TEXT_XS 12.f      /* text-xs */
#define CTW_UI_TEXT_SM 14.f      /* text-sm */

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

static inline uint32_t ctw_ui_alpha(uint32_t rgba, float a) {   /* a colour at a fraction of its opacity (bg-x/10) */
    float v = (float)(rgba & 0xFFu) * a + 0.5f;
    return (rgba & 0xFFFFFF00u) | (uint32_t)(v > 255.f ? 255.f : v);
}

/* ---- shapes ------------------------------------------------------------------------------------------------------- */

/* The insets of a rounded rectangle's rows, with a partly covered pixel at each end so the corners are smooth. */
static inline void ctw_ui__span(const CtwApi* a, float x, float y, float w, float inset, uint32_t rgba) {
    const float whole = ceilf(inset), part = whole - inset;
    if (w - 2.f * whole > 0.f) a->draw_rect(x + whole, y, w - 2.f * whole, 1.f, rgba);
    if (part > 0.01f && whole >= 1.f) {
        const uint32_t edge = ctw_ui_alpha(rgba, part);
        a->draw_rect(x + whole - 1.f, y, 1.f, 1.f, edge);
        a->draw_rect(x + w - whole, y, 1.f, 1.f, edge);
    }
}
static inline float ctw_ui__inset(float r, int row) {
    const float dy = r - (float)row - 0.5f;
    const float inset = r - sqrtf(r * r - dy * dy);
    return inset < 0.f ? 0.f : inset;
}

/* A rectangle with rounded corners (radius in pixels), drawn as non-overlapping rows so translucent colours stay even. */
static inline void ctw_ui_round_rect(const CtwApi* a, float x, float y, float w, float h, float r, uint32_t rgba) {
    if (w <= 0.f || h <= 0.f) return;
    x = floorf(x + 0.5f); y = floorf(y + 0.5f); w = floorf(w + 0.5f); h = floorf(h + 0.5f);
    if (r > w * 0.5f) r = w * 0.5f;
    if (r > h * 0.5f) r = h * 0.5f;
    const int n = (int)ceilf(r);
    if (n < 1) { a->draw_rect(x, y, w, h, rgba); return; }
    for (int i = 0; i < n && (float)i < h * 0.5f; ++i) {
        const float inset = ctw_ui__inset(r, i);
        ctw_ui__span(a, x, y + (float)i, w, inset, rgba);
        if (y + h - (float)i - 1.f > y + (float)i) ctw_ui__span(a, x, y + h - (float)i - 1.f, w, inset, rgba);
    }
    if (h - 2.f * (float)n > 0.f) a->draw_rect(x, y + (float)n, w, h - 2.f * (float)n, rgba);
}

/* A border (CSS `border`, t pixels wide) around a rounded rectangle, without its inside. */
static inline void ctw_ui_round_frame(const CtwApi* a, float x, float y, float w, float h, float r, float t, uint32_t rgba) {
    if (w <= 0.f || h <= 0.f) return;
    x = floorf(x + 0.5f); y = floorf(y + 0.5f); w = floorf(w + 0.5f); h = floorf(h + 0.5f);
    if (r > w * 0.5f) r = w * 0.5f;
    if (r > h * 0.5f) r = h * 0.5f;
    const float ri = r - t > 0.f ? r - t : 0.f;
    const int n = (int)ceilf(r) > (int)t ? (int)ceilf(r) : (int)t;
    for (int i = 0; i < n && (float)i < h * 0.5f; ++i) {
        for (int side = 0; side < 2; ++side) {
            const float yy = side ? y + h - (float)i - 1.f : y + (float)i;
            if (side && yy <= y + (float)i) break;
            const float outer = (float)i < r ? ctw_ui__inset(r, i) : 0.f;
            if ((float)i < t) { ctw_ui__span(a, x, yy, w, outer, rgba); continue; }
            /* the inner edge: the inner curve, offset by the border */
            const int j = i - (int)t;
            const float inner = t + ((float)j < ri ? ctw_ui__inset(ri, j) : 0.f);
            const float ow = ceilf(inner) - floorf(outer);
            a->draw_rect(x + floorf(outer), yy, ow, 1.f, rgba);
            a->draw_rect(x + w - floorf(outer) - ow, yy, ow, 1.f, rgba);
        }
    }
    if (h - 2.f * (float)n > 0.f) {
        a->draw_rect(x, y + (float)n, t, h - 2.f * (float)n, rgba);
        a->draw_rect(x + w - t, y + (float)n, t, h - 2.f * (float)n, rgba);
    }
}

/* A straight stroke (an icon's line), round-capped, w pixels wide: rows of coverage along it. */
static inline void ctw_ui_line(const CtwApi* a, float x0, float y0, float x1, float y1, float w, uint32_t rgba) {
    const float dx = x1 - x0, dy = y1 - y0, len = sqrtf(dx * dx + dy * dy);
    if (len <= 0.f) return;
    const float top = (y0 < y1 ? y0 : y1) - w, bottom = (y0 > y1 ? y0 : y1) + w;
    const float left = (x0 < x1 ? x0 : x1) - w, right = (x0 > x1 ? x0 : x1) + w;
    for (float py = floorf(top); py < bottom; py += 1.f)
        for (float px = floorf(left); px < right; px += 1.f) {
            /* coverage of this pixel: 4 x 4 samples against the capsule */
            int hits = 0;
            for (int s = 0; s < 16; ++s) {
                const float sx = px + ((float)(s & 3) + 0.5f) / 4.f, sy = py + ((float)(s >> 2) + 0.5f) / 4.f;
                float t = ((sx - x0) * dx + (sy - y0) * dy) / (len * len);
                t = t < 0.f ? 0.f : t > 1.f ? 1.f : t;
                const float ex = sx - (x0 + dx * t), ey = sy - (y0 + dy * t);
                hits += ex * ex + ey * ey <= w * w * 0.25f;
            }
            if (hits) a->draw_rect(px, py, 1.f, 1.f, ctw_ui_alpha(rgba, (float)hits / 16.f));
        }
}

/* lucide's icons, as the Porter draws them (24-unit view box, stroke 2 unless given) */
static inline void ctw_ui_icon_x(const CtwApi* a, float x, float y, float size, uint32_t rgba) {   /* X */
    const float k = size / 24.f;
    ctw_ui_line(a, x + 18.f * k, y + 6.f * k, x + 6.f * k, y + 18.f * k, 2.f * k, rgba);
    ctw_ui_line(a, x + 6.f * k, y + 6.f * k, x + 18.f * k, y + 18.f * k, 2.f * k, rgba);
}
static inline void ctw_ui_icon_check(const CtwApi* a, float x, float y, float size, float stroke, uint32_t rgba) {
    const float k = size / 24.f;   /* Check: M20 6 9 17l-5-5 */
    ctw_ui_line(a, x + 20.f * k, y + 6.f * k, x + 9.f * k, y + 17.f * k, stroke * k, rgba);
    ctw_ui_line(a, x + 9.f * k, y + 17.f * k, x + 4.f * k, y + 12.f * k, stroke * k, rgba);
}
static inline void ctw_ui_icon_chevron(const CtwApi* a, float x, float y, float size, int right, uint32_t rgba) {
    const float k = size / 24.f;   /* ChevronLeft: m15 18-6-6 6-6; ChevronRight: m9 18 6-6-6-6 */
    const float tip = right ? 15.f : 9.f, back = right ? 9.f : 15.f;
    ctw_ui_line(a, x + back * k, y + 18.f * k, x + tip * k, y + 12.f * k, 2.f * k, rgba);
    ctw_ui_line(a, x + tip * k, y + 12.f * k, x + back * k, y + 6.f * k, 2.f * k, rgba);
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

/* ---- type: px is the CSS font size, tracking the letter spacing in em -------------------------------------------- */

static inline void ctw_ui_type(const CtwUi* ui, float x, float y, float px, float tracking, int style, uint32_t rgba,
                               const char* text) {
    if (!text) return;
    if (CTW_MOD_HAS(ui->api, draw_ui_text)) { ui->api->draw_ui_text(x, y, px, tracking, style, rgba, text); return; }
    const float s = px / 12.f;   /* an older game: its own font */
    ui->api->draw_text(x, y + (px * 1.5f - ui->api->line_height(s)) * 0.5f, s, rgba, text);
}
static inline float ctw_ui_type_w(const CtwUi* ui, float px, float tracking, int style, const char* text) {
    if (!text) return 0.f;
    if (CTW_MOD_HAS(ui->api, ui_text_width)) return ui->api->ui_text_width(px, tracking, style, text);
    return ui->api->text_width(px / 12.f, text);
}
/* `truncate`: cut to max_w pixels with an ellipsis */
static inline void ctw_ui_type_fit(const CtwUi* ui, float x, float y, float px, float tracking, int style, uint32_t rgba,
                                   const char* text, float max_w) {
    char buf[256];
    size_t n = strlen(text);
    if (n >= sizeof buf) n = sizeof buf - 1;
    memcpy(buf, text, n);
    buf[n] = 0;
    if (ctw_ui_type_w(ui, px, tracking, style, buf) > max_w) {
        while (n > 0) {
            buf[--n] = 0;
            while (n > 0 && ((unsigned char)buf[n] & 0xC0) == 0x80) buf[--n] = 0;   /* whole UTF-8 characters */
            while (n > 0 && buf[n - 1] == ' ') buf[--n] = 0;
            char tmp[264];
            snprintf(tmp, sizeof tmp, "%s\xE2\x80\xA6", buf);
            if (ctw_ui_type_w(ui, px, tracking, style, tmp) <= max_w) { ctw_ui_type(ui, x, y, px, tracking, style, rgba, tmp); return; }
        }
        return;
    }
    ctw_ui_type(ui, x, y, px, tracking, style, rgba, buf);
}
/* the top of a px-sized line box centred in a box of height h (flex items-center) */
static inline float ctw_ui_type_y(float y, float h, float px) { return y + (h - px * 1.5f) * 0.5f; }

/* <Label>: the section heading, text-xs uppercase tracking-[0.25em] text-primary */
static inline void ctw_ui_label(const CtwUi* ui, float x, float y, const char* text) {
    ctw_ui_type(ui, x, y, CTW_UI_TEXT_XS, 0.25f, CTW_UI_UPPER, CTW_UI_PRIMARY, text);
}
/* <Caption>: text-[10px] uppercase tracking-wider text-muted-foreground */
static inline void ctw_ui_caption(const CtwUi* ui, float x, float y, const char* text) {
    ctw_ui_type(ui, x, y, 10.f, 0.05f, CTW_UI_UPPER, CTW_UI_MUTED_FG, text);
}

/* ---- sizes (the Porter's Tailwind classes, in CSS pixels) ------------------------------------------------------- */

/* the line box of the old scale-based calls: scale 1 is text-xs */
static inline float ctw_ui_lh(const CtwUi* ui, float scale) { (void)ui; return CTW_UI_TEXT_XS * scale * 1.5f; }
/* A one-line row (checks, buttons, fields): h-9. */
static inline float ctw_ui_row_h(const CtwUi* ui) { (void)ui; return 36.f; }
/* A slider row: its label line (text-xs) and the track under it. */
static inline float ctw_ui_slider_h(const CtwUi* ui) { (void)ui; return 48.f; }
/* A window's title bar: h-10. */
static inline float ctw_ui_title_h(const CtwUi* ui) { (void)ui; return 40.f; }
/* The hint line at the bottom of a window. */
static inline float ctw_ui_footer_h(const CtwUi* ui) { (void)ui; return 36.f; }

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
    r.y = 16.f + a->line_height(1.5f) + CTW_UI_GAP;   /* the clock: 1.5x game text at y 16 */
    r.h = ctw_ui_screen_bottom(a) - r.y;
    return r;
}

/* ---- text (the earlier calls: scale 1 is text-xs) ---------------------------------------------------------------- */

static inline void ctw_ui_text(const CtwUi* ui, float x, float y, float scale, uint32_t rgba, const char* text) {
    ctw_ui_type(ui, x, y, CTW_UI_TEXT_XS * scale, 0.f, 0, rgba, text);
}

/* Text cut to max_w pixels with an ellipsis */
static inline void ctw_ui_text_fit(const CtwUi* ui, float x, float y, float scale, uint32_t rgba, const char* text, float max_w) {
    ctw_ui_type_fit(ui, x, y, CTW_UI_TEXT_XS * scale, 0.f, 0, rgba, text, max_w);
}

/* Text wrapped at spaces to max_w pixels, px-sized lines leading x px apart (leading-relaxed: 1.625). Draws it when
 * draw is set; returns its height either way, so a layout can measure first. */
static inline float ctw_ui_type_wrap(const CtwUi* ui, float x, float y, float px, float leading, uint32_t rgba,
                                     const char* text, float max_w, int draw) {
    float lh = px * leading;
    if (!CTW_MOD_HAS(ui->api, draw_ui_text)) {   /* an older game's own font: its lines never closer than it is tall */
        const float own = ui->api->line_height(px / 12.f);
        if (own > lh) lh = own;
    }
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
            if (take > 0 && ctw_ui_type_w(ui, px, 0.f, 0, line) > max_w) break;
            take = next;
            if (ctw_ui_type_w(ui, px, 0.f, 0, line) > max_w) break;   /* one word wider than the line: it gets cut */
        }
        if (take == 0 && *text != '\n') take = strlen(text) < sizeof line - 1 ? strlen(text) : sizeof line - 1;
        memcpy(line, text, take);
        line[take] = 0;
        if (draw) ctw_ui_type_fit(ui, x, y + h + (lh - px * 1.5f) * 0.5f, px, 0.f, 0, rgba, line, max_w);
        h += lh;
        text += take;
        while (*text == ' ') ++text;
        if (*text == '\n') ++text;
    }
    return h;
}
static inline float ctw_ui_text_wrap(const CtwUi* ui, float x, float y, float scale, uint32_t rgba, const char* text,
                                     float max_w, int draw) {
    return ctw_ui_type_wrap(ui, x, y, CTW_UI_TEXT_XS * scale, 1.5f, rgba, text, max_w, draw);
}

/* A notice (the Porter's NoGameNotice): kryo-radius border colour/50, bg colour/10, px-4 py-2, text-[11px] in the
 * colour. Returns its height; draws it when draw is set. */
static inline float ctw_ui_notice(const CtwUi* ui, float x, float y, float w, uint32_t rgba, const char* text, int draw) {
    const float th = ctw_ui_type_wrap(ui, 0.f, 0.f, 11.f, 1.5f, rgba, text, w - 32.f, 0), h = th + 16.f + 2.f;
    if (draw) {
        ctw_ui_round_rect(ui->api, x, y, w, h, CTW_UI_RADIUS, ctw_ui_alpha(rgba, 0.1f));
        ctw_ui_round_frame(ui->api, x, y, w, h, CTW_UI_RADIUS, 1.f, ctw_ui_alpha(rgba, 0.5f));
        ctw_ui_type_wrap(ui, x + 16.f, y + 9.f, 11.f, 1.5f, rgba, text, w - 32.f, 1);
    }
    return h;
}

static inline float ctw_ui_text_y(const CtwUi* ui, float y, float h, float scale) {
    return y + (h - ctw_ui_lh(ui, scale)) * 0.5f;
}

/* ---- windows ----------------------------------------------------------------------------------------------------- */

/* box-shadow: 0 dy blur spread rgba, outside the box only (as CSS draws an outer shadow). The blur is a Gaussian of
 * sigma blur / 2: nested layers whose combined opacity across each edge follows its integral. */
static inline void ctw_ui_shadow(const CtwApi* a, float x, float y, float w, float h, float r, float dy, float blur,
                                 float spread, uint32_t rgba) {
    enum { LAYERS = 16 };
    const float sigma = blur * 0.5f, peak = (float)(rgba & 0xFFu) / 255.f;
    const float sx = x - spread, sy = y + dy - spread, sw = w + 2.f * spread, sh = h + 2.f * spread;
    const float sr = r + spread > 0.f ? r + spread : 0.f;
    float target[LAYERS + 1];
    for (int i = 0; i <= LAYERS; ++i) {   /* layer i reaches g pixels out of the spread box */
        const float g = -2.f * sigma + 4.f * sigma * ((float)i + 0.5f) / (float)LAYERS;
        target[i] = i == LAYERS ? 0.f : peak * 0.5f * erfcf(g / (sigma * 1.41421356f));
    }
    const float bands[4][4] = {{x - 3.f * blur, y, 3.f * blur, h + 3.f * blur}, {x + w, y, 3.f * blur, h + 3.f * blur},
                               {x, y + h, w, 3.f * blur}, {x - 3.f * blur, y - 3.f * blur, w + 6.f * blur, 3.f * blur}};
    for (int b = 0; b < 4; ++b) {
        a->set_clip(bands[b][0], bands[b][1], bands[b][2], bands[b][3]);
        for (int i = LAYERS - 1; i >= 0; --i) {   /* outermost first: (1 - a_i) = (1 - T_i) / (1 - T_i+1) */
            const float g = -2.f * sigma + 4.f * sigma * (float)(i + 1) / (float)LAYERS;
            const float layer = 1.f - (1.f - target[i]) / (1.f - target[i + 1]);
            if (layer <= 0.f || sw + 2.f * g <= 0.f || sh + 2.f * g <= 0.f) continue;
            ctw_ui_round_rect(a, sx - g, sy - g, sw + 2.f * g, sh + 2.f * g, sr + g > 0.f ? sr + g : 0.f,
                              (rgba & 0xFFFFFF00u) | (uint32_t)(layer * 255.f + 0.5f));
        }
    }
    a->set_clip(0.f, 0.f, 0.f, 0.f);
}

/* The card (kryo-radius border border-border bg-card/95 shadow-2xl shadow-black/70): box-shadow 0 25px 50px -12px. */
static inline void ctw_ui_card(const CtwApi* a, float x, float y, float w, float h) {
    ctw_ui_shadow(a, x, y, w, h, CTW_UI_RADIUS, 25.f, 50.f, -12.f, 0x000000B3u);
    ctw_ui_round_rect(a, x, y, w, h, CTW_UI_RADIUS, CTW_UI_CARD);
    ctw_ui_round_frame(a, x, y, w, h, CTW_UI_RADIUS, 1.f, CTW_UI_BORDER);
}

/* The Porter's title bar (h-10 border-b, pl-4 pr-2 gap-3): the title in text-[11px] bold uppercase tracking-[0.3em],
 * the note in a pill (border px-2 py-0.5 text-[10px] tracking-wider muted), a close button (size-7 pill, X size-3.5).
 * Returns 1 when the close button was clicked. The contents start ctw_ui_title_h() below y. */
static inline int ctw_ui_panel(CtwUi* ui, float x, float y, float w, float h, const char* title, const char* note) {
    const CtwApi* a = ui->api;
    const float th = ctw_ui_title_h(ui), bs = 28.f;
    if (CTW_MOD_HAS(a, claim_mouse_rect)) a->claim_mouse_rect(x, y, w, h);   /* clicks here are not gunfire */
    ctw_ui_card(a, x, y, w, h);
    a->draw_rect(x + 1.f, y + th - 1.f, w - 2.f, 1.f, CTW_UI_BORDER);
    const float bx = x + w - 8.f - bs, by = y + (th - bs) * 0.5f;
    float tx = x + 16.f;
    const float title_w = bx - 12.f - tx;
    ctw_ui_type_fit(ui, tx, ctw_ui_type_y(y, th, 11.f), 11.f, 0.3f, CTW_UI_BOLD | CTW_UI_UPPER, CTW_UI_FOREGROUND, title, title_w);
    tx += ctw_ui_type_w(ui, 11.f, 0.3f, CTW_UI_BOLD | CTW_UI_UPPER, title) + 12.f;
    if (note && note[0]) {
        const float nw = ctw_ui_type_w(ui, 10.f, 0.05f, 0, note) + 16.f, nh = 15.f + 4.f + 2.f;
        if (tx + nw < bx - 12.f) {
            const float ny = y + (th - nh) * 0.5f;
            ctw_ui_round_frame(a, tx, ny, nw, nh, nh * 0.5f, 1.f, CTW_UI_BORDER);
            ctw_ui_type(ui, tx + 8.f, ny + 3.f, 10.f, 0.05f, 0, CTW_UI_MUTED_FG, note);
        }
    }
    const int hot = ctw_ui_in(ui, bx, by, bs, bs);
    if (hot) ctw_ui_round_rect(a, bx, by, bs, bs, bs * 0.5f, CTW_UI_SECONDARY);
    ctw_ui_icon_x(a, bx + 7.f, by + 7.f, 14.f, hot ? CTW_UI_FOREGROUND : CTW_UI_MUTED_FG);
    return hot && ui->clicked;
}

/* The hint line at the bottom of a window (x, y, w, h: the window): text-[10px] muted, over a hairline. */
static inline void ctw_ui_footer(CtwUi* ui, float x, float y, float w, float h, uint32_t rgba, const char* text) {
    const float fh = ctw_ui_footer_h(ui);
    ui->api->draw_rect(x + 1.f, y + h - fh, w - 2.f, 1.f, CTW_UI_BORDER);
    ctw_ui_type_fit(ui, x + 16.f, ctw_ui_type_y(y + h - fh, fh, 10.f), 10.f, 0.f, 0, rgba, text, w - 32.f);
}

/* ---- widgets ----------------------------------------------------------------------------------------------------- */

enum { CTW_UI_OUTLINE, CTW_UI_PRIMARY_BUTTON, CTW_UI_GHOST, CTW_UI_DANGER };
enum { CTW_UI_SM, CTW_UI_MD, CTW_UI_LG };

/* <Button>: a pill, font-bold uppercase tracking-wider. sm h-7 px-3 text-[10px], md h-9 px-4 text-[11px], lg h-12 px-8
 * text-sm tracking-[0.2em]. The width of a button sized to its label: ctw_ui_button_w. */
static inline float ctw_ui_button_w(const CtwUi* ui, int size, const char* label) {
    const float px = size == CTW_UI_SM ? 10.f : size == CTW_UI_LG ? CTW_UI_TEXT_SM : 11.f;
    const float pad = size == CTW_UI_SM ? 12.f : size == CTW_UI_LG ? 32.f : 16.f;
    return ctw_ui_type_w(ui, px, size == CTW_UI_LG ? 0.2f : 0.05f, CTW_UI_BOLD | CTW_UI_UPPER, label) + 2.f * pad;
}
static inline int ctw_ui_button_ex(CtwUi* ui, float x, float y, float w, float h, int variant, int size, int disabled,
                                   const char* label) {
    const CtwApi* a = ui->api;
    const int hot = !disabled && ctw_ui_in(ui, x, y, w, h);
    const float px = size == CTW_UI_SM ? 10.f : size == CTW_UI_LG ? CTW_UI_TEXT_SM : 11.f;
    const float tracking = size == CTW_UI_LG ? 0.2f : 0.05f;
    const float k = disabled ? 0.4f : 1.f;   /* disabled:opacity-40 */
    uint32_t text = CTW_UI_MUTED_FG;
    float bx = x, by = y, bw = w, bh = h;
    if (hot && ui->down) { bx += w * 0.015f; by += h * 0.015f; bw *= 0.97f; bh *= 0.97f; }   /* kryo-press: active:scale-[0.97] */
    if (variant == CTW_UI_PRIMARY_BUTTON) {
        ctw_ui_round_rect(a, bx, by, bw, bh, bh * 0.5f, ctw_ui_alpha(CTW_UI_PRIMARY, (hot ? 0.9f : 1.f) * k));
        text = CTW_UI_PRIMARY_FG;
    } else if (variant == CTW_UI_GHOST) {
        if (hot) ctw_ui_round_rect(a, bx, by, bw, bh, bh * 0.5f, CTW_UI_SECONDARY);
        text = hot ? CTW_UI_FOREGROUND : CTW_UI_MUTED_FG;
    } else if (variant == CTW_UI_DANGER) {
        if (hot) ctw_ui_round_rect(a, bx, by, bw, bh, bh * 0.5f, CTW_UI_DESTRUCTIVE);
        else ctw_ui_round_frame(a, bx, by, bw, bh, bh * 0.5f, 1.f, ctw_ui_alpha(CTW_UI_DESTRUCTIVE, 0.5f * k));
        text = hot ? 0xFCFCFCFFu : CTW_UI_DESTRUCTIVE;
    } else {
        ctw_ui_round_frame(a, bx, by, bw, bh, bh * 0.5f, 1.f, ctw_ui_alpha(hot ? CTW_UI_FOREGROUND : CTW_UI_BORDER, k));
        text = hot ? CTW_UI_FOREGROUND : CTW_UI_MUTED_FG;
    }
    const int style = CTW_UI_BOLD | CTW_UI_UPPER;
    float tw = ctw_ui_type_w(ui, px, tracking, style, label);
    if (tw > bw - 16.f) tw = bw - 16.f;
    ctw_ui_type_fit(ui, bx + (bw - tw) * 0.5f, ctw_ui_type_y(by, bh, px), px, tracking, style, ctw_ui_alpha(text, k),
                    label, bw - 16.f);
    return hot && ui->clicked;
}
/* The default button: outline, md. */
static inline int ctw_ui_button(CtwUi* ui, float x, float y, float w, float h, const char* label) {
    return ctw_ui_button_ex(ui, x, y, w, h, CTW_UI_OUTLINE, CTW_UI_MD, 0, label);
}

/* <Check>: a size-4 box (radius min(radius, 5px), border; checked: primary with a Check size-3 stroke 3), gap-2.5,
 * the label text-xs foreground. The whole row is the button. Returns 1 when it changed. */
static inline int ctw_ui_check(CtwUi* ui, float x, float y, float w, float h, const char* label, int* value, int disabled) {
    const CtwApi* a = ui->api;
    const int hot = !disabled && ctw_ui_in(ui, x, y, w, h);
    const float k = disabled ? 0.4f : 1.f, bx = x, by = y + (h - 16.f) * 0.5f;
    if (*value) {
        ctw_ui_round_rect(a, bx, by, 16.f, 16.f, 5.f, ctw_ui_alpha(CTW_UI_PRIMARY, k));
        ctw_ui_icon_check(a, bx + 2.f, by + 2.f, 12.f, 3.f, CTW_UI_PRIMARY_FG);
    } else ctw_ui_round_frame(a, bx, by, 16.f, 16.f, 5.f, 1.f, ctw_ui_alpha(hot ? 0x808080FFu : CTW_UI_BORDER, k));
    if (label && label[0])
        ctw_ui_type_fit(ui, bx + 26.f, ctw_ui_type_y(y, h, CTW_UI_TEXT_XS), CTW_UI_TEXT_XS, 0.f, 0,
                        ctw_ui_alpha(CTW_UI_FOREGROUND, k), label, w - 26.f);
    if (hot && ui->clicked) { *value = !*value; return 1; }
    return 0;
}

/* An on/off row: the Porter's Check (the earlier switch). Returns 1 when it changed. */
static inline int ctw_ui_switch(CtwUi* ui, float x, float y, float w, float h, const char* label, int* value) {
    return ctw_ui_check(ui, x, y, w, h, label, value, 0);
}

/* A slider row: the label (text-xs) left, the value (tabular, muted) right, a pill track under them (make h
 * ctw_ui_slider_h()). Drag or use the wheel over it. Returns 1 when it changed. */
static inline int ctw_ui_slider(CtwUi* ui, int id, float x, float y, float w, float h, const char* label,
                                float* value, float min, float max, float step) {
    const CtwApi* a = ui->api;
    float old = *value, bx = x + 8.f, bw = w - 16.f, ty = y + (h - 18.f - 14.f) * 0.5f, by = ty + 18.f + 8.f;
    int hot = ctw_ui_in(ui, x, y, w, h);
    char text[48];
    /* as many decimals as the step needs: 0.25 -> "1.25", 1 -> "240" */
    const int decimals = step >= 1.f ? 0 : step >= 0.1f && (float)(int)(step * 10.f + 0.5f) == step * 10.f ? 1 : 2;
    snprintf(text, sizeof text, "%.*f", decimals, *value);
    const float vw = ctw_ui_type_w(ui, CTW_UI_TEXT_XS, 0.f, 0, text);
    ctw_ui_type_fit(ui, bx, ty, CTW_UI_TEXT_XS, 0.f, 0, CTW_UI_FOREGROUND, label, bw - vw - 16.f);
    ctw_ui_type(ui, bx + bw - vw, ty, CTW_UI_TEXT_XS, 0.f, 0, hot || ui->drag == id ? CTW_UI_FOREGROUND : CTW_UI_MUTED_FG, text);
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
        if (CTW_MOD_HAS(ui->api, consume_mouse_wheel)) ui->api->consume_mouse_wheel();
    }
    if (*value < min) *value = min;
    if (*value > max) *value = max;
    const float t = max > min ? (*value - min) / (max - min) : 0.f;
    ctw_ui_round_rect(a, bx, by, bw, 4.f, 2.f, CTW_UI_BORDER);
    ctw_ui_round_rect(a, bx, by, bw * t, 4.f, 2.f, CTW_UI_PRIMARY);
    ctw_ui_round_rect(a, bx + bw * t - 8.f, by - 6.f, 16.f, 16.f, 8.f, CTW_UI_PRIMARY);
    if (hot || ui->drag == id) ctw_ui_round_frame(a, bx + bw * t - 11.f, by - 9.f, 22.f, 22.f, 11.f, 2.f, CTW_UI_RING);
    return *value != old;
}

/* A row that picks one of several options: the label left, an outline pill with chevrons right. Click its left or
 * right half, or use the wheel over the row, to step through them. Returns 1 when it changed. */
static inline int ctw_ui_choice(CtwUi* ui, float x, float y, float w, float h, const char* label, int* value,
                                const char* const* options, int count) {
    const CtwApi* a = ui->api;
    if (count <= 0) return 0;
    const int old = *value;
    if (*value < 0 || *value >= count) *value = 0;
    const int hot = ctw_ui_in(ui, x, y, w, h);
    const float pw = w * 0.42f > 200.f ? 200.f : w * 0.42f, ph = 28.f, px = x + w - pw, py = y + (h - ph) * 0.5f;
    ctw_ui_type_fit(ui, x, ctw_ui_type_y(y, h, CTW_UI_TEXT_XS), CTW_UI_TEXT_XS, 0.f, 0, CTW_UI_FOREGROUND, label, px - x - 12.f);
    const int left = ctw_ui_in(ui, px, py, pw * 0.5f, ph), right = ctw_ui_in(ui, px + pw * 0.5f, py, pw * 0.5f, ph);
    ctw_ui_round_frame(a, px, py, pw, ph, ph * 0.5f, 1.f, hot ? CTW_UI_FOREGROUND : CTW_UI_BORDER);
    ctw_ui_icon_chevron(a, px + 8.f, py + 7.f, 14.f, 0, left ? CTW_UI_FOREGROUND : CTW_UI_MUTED_FG);
    ctw_ui_icon_chevron(a, px + pw - 22.f, py + 7.f, 14.f, 1, right ? CTW_UI_FOREGROUND : CTW_UI_MUTED_FG);
    const char* text = options[*value] ? options[*value] : "";
    const float room = pw - 2.f * 26.f;
    float tw = ctw_ui_type_w(ui, CTW_UI_TEXT_XS, 0.f, 0, text);
    if (tw > room) tw = room;
    ctw_ui_type_fit(ui, px + (pw - tw) * 0.5f, ctw_ui_type_y(py, ph, CTW_UI_TEXT_XS), CTW_UI_TEXT_XS, 0.f, 0,
                    CTW_UI_FOREGROUND, text, room);
    if (ui->clicked && (left || right)) *value = (*value + (right ? 1 : count - 1)) % count;
    else if (hot && ui->wheel != 0.f && !ui->wheel_used) {
        *value = (*value + (ui->wheel > 0.f ? 1 : count - 1)) % count;
        ui->wheel_used = 1;
        if (CTW_MOD_HAS(ui->api, consume_mouse_wheel)) ui->api->consume_mouse_wheel();
    }
    return *value != old;
}

/* <Tag> ("code", "3 textures"): rounded border px-1.5 py-0.5 text-[10px] uppercase tracking-wide, in rgba (the
 * Porter's are muted-foreground). Returns its width, so tags can be placed side by side. */
static inline float ctw_ui_badge(const CtwUi* ui, float x, float y, uint32_t rgba, const char* text) {
    const float w = ctw_ui_type_w(ui, 10.f, 0.025f, CTW_UI_UPPER, text) + 12.f + 2.f, h = 15.f + 4.f + 2.f;
    ctw_ui_round_frame(ui->api, x, y, w, h, h * 0.5f, 1.f, rgba == CTW_UI_MUTED_FG ? CTW_UI_BORDER : ctw_ui_alpha(rgba, 0.5f));
    ctw_ui_type(ui, x + 7.f, y + 3.f, 10.f, 0.025f, CTW_UI_UPPER, rgba, text);
    return w;
}
static inline float ctw_ui_tag(const CtwUi* ui, float x, float y, const char* text) {
    return ctw_ui_badge(ui, x, y, CTW_UI_MUTED_FG, text);
}

/* A scrolling area. Call before drawing its contents at y - *scroll, then ctw_ui_scroll_end.
 * *scroll eases toward *target; content_h is the full height of the contents. The scrollbar is the Porter's: 10 px,
 * a border-coloured thumb inset 2 px, muted-foreground under the pointer. */
typedef struct CtwUiScroll { float pos, target; } CtwUiScroll;

static inline void ctw_ui_scroll_begin(CtwUi* ui, int id, CtwUiScroll* s, float x, float y, float w, float h, float content_h) {
    float max = content_h > h ? content_h - h : 0.f;
    if (ctw_ui_in(ui, x, y, w, h) && ui->wheel != 0.f && !ui->wheel_used) {
        s->target -= ui->wheel * 90.f;
        ui->wheel_used = 1;
        if (CTW_MOD_HAS(ui->api, consume_mouse_wheel)) ui->api->consume_mouse_wheel();
    }
    if (max > 0.f) {
        float bar_h = h * h / content_h, track = h - bar_h, bx = x + w - 10.f;
        if (bar_h < 30.f) { bar_h = 30.f; track = h - bar_h; }
        float by = y + (s->target / max) * track;
        int hot = ctw_ui_in(ui, bx, y, 10.f, h);
        if (hot && ui->clicked) { ui->drag = id; ui->drag_offset = ctw_ui_in(ui, bx, by, 10.f, bar_h) ? ui->my - by : bar_h * 0.5f; }
        if (ui->drag == id && track > 0.f) s->target = (ui->my - ui->drag_offset - y) / track * max;
        if (s->target < 0.f) s->target = 0.f;
        if (s->target > max) s->target = max;
        by = y + (s->pos / max) * track;
        ui->api->draw_rect(bx + 2.f, by + 2.f, 6.f, bar_h - 4.f, hot || ui->drag == id ? CTW_UI_MUTED_FG : CTW_UI_BORDER);
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

/* A one-line text field: the Porter's (kryo-pill border bg-background px-4 text-xs; make h ctw_ui_row_h()), with the
 * focus ring (2 px ring, 2 px out). Click to focus; while focused it takes the typed text. Returns 1 when the text
 * changed. */
static inline int ctw_ui_textbox(CtwUi* ui, float x, float y, float w, float h, char* buf, size_t size,
                                 int* focused, const char* placeholder) {
    const CtwApi* a = ui->api;
    int hot = ctw_ui_in(ui, x, y, w, h), changed = 0;
    if (ui->clicked) *focused = hot;
    ctw_ui_round_rect(a, x, y, w, h, h * 0.5f, CTW_UI_BACKGROUND);
    ctw_ui_round_frame(a, x, y, w, h, h * 0.5f, 1.f, hot && !*focused ? 0x808080FFu : CTW_UI_BORDER);
    if (*focused) ctw_ui_round_frame(a, x - 4.f, y - 4.f, w + 8.f, h + 8.f, (h + 8.f) * 0.5f, 2.f, CTW_UI_RING);
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
    const float ty = ctw_ui_type_y(y, h, CTW_UI_TEXT_XS), room = w - 32.f;
    float tw = ctw_ui_type_w(ui, CTW_UI_TEXT_XS, 0.f, 0, buf);
    if (tw > room) tw = room;
    if (buf[0]) ctw_ui_type_fit(ui, x + 16.f, ty, CTW_UI_TEXT_XS, 0.f, 0, CTW_UI_FOREGROUND, buf, room);
    else if (placeholder) ctw_ui_type_fit(ui, x + 16.f, ty, CTW_UI_TEXT_XS, 0.f, 0, CTW_UI_MUTED_FG, placeholder, room);
    if (*focused && (a->frame_count() / 15) % 2 == 0) a->draw_rect(x + 17.f + tw, ty + 2.f, 1.f, 14.f, CTW_UI_FOREGROUND);
    return changed;
}

#endif /* CTW_UI_H */
