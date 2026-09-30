// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kryo.to
// See LICENSE in the repository root.
#include "hud.h"
#include "gfx/font.h"
#include "os/gamefs.h"
#include <glad/gl.h>
#include <vector>

static BitmapFont g_font;
static bool g_fontOk = false;

bool Hud_Init(const std::string& dataDir) {
    GameFs fs;
    std::vector<uint8_t> bin;
    if (fs.open(dataDir) && fs.read("IPhone_Hel_16x16.bin", bin)) g_fontOk = g_font.load(bin, dataDir + "/iphone_hel_16x16.png");
    return g_fontOk;
}

void Hud_Begin(int w, int h) {
    glViewport(0, 0, w, h);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0, w, h, 0, -1, 1);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_LIGHTING);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}

void Hud_End() {
    glDisable(GL_BLEND);
    glDisable(GL_TEXTURE_2D);
}

std::u16string Hud_Utf8To16(const std::string& s) {
    std::u16string out;
    for (size_t i = 0; i < s.size();) {
        unsigned char c = (unsigned char)s[i];
        uint32_t cp = c;
        int n = 0;
        if (c >= 0xF0) { cp = c & 0x07; n = 3; }
        else if (c >= 0xE0) { cp = c & 0x0F; n = 2; }
        else if (c >= 0xC0) { cp = c & 0x1F; n = 1; }
        ++i;
        for (int k = 0; k < n && i < s.size(); ++k, ++i) cp = cp << 6 | ((unsigned char)s[i] & 0x3F);
        out.push_back(cp > 0xFFFF ? u'?' : (char16_t)cp);
    }
    return out;
}

void Hud_Text(float x, float y, float scale, uint32_t rgba, const std::string& utf8) {
    if (!g_fontOk) return;
    glColor4ub((GLubyte)(rgba >> 24), (GLubyte)(rgba >> 16), (GLubyte)(rgba >> 8), (GLubyte)rgba);
    int colour = 0;
    g_font.draw(Hud_Utf8To16(utf8), x, y, scale, &colour);
}

float Hud_TextWidth(const std::string& utf8, float scale) {
    return g_fontOk ? g_font.textWidth(Hud_Utf8To16(utf8)) * scale : 0.f;
}

float Hud_LineHeight(float scale) { return (g_fontOk ? g_font.lineHeight() : 16) * scale; }

void Hud_Rect(float x, float y, float w, float h, uint32_t rgba) {
    glDisable(GL_TEXTURE_2D);
    glColor4ub((GLubyte)(rgba >> 24), (GLubyte)(rgba >> 16), (GLubyte)(rgba >> 8), (GLubyte)rgba);
    glBegin(GL_QUADS);
    glVertex2f(x, y); glVertex2f(x + w, y); glVertex2f(x + w, y + h); glVertex2f(x, y + h);
    glEnd();
}
