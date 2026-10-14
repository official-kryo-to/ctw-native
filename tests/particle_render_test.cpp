// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kryo.to
// See LICENSE in the repository root.
#define SDL_MAIN_HANDLED
#include "particles.h"
#include "world/worldrenderer.h"
#include "os/os.h"
#include <SDL.h>
#include <glad/gl.h>
#include <cmath>
#include <cstdio>

struct ParticlesTestAccess {
    static void quad(Emitter& emitter, uint16_t colour, uint8_t alpha) {
        // Solid synthetic quads exercise real blend state without loading game assets.
        emitter.sprite_ = 255;
        emitter.billboard_ = false;
        emitter.parts_.assign(1, Particle{});
        Particle& p = emitter.parts_.front();
        p.colour = colour; p.alpha = alpha; p.size = 4096; p.active = 1;
        emitter.alive_ = 1;
    }
};

static int failures;
static void pixel(const Particles& particles, const float expected[3], const char* description) {
    glClearColor(32.f/255,64.f/255,96.f/255,1);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    particles.render(WorldCamera{});
    uint8_t actual[4]{};
    glReadPixels(32,32,1,1,GL_RGBA,GL_UNSIGNED_BYTE,actual);
    for (int i=0;i<3;++i) if (std::abs(actual[i]-expected[i]) > 2) {
        std::fprintf(stderr,"FAIL: %s: RGB %u,%u,%u; expected %.1f,%.1f,%.1f\n",
                     description,actual[0],actual[1],actual[2],expected[0],expected[1],expected[2]);
        ++failures;
        break;
    }
}

int main() {
    if (!Host_Init("Particle rendering regression",64,64)) {
        std::fprintf(stderr,"SKIP: no OpenGL test context: %s\n",SDL_GetError());
        Host_Shutdown();
        return 77;
    }
    glViewport(0,0,64,64);
    glMatrixMode(GL_PROJECTION); glLoadIdentity(); glOrtho(-5,5,-5,5,-5,5);
    glMatrixMode(GL_MODELVIEW); glLoadIdentity();
    const int32_t origin[3]{};
    const float alpha = (15*255/31)/255.f;
    const float flashRGB[3] = {248,216,0}, background[3] = {32,64,96};
    float added[3], smoked[3], combined[3];
    for (int i=0;i<3;++i) {
        added[i] = background[i]+flashRGB[i]*alpha;
        smoked[i] = 248*alpha+background[i]*(1-alpha);
        combined[i] = 248*alpha+added[i]*(1-alpha);
    }
    Particles flashOnly;
    auto* flash = flashOnly.add<ExplosionFlash>(origin);
    ParticlesTestAccess::quad(*flash,0x37F,15);
    pixel(flashOnly,added,"yellow flash adds light without darkening the blue background channel");

    Particles smokeOnly;
    auto* smoke = smokeOnly.add<SmokeEmitter>(origin,1);
    ParticlesTestAccess::quad(*smoke,0x7FFF,15);
    pixel(smokeOnly,smoked,"ordinary smoke retains source-alpha blending");

    Particles mixed;
    smoke = mixed.add<SmokeEmitter>(origin,1);
    flash = mixed.add<ExplosionFlash>(origin);
    ParticlesTestAccess::quad(*smoke,0x7FFF,15);
    ParticlesTestAccess::quad(*flash,0x37F,15);
    pixel(mixed,combined,"emissive pass precedes smoke even when smoke was registered first");
    GLint source = 0, destination = 0;
    glGetIntegerv(GL_BLEND_SRC,&source); glGetIntegerv(GL_BLEND_DST,&destination);
    if (source != GL_SRC_ALPHA || destination != GL_ONE_MINUS_SRC_ALPHA || glIsEnabled(GL_BLEND)) {
        std::fprintf(stderr,"FAIL: particles restore ordinary blend factors and disable blending\n");
        ++failures;
    }
    Host_Shutdown();
    return failures ? 1 : 0;
}
