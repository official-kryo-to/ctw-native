// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kryo.to
// See LICENSE in the repository root.
// The city renderer shared by the showcase viewer and the game: streams world blocks around a focus point and draws
// them the way the game does - render lists and materials (cBucketManager::Render), time-of-day lighting
// (cRenderer + cTimeCycle), animated models, water (cWaterRenderBlock) and world lights (cLightManager::Render).
#pragma once
#include "world/timecycle.h"
#include "world/worldmap.h"
#include <functional>
#include <map>
#include <memory>
#include <string>

struct WorldCamera {
    float eye[3];
    float right[3], up[3], fwd[3];   // orthonormal view basis (fwd = viewing direction), world is z-up
    float fovY = 50.f, zNear = 1.f, zFar = 3000.f;
    // Builds the basis from yaw (degrees, 0 = looking along +y) and pitch (degrees, negative = looking down).
    void setYawPitch(float yawDeg, float pitchDeg);
    // Builds the basis looking from eye at target (up hint = +z).
    void lookAt(const float target[3]);
};

class WorldRenderer {
public:
    bool init(const std::string& dataDir);
    bool ok() const { return ok_; }

    // Streaming: blocks within `radius` of the block containing (x, y) are loaded, up to `budget` per call.
    bool stream(float x, float y, int budget);
    void loadAllNow(float x, float y) { while (ok_ && !stream(x, y, 64)) {} }
    void setRadius(int r) { radius_ = r; }
    int radius() const { return radius_; }

    void tick();                              // one 30 fps game frame: water UVs and model animations
    TimeCycle& timeCycle() { return tc_; }

    void render(const WorldCamera& cam, int width, int height);

    // Debug / viewer options
    bool textured = true, wireframe = false;
    std::vector<WorldLight> propLights;   // lights of street furniture near the camera (drawn like world lights)
    std::function<void()> drawBeforeLights;   // more of the world (props), drawn after the blocks, before the lights
    struct Stats { size_t blocks, draws; int objects; };
    Stats stats() const;

private:
    struct LoadedBlock { int c = 0, r = 0; unsigned vbo = 0; WorldBlockMesh mesh; };
    struct WaterBlock { int16_t uvSize, counter; bool scrolls; int16_t uv[24]; };
    struct SpriteRect { uint16_t x, y, w, h; };

    static int keyOf(int c, int r) { return r * WorldMap::kCols + c; }
    void build(LoadedBlock& b);
    void upload(LoadedBlock& b);
    void unload(int key);
    static void processUVs(WaterBlock& w);
    void renderWater(const WaterBlock& w, bool sea, uint32_t colour);
    void loadFxSprites();
    void fxSprite(int sprite, uint32_t argb, const float pos[3], float sx, float sy, const float right[3], const float up[3]);
    void renderLights(const WorldCamera& cam);
    void debugPick(int x, int y, int height);

    WorldMap map_;
    TimeCycle tc_;
    bool ok_ = false;
    int radius_ = 2;
    std::map<int, std::unique_ptr<LoadedBlock>> blocks_;
    std::map<WorldObjectKey, int> owner_;
    WaterBlock sea_{0x800, 0, true, {}}, lake_{0x1000, 0, false, {}};
    std::vector<SpriteRect> fxSprites_;
    int fxTexture_ = -1;
};
