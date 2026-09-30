#include "worldrenderer.h"
#include "gfx/assets.h"
#include "os/gamefs.h"
#include "os/pak.h"
#include <glad/gl.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <set>

// ---------------------------------------------------------------------------------------------- camera
static void cross(const float a[3], const float b[3], float o[3]) {
    o[0] = a[1] * b[2] - a[2] * b[1]; o[1] = a[2] * b[0] - a[0] * b[2]; o[2] = a[0] * b[1] - a[1] * b[0];
}
static void normalise(float v[3]) {
    float l = sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    if (l > 0) { v[0] /= l; v[1] /= l; v[2] /= l; }
}

void WorldCamera::setYawPitch(float yawDeg, float pitchDeg) {
    float yr = yawDeg * 3.14159265f / 180.f, pr = pitchDeg * 3.14159265f / 180.f;
    fwd[0] = -sinf(yr) * cosf(pr); fwd[1] = cosf(yr) * cosf(pr); fwd[2] = sinf(pr);
    right[0] = cosf(yr); right[1] = sinf(yr); right[2] = 0.f;
    cross(right, fwd, up);
}

void WorldCamera::lookAt(const float target[3]) {
    for (int i = 0; i < 3; ++i) fwd[i] = target[i] - eye[i];
    normalise(fwd);
    const float z[3] = {0, 0, 1};
    cross(fwd, z, right);
    if (right[0] * right[0] + right[1] * right[1] + right[2] * right[2] < 1e-8f) { right[0] = 1; right[1] = 0; right[2] = 0; }
    normalise(right);
    cross(right, fwd, up);
}

// ---------------------------------------------------------------------------------------------- setup
bool WorldRenderer::init(const std::string& dataDir) {
    ok_ = Assets_Open(dataDir) && map_.init();
    if (ok_) loadFxSprites();
    GameFs fs;
    std::vector<uint8_t> tc;
    if (fs.open(dataDir) && fs.read("timecycle.dat", tc)) tc_.load(tc);
    tc_.setWeather(0);
    tc_.setTime(12u << 12);
    tc_.evaluate();
    return ok_;
}

WorldRenderer::Stats WorldRenderer::stats() const {
    Stats s{blocks_.size(), 0, 0};
    for (auto& kv : blocks_) { s.draws += kv.second->mesh.draws.size(); s.objects += kv.second->mesh.drawn; }
    return s;
}

// ---------------------------------------------------------------------------------------------- streaming
void WorldRenderer::upload(LoadedBlock& b) {
    if (!b.vbo) glGenBuffers(1, &b.vbo);
    glBindBuffer(GL_ARRAY_BUFFER, b.vbo);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(b.mesh.verts.size() * sizeof(WorldVertex)), b.mesh.verts.data(), GL_STATIC_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    b.mesh.verts.clear();
    b.mesh.verts.shrink_to_fit();
}

void WorldRenderer::build(LoadedBlock& b) {
    int self = keyOf(b.c, b.r);
    map_.build(b.c, b.r, owner_, self, b.mesh);
    for (const WorldObjectKey& k : b.mesh.objects) owner_.emplace(k, self);   // claim what nobody draws yet
    upload(b);
}

void WorldRenderer::unload(int key) {
    auto it = blocks_.find(key);
    if (it == blocks_.end()) return;
    std::unique_ptr<LoadedBlock> b = std::move(it->second);
    blocks_.erase(it);
    std::set<WorldObjectKey> released;
    for (const WorldObjectKey& k : b->mesh.objects) {
        auto o = owner_.find(k);
        if (o != owner_.end() && o->second == key) { owner_.erase(o); released.insert(k); }
    }
    if (b->vbo) glDeleteBuffers(1, &b->vbo);
    for (auto& kv : blocks_)   // neighbours that also list a released object take it over
        for (const WorldObjectKey& k : kv.second->mesh.objects)
            if (released.count(k)) { build(*kv.second); break; }
}

bool WorldRenderer::stream(float x, float y, int budget) {
    if (!ok_) return true;
    int cc = (int)std::floor((x + 3510.f) / 120.f), cr = (int)std::floor((y + 2490.f) / 120.f);
    for (auto it = blocks_.begin(); it != blocks_.end();) {
        int c = it->second->c, r = it->second->r, key = it->first;
        ++it;
        if (std::abs(c - cc) > radius_ + 1 || std::abs(r - cr) > radius_ + 1) unload(key);   // hysteresis of 1
    }
    for (int ring = 0; ring <= radius_; ++ring)
        for (int r = cr - ring; r <= cr + ring; ++r)
            for (int c = cc - ring; c <= cc + ring; ++c) {
                if (std::max(std::abs(c - cc), std::abs(r - cr)) != ring) continue;
                if (map_.blockId(c, r) < 0 || blocks_.count(keyOf(c, r))) continue;
                if (budget-- <= 0) return false;
                auto b = std::make_unique<LoadedBlock>();
                b->c = c; b->r = r;
                LoadedBlock& ref = *b;
                blocks_[keyOf(c, r)] = std::move(b);
                build(ref);
            }
    return true;
}

// ---------------------------------------------------------------------------------------------- water
// Sea (cWaterBlock: z -7.5, UV scroll, tile UV size 0x800) and lake (cLakeBlock: z -2.5, no scroll, 0x1000).
static int fastsin(int a) { return (int)(sinf((float)a * 9.587378e-05f) * 4096.f); }

void WorldRenderer::processUVs(WaterBlock& w) {   // cWaterRenderBlock::ProcessUVs (+ cWaterBlock scroll)
    uint32_t scroll = 0;
    if (w.scrolls) scroll = (uint32_t)((int)w.counter * 0x1000 / 0xFFFF);
    int16_t s2 = w.uvSize;
    uint16_t u1 = (uint16_t)(s2 - (s2 >> 15));
    w.counter = (int16_t)(w.counter + 0xB6);
    int i9 = fastsin(w.counter);
    uint32_t u10 = (uint32_t)fastsin(w.counter << 1);
    uint32_t u11 = (uint32_t)fastsin(w.counter * 2 + 0x4000);
    int16_t s5 = (int16_t)(i9 * 3 >> 6);
    int64_t half = (int64_t)(int16_t)u1 >> 1;
    int16_t s6 = (int16_t)(((uint64_t)scroll * (uint64_t)half) >> 11);
    int16_t s7 = (int16_t)((u1 & 0xFFFE) + s6);
    int16_t s8 = (int16_t)(s6 + s2 / 2);
    int16_t s4 = (int16_t)((int)(u10 << 1) >> 6);
    int16_t s2b = (int16_t)(s4 + s6);
    int16_t s3 = (int16_t)(s8 + (int16_t)(u10 >> 5));
    int16_t s2c = (int16_t)(s8 + (int16_t)(u11 * 3 >> 6));
    int16_t s8b = (int16_t)(s8 + (int16_t)(u11 >> 5));
    int16_t s4b = (int16_t)(s7 + s4);
    int16_t* o = w.uv;   // o[k] = the short at +0x10 + 2k
    o[0] = s6;  o[1] = s6;   o[2] = s8b;  o[3] = s2b;  o[4] = (int16_t)(s5 + s6); o[5] = s2c;
    o[6] = s3;  o[7] = s2c;  o[8] = s6;   o[9] = s7;   o[10] = s8b; o[11] = s4b;
    o[12] = s8b; o[13] = s2b; o[14] = s7; o[15] = s6;  o[16] = s3;  o[17] = s2c;
    o[18] = (int16_t)(s7 + s5); o[19] = s2c; o[20] = s8b; o[21] = s4b; o[22] = s7; o[23] = s7;
}

void WorldRenderer::renderWater(const WaterBlock& w, bool sea, uint32_t colour) {
    // cWaterRenderBlock::Render: per tile a 16-vertex triangle strip over a 2 x 2 grid of 10-unit cells.
    static const struct { int dx, dy, uv; } kStrip[16] = {
        {-1, -1, 0}, {-1, -1, 0}, {0, -1, 1}, {-1, 0, 2}, {0, 0, 3}, {-1, 1, 4}, {0, 1, 5}, {0, 1, 5},
        {0, -1, 6},  {0, -1, 6},  {1, -1, 7}, {0, 0, 8},  {1, 0, 9}, {0, 1, 10}, {1, 1, 11}, {1, 1, 11}};
    glColor4f((colour & 0xFF) / 255.f, (colour >> 8 & 0xFF) / 255.f, (colour >> 16 & 0xFF) / 255.f, 0xC8 / 255.f);
    glBegin(GL_TRIANGLE_STRIP);
    for (auto& kv : blocks_)
        for (const WaterPatch& p : kv.second->mesh.water) {
            if (p.sea != sea) continue;
            for (const auto& s : kStrip) {
                glTexCoord2f(w.uv[s.uv * 2] / 2048.f, w.uv[s.uv * 2 + 1] / 2048.f);
                glVertex3f(p.cx + s.dx * 10.f, p.cy + s.dy * 10.f, p.z);
            }
        }
    glEnd();
}

void WorldRenderer::tick() {
    processUVs(sea_);
    processUVs(lake_);
    for (auto& kv : blocks_)
        for (WorldAnim& a : kv.second->mesh.anims) a.step();
}

// ---------------------------------------------------------------------------------------------- world lights
// Sprites come from the global effects sheet: gGlobalAssetMgr+4 = resource gGameDir[9] (2396): u16 texture id,
// then {u16 x, y, w, h} per sprite in pixels (UV = pixel * 8 / 2048).
void WorldRenderer::loadFxSprites() {
    std::vector<uint8_t> gd, r;
    uint16_t dir[28];
    if (!Assets_Pak().read(0, gd) || gd.size() < sizeof dir) return;
    memcpy(dir, gd.data(), sizeof dir);
    if (!Assets_Pak().read(dir[9], r) || r.size() < 0xD4) return;
    uint16_t tex;
    memcpy(&tex, &r[0], 2);
    fxTexture_ = tex;
    for (size_t o = 4; o + 8 <= 0xD4; o += 8) {
        SpriteRect s;
        memcpy(&s, &r[o], 8);
        fxSprites_.push_back(s);
    }
}

// immsprite3d::RenderWorldScaleCamAligned: quad centred on pos, half extents sx along camera right, sy along up.
void WorldRenderer::fxSprite(int sprite, uint32_t argb, const float pos[3], float sx, float sy, const float right[3],
                             const float up[3]) {
    if (sprite >= (int)fxSprites_.size()) return;
    const SpriteRect& s = fxSprites_[sprite];
    float u0 = s.x / 256.f, v0 = s.y / 256.f, u1 = (s.x + s.w) / 256.f, v1 = (s.y + s.h) / 256.f;
    glColor4ub((GLubyte)(argb >> 16), (GLubyte)(argb >> 8), (GLubyte)argb, (GLubyte)(argb >> 24));
    auto vtx = [&](float a, float b, float u, float v) {
        glTexCoord2f(u, v);
        glVertex3f(pos[0] + right[0] * sx * a + up[0] * sy * b, pos[1] + right[1] * sx * a + up[1] * sy * b,
                   pos[2] + right[2] * sx * a + up[2] * sy * b);
    };
    vtx(-1, -1, u0, v1); vtx(1, -1, u1, v1); vtx(1, 1, u1, v0); vtx(-1, 1, u0, v0);
}

// RGB555 (red in the low bits) expanded to 8 bits per channel, as cLight::Render builds its colour words.
static uint32_t lightColour(uint16_t c, uint32_t alpha) {
    return alpha << 24 | (uint32_t)((c & 0x1F) << 3) << 16 | (uint32_t)((c >> 5 & 0x1F) << 3) << 8 | (uint32_t)((c >> 10 & 0x1F) << 3);
}
// cLight::Process: on from 20:00 to 07:00, size fading in around 20:00 and out around 07:00.
static bool lightOn(uint32_t t) { return t - 0x14000u < 0xFFFF3000u; }
static int lightSize(int base, uint32_t t) {
    int cur = base << 1;
    if (std::abs((int)t - 0x14000) < 0x1000) cur = (int)((uint32_t)(((int)t - 0x13000) * base) >> 12);
    int d7 = std::abs((int)t - 0x7000);
    if (d7 < 0x1000) cur = (int)((uint32_t)((d7 + 0x1000) * base) >> 12);
    return (int16_t)cur;
}

void WorldRenderer::renderLights(const WorldCamera& cam) {
    uint32_t t = tc_.time();
    if (!lightOn(t) || fxTexture_ < 0) return;
    GLuint tex = Assets_Texture(fxTexture_);
    if (!tex) return;
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, tex);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE);   // cLightManager::Render: additive, no depth writes
    glDepthMask(GL_FALSE);
    glDisable(GL_CULL_FACE);
    glDisable(GL_LIGHTING);
    const int64_t cx = (int64_t)(cam.eye[0] * 4096.f), cy = (int64_t)(cam.eye[1] * 4096.f), cz = (int64_t)(cam.eye[2] * 4096.f);
    const uint32_t streakAlpha = (uint32_t)((8u * 0x1000u / 0x1Fu) * 0xFF000u) >> 24;   // sStreakAlpha = 8
    glBegin(GL_QUADS);
    for (auto& kv : blocks_)
        for (const WorldLight& l : kv.second->mesh.lights) {
            int cur = lightSize(l.size, t);
            float p[3] = {l.x / 4096.f, l.y / 4096.f, l.z / 4096.f};
            int64_t dx = (cx - l.x) >> 4, dy = (cy - l.y) >> 4;
            uint64_t d2 = (uint64_t)(dx * dx + dy * dy);
            if (d2 <= 0x9C4000000ull) {   // type-0 lights: big faint glow + light streak when within range
                int u4 = (int)std::sqrt((double)d2) - (int)((cz - l.z) >> 4);
                int u6 = std::min(std::max(u4, 0), 0x2000);
                int64_t u7 = u4 < 1 ? 0x800 : (((int64_t)(uint32_t)u4 * 0x800 + 0x800000) >> 12);
                float s = (float)(((int64_t)cur * u7) >> 10) / 4096.f;
                fxSprite(13, lightColour(l.colour, 0x20), p, s, s, cam.right, cam.up);
                float ps[3] = {p[0], p[1], p[2] + 0x199 / 4096.f};
                fxSprite(14, lightColour(l.colour, streakAlpha), ps, u6 * 12 / 4096.f, (u6 >> 1) / 4096.f, cam.right, cam.up);
            }
            float pm[3] = {p[0], p[1], p[2] + 0x333 / 4096.f};
            fxSprite(14, lightColour(l.colour, 0xF6), pm, 1.f, 1.f, cam.right, cam.up);
        }
    glEnd();
    glDepthMask(GL_TRUE);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_BLEND);
    glDisable(GL_TEXTURE_2D);
}

// ---------------------------------------------------------------------------------------------- frame
void WorldRenderer::render(const WorldCamera& cam, int W, int H) {
    glViewport(0, 0, W, H);
    float sky[3] = {0.10f, 0.12f, 0.16f};
    if (tc_.ok()) tc_.skyColour(sky);   // cRenderer: ClearColor from the time cycle
    glClearColor(sky[0], sky[1], sky[2], 1.f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    if (!ok_) return;

    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    float t = cam.zNear * tanf(cam.fovY * 3.14159265f / 360.f), a = (float)W / H;
    glFrustum(-t * a, t * a, -t, t, cam.zNear, cam.zFar);
    glMatrixMode(GL_MODELVIEW);
    const float* R = cam.right; const float* U = cam.up; const float* F = cam.fwd; const float* E = cam.eye;
    float view[16] = {R[0], U[0], -F[0], 0, R[1], U[1], -F[1], 0, R[2], U[2], -F[2], 0,
                      -(R[0] * E[0] + R[1] * E[1] + R[2] * E[2]), -(U[0] * E[0] + U[1] * E[1] + U[2] * E[2]),
                      F[0] * E[0] + F[1] * E[1] + F[2] * E[2], 1};
    glLoadMatrixf(view);

    // The game's lighting (cRenderer + the vertex shader):
    //   colour = sunColour * dot(normal, sunDir) + 0.4 * ambient, sunColour = ColourLightning(0),
    //   ambient = Colour(0xD) (vecMainLightColorAmbient is 1,1,1), sunDir from the time cycle's sun angles.
    float sun[4] = {0.f, 0.f, 1.f, 0.f}, amb[4] = {0.45f, 0.45f, 0.45f, 1.f}, dif[4] = {0.7f, 0.7f, 0.7f, 1.f};
    float zero[4] = {0.f, 0.f, 0.f, 1.f};
    if (tc_.ok()) {
        tc_.sunDirection(sun);
        uint32_t d = tc_.colour(0), am = tc_.colour(13);
        for (int i = 0; i < 3; ++i) {
            dif[i] = (d >> (8 * i) & 0xFF) / 255.f;
            amb[i] = 0.4f * (am >> (8 * i) & 0xFF) / 255.f;
        }
    }
    glLightfv(GL_LIGHT0, GL_POSITION, sun);
    glLightfv(GL_LIGHT0, GL_AMBIENT, zero);
    glLightfv(GL_LIGHT0, GL_DIFFUSE, dif);
    glLightfv(GL_LIGHT0, GL_SPECULAR, zero);
    glLightModelfv(GL_LIGHT_MODEL_AMBIENT, amb);
    glLightModeli(GL_LIGHT_MODEL_TWO_SIDE, GL_FALSE);
    glColorMaterial(GL_FRONT_AND_BACK, GL_AMBIENT_AND_DIFFUSE);
    glEnable(GL_LIGHT0);
    glEnable(GL_COLOR_MATERIAL);
    glEnable(GL_NORMALIZE);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);          // cBucketManager::Render: DepthFunc(GL_LESS)
    glCullFace(GL_BACK);
    glAlphaFunc(GL_GREATER, 8.f / 256.f);
    glPolygonMode(GL_FRONT_AND_BACK, wireframe ? GL_LINE : GL_FILL);
    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_NORMAL_ARRAY);
    glEnableClientState(GL_TEXTURE_COORD_ARRAY);

    static const char* onlyTex = getenv("CTW_ONLYTEX");   // debug: draw a single texture
    // Render lists 0..11 in order, like cGame: Render(0,1), [water], Render(2,7), Render(8,11).
    for (int list = 0; list <= 11; ++list) {
        if (list == 2) {   // cEffectManager::PreWorldAlphaRender: water, blended, no depth writes, no culling
            glDisableClientState(GL_VERTEX_ARRAY);
            glDisableClientState(GL_NORMAL_ARRAY);
            glDisableClientState(GL_TEXTURE_COORD_ARRAY);
            glBindBuffer(GL_ARRAY_BUFFER, 0);
            glDisable(GL_LIGHTING);
            glDisable(GL_CULL_FACE);
            glDisable(GL_ALPHA_TEST);
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            glDepthMask(GL_FALSE);
            GLuint wt = textured ? Assets_Texture(4180) : 0;   // gGameDir[27]: the water texture
            if (wt) { glEnable(GL_TEXTURE_2D); glBindTexture(GL_TEXTURE_2D, wt); } else glDisable(GL_TEXTURE_2D);
            uint32_t wc = tc_.ok() ? tc_.colour(0x16) : 0xFF806040u;
            renderWater(sea_, true, wc);
            renderWater(lake_, false, wc);
            glDepthMask(GL_TRUE);
            glEnableClientState(GL_VERTEX_ARRAY);
            glEnableClientState(GL_NORMAL_ARRAY);
            glEnableClientState(GL_TEXTURE_COORD_ARRAY);
        }
        if (list == 0) glDisable(GL_BLEND);
        else {
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, (list == 4 || list == 6) ? GL_ONE : GL_ONE_MINUS_SRC_ALPHA);
        }
        if (list == 7) glEnable(GL_ALPHA_TEST); else glDisable(GL_ALPHA_TEST);
        float tint[3] = {1.f, 1.f, 1.f};
        if (list == 6) { tint[0] = 0.992f; tint[1] = 0.98f; tint[2] = 0.88f; }
        for (auto& kv : blocks_) {
            LoadedBlock& b = *kv.second;
            bool bound = false;
            for (const WorldDraw& d : b.mesh.draws) {
                if (d.list != list || !d.count) continue;
                if (d.anim >= 0 && !(d.mask & b.mesh.anims[d.anim].current)) continue;   // animation frame
                if (onlyTex && atoi(onlyTex) != d.texture) continue;
                if (!bound) {
                    glBindBuffer(GL_ARRAY_BUFFER, b.vbo);
                    glVertexPointer(3, GL_FLOAT, sizeof(WorldVertex), (void*)0);
                    glNormalPointer(GL_FLOAT, sizeof(WorldVertex), (void*)12);
                    glTexCoordPointer(2, GL_FLOAT, sizeof(WorldVertex), (void*)24);
                    bound = true;
                }
                GLuint tex = textured ? Assets_Texture(d.texture) : 0;
                if (tex) { glEnable(GL_TEXTURE_2D); glBindTexture(GL_TEXTURE_2D, tex); }
                else glDisable(GL_TEXTURE_2D);
                if (d.flags & 0x10) glDisable(GL_CULL_FACE); else glEnable(GL_CULL_FACE);
                if (d.flags & 0x07) glEnable(GL_LIGHTING); else glDisable(GL_LIGHTING);
                glDepthMask((d.flags & 0x20) ? GL_FALSE : GL_TRUE);
                glColor4f(tint[0], tint[1], tint[2], list == 0 ? 1.f : d.alpha);
                glDrawArrays(GL_TRIANGLES, (GLint)d.first, (GLsizei)d.count);
            }
        }
    }
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glDisableClientState(GL_VERTEX_ARRAY);
    glDisableClientState(GL_NORMAL_ARRAY);
    glDisableClientState(GL_TEXTURE_COORD_ARRAY);
    glDisable(GL_LIGHTING);
    glDisable(GL_TEXTURE_2D);
    glDisable(GL_CULL_FACE);
    glDisable(GL_ALPHA_TEST);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);

    renderLights(cam);

    if (const char* pick = getenv("CTW_PICK")) {
        int px = 0, py = 0;
        sscanf(pick, "%d,%d", &px, &py);
        debugPick(px, py, H);
    }
    glDisable(GL_DEPTH_TEST);
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
}

// Debug picker (CTW_PICK=x,y): redraws every draw call in a unique flat colour, reports the draw and triangle at (x, y).
void WorldRenderer::debugPick(int px, int py, int H) {
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glDisable(GL_LIGHTING); glDisable(GL_TEXTURE_2D); glDisable(GL_BLEND); glDisable(GL_ALPHA_TEST);
    glEnableClientState(GL_VERTEX_ARRAY);
    std::vector<std::pair<LoadedBlock*, size_t>> ids;
    for (auto& kv : blocks_) {
        LoadedBlock& b = *kv.second;
        glBindBuffer(GL_ARRAY_BUFFER, b.vbo);
        glVertexPointer(3, GL_FLOAT, sizeof(WorldVertex), (void*)0);
        for (size_t i = 0; i < b.mesh.draws.size(); ++i) {
            const WorldDraw& d = b.mesh.draws[i];
            if (d.anim >= 0 && !(d.mask & b.mesh.anims[d.anim].current)) continue;
            ids.push_back({&b, i});
            size_t id = ids.size();
            glColor3ub((GLubyte)(id & 0xFF), (GLubyte)(id >> 8 & 0xFF), (GLubyte)(id >> 16 & 0xFF));
            if (d.flags & 0x10) glDisable(GL_CULL_FACE); else glEnable(GL_CULL_FACE);
            glDrawArrays(GL_TRIANGLES, (GLint)d.first, (GLsizei)d.count);
        }
    }
    GLubyte c[4];
    glReadPixels(px, H - 1 - py, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, c);
    size_t id = c[0] | c[1] << 8 | c[2] << 16;
    if (id >= 1 && id <= ids.size()) {
        LoadedBlock& b = *ids[id - 1].first;
        const WorldDraw& d = b.mesh.draws[ids[id - 1].second];
        printf("pick %d,%d: block %d,%d draw %zu tex %u flags 0x%02X list %u alpha %.2f verts %u anim %d\n", px, py, b.c, b.r,
               ids[id - 1].second, d.texture, d.flags, d.list, d.alpha, d.count, d.anim);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glBindBuffer(GL_ARRAY_BUFFER, b.vbo);
        glVertexPointer(3, GL_FLOAT, sizeof(WorldVertex), (void*)0);
        if (d.flags & 0x10) glDisable(GL_CULL_FACE); else glEnable(GL_CULL_FACE);
        for (uint32_t t = 0; t < d.count / 3; ++t) {
            uint32_t k = t + 1;
            glColor3ub((GLubyte)(k & 0xFF), (GLubyte)(k >> 8 & 0xFF), (GLubyte)(k >> 16 & 0xFF));
            glDrawArrays(GL_TRIANGLES, (GLint)(d.first + t * 3), 3);
        }
        glReadPixels(px, H - 1 - py, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, c);
        uint32_t tri = c[0] | c[1] << 8 | c[2] << 16;
        if (tri >= 1) {
            WorldVertex v[3];
            glGetBufferSubData(GL_ARRAY_BUFFER, (GLintptr)((d.first + (tri - 1) * 3) * sizeof(WorldVertex)), sizeof v, v);
            for (auto& q : v)
                printf("  pos %.2f %.2f %.2f  n %.2f %.2f %.2f  uv %.4f %.4f\n", q.x, q.y, q.z, q.nx, q.ny, q.nz, q.u, q.v);
        }
    } else printf("pick %d,%d: nothing\n", px, py);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glDisableClientState(GL_VERTEX_ARRAY);
    glDisable(GL_CULL_FACE);
}
