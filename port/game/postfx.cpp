// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kryo.to
// See LICENSE in the repository root.
// The colour grade of Game::renderStyle (a PC addition for mods; the game has none): the drawn world is copied to a
// texture and drawn back through a small shader. Off (and free) while the style is the game's own.
#include "game.h"
#include <glad/gl.h>
#include <cstdio>

namespace {
const char* kVertex = R"(#version 110
void main() { gl_TexCoord[0] = gl_MultiTexCoord0; gl_Position = gl_Vertex; }
)";
const char* kFragment = R"(#version 110
uniform sampler2D picture;
uniform vec3 tint;
uniform float sepia, saturation, contrast, brightness, vignette;
void main() {
    vec2 uv = gl_TexCoord[0].xy;
    vec3 c = texture2D(picture, uv).rgb;
    float l = dot(c, vec3(0.299, 0.587, 0.114));
    c = mix(vec3(l), c, saturation);
    c = mix(c, vec3(l * 1.08 + 0.03, l * 0.93 + 0.015, l * 0.74), sepia);
    c = (c - 0.5) * contrast + 0.5 + brightness;
    c *= tint;
    vec2 d = uv - 0.5;
    c *= 1.0 - vignette * smoothstep(0.12, 0.55, dot(d, d) * 1.6);
    gl_FragColor = vec4(clamp(c, 0.0, 1.0), 1.0);
}
)";

struct Grade {
    GLuint program = 0, texture = 0;
    int w = 0, h = 0;
    bool failed = false;
    GLint tint = -1, sepia = -1, saturation = -1, contrast = -1, brightness = -1, vignette = -1;

    GLuint compile(GLenum type, const char* src) {
        GLuint s = glCreateShader(type);
        glShaderSource(s, 1, &src, nullptr);
        glCompileShader(s);
        GLint ok = 0;
        glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
        if (!ok) {
            char log[512] = {};
            glGetShaderInfoLog(s, sizeof log, nullptr, log);
            fprintf(stderr, "render style shader: %s\n", log);
            glDeleteShader(s);
            return 0;
        }
        return s;
    }
    bool init() {
        if (program) return true;
        if (failed || !GLAD_GL_VERSION_2_0) return false;
        failed = true;
        GLuint v = compile(GL_VERTEX_SHADER, kVertex), f = compile(GL_FRAGMENT_SHADER, kFragment);
        if (!v || !f) return false;
        program = glCreateProgram();
        glAttachShader(program, v);
        glAttachShader(program, f);
        glLinkProgram(program);
        glDeleteShader(v);
        glDeleteShader(f);
        GLint ok = 0;
        glGetProgramiv(program, GL_LINK_STATUS, &ok);
        if (!ok) { glDeleteProgram(program); program = 0; return false; }
        tint = glGetUniformLocation(program, "tint");
        sepia = glGetUniformLocation(program, "sepia");
        saturation = glGetUniformLocation(program, "saturation");
        contrast = glGetUniformLocation(program, "contrast");
        brightness = glGetUniformLocation(program, "brightness");
        vignette = glGetUniformLocation(program, "vignette");
        glGenTextures(1, &texture);
        failed = false;
        return true;
    }
};
Grade g_grade;
}

void Game::applyGrade(int W, int H) {
    const RenderStyle& s = renderStyle;
    if (!s.graded() || W <= 0 || H <= 0 || !g_grade.init()) return;
    glBindTexture(GL_TEXTURE_2D, g_grade.texture);
    if (g_grade.w != W || g_grade.h != H) {
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, W, H, 0, GL_RGB, GL_UNSIGNED_BYTE, nullptr);
        g_grade.w = W; g_grade.h = H;
    }
    glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 0, 0, W, H);
    glUseProgram(g_grade.program);
    glUniform3f(g_grade.tint, s.tint[0], s.tint[1], s.tint[2]);
    glUniform1f(g_grade.sepia, s.sepia);
    glUniform1f(g_grade.saturation, s.saturation);
    glUniform1f(g_grade.contrast, s.contrast);
    glUniform1f(g_grade.brightness, s.brightness);
    glUniform1f(g_grade.vignette, s.vignette);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glDisable(GL_ALPHA_TEST);
    glDisable(GL_LIGHTING);
    glDisable(GL_CULL_FACE);
    glEnable(GL_TEXTURE_2D);
    glBegin(GL_QUADS);   // clip space: the vertex shader passes positions through
    glTexCoord2f(0, 0); glVertex2f(-1, -1);
    glTexCoord2f(1, 0); glVertex2f(1, -1);
    glTexCoord2f(1, 1); glVertex2f(1, 1);
    glTexCoord2f(0, 1); glVertex2f(-1, 1);
    glEnd();
    glUseProgram(0);
    glDisable(GL_TEXTURE_2D);
}
