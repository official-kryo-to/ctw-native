// Vehicle/model browser: loads cModel resources straight out of game.pak and draws them textured and lit with the
// model's normals, drawing the batches of one colour variant (palette) like cModelInstance::Render.
#include "modelview.h"
#include "os/os.h"
#include "os/pak.h"
#include "gfx/model.h"
#include "gfx/assets.h"
#include <map>
#include <SDL.h>
#include <glad/gl.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

static bool g_ok = false;
static std::vector<int> g_ids;
static int g_cur = 0;
static Model g_model;
// A vehicle instance draws the batches whose mask has its palette bit set (instance mask = 1 << palette,
// cVehicleModelInstance::SetColour / cModelInstance::Render). Palettes 0..25 are tried like HasVariance does.
static std::vector<int> g_palettes;     // palette indices this model has geometry for
static int g_variant = 0;               // index into g_palettes
static bool g_wire = false;
static bool g_partColours = false;   // debug: tint each node (door, wheel, rotor...) differently
static bool g_textured = true;
// Vehicle info table (gGameDir[4], resource 1994): u32 count, then 0x138-byte records {u16 type, u16 model id,
// u32 allowed-palette mask, char name[...]}. cVehicleInfo::RandomPalette picks from bits 0..24 and falls back to
// palette 26; cVehicle::SetDead switches to palette 25 (the burnt-out wreck).
struct VehicleInfo { std::string name; uint32_t paletteMask; };
static std::map<int, VehicleInfo> g_vehInfo;   // model id -> info
static const int kPalDefault = 26, kPalWrecked = 25;
static GLuint textureFor(int id) { return Assets_Texture(id); }
static bool g_extras = true;    // batches on nodes > 0 (wheels, rotors, ...)
static float g_yaw = 35.f, g_pitch = 20.f, g_dist = 9.f;
static int g_lastX = 0, g_lastY = 0;
static bool g_dragging = false;

static void updateTitle() {
    char t[400];
    std::string pal = "-";
    if (!g_palettes.empty()) {
        int p = g_palettes[g_variant];
        pal = std::to_string(p) + (p == kPalWrecked ? " wrecked" : p == kPalDefault ? " default" : "") + " (" +
              std::to_string(g_variant + 1) + "/" + std::to_string(g_palettes.size()) + ")";
    }
    auto vi = g_vehInfo.find(g_ids[g_cur]);
    std::string name = vi != g_vehInfo.end() ? " " + vi->second.name : "";
    snprintf(t, sizeof t, "CTW models [%d/%zu] id %d%s  verts %zu  batches %zu  palette %s  parts %s  (Tab: audio, Left/Right, V: palette, G: parts, T: texture, C: part colours, W: wire, drag: rotate, wheel: zoom)",
             g_cur + 1, g_ids.size(), g_ids[g_cur], name.c_str(), g_model.verts.size(), g_model.batches.size(),
             pal.c_str(), g_extras ? "on" : "off");
    Host_SetTitle(t);
    if (getenv("CTW_DEBUG")) printf("%s\n", t);
}

static void loadCurrent() {
    std::vector<uint8_t> raw, head;
    g_model = Model();
    // The table-derived size can be bogus (a sentinel follows some entries), so size the read from the model header.
    if (Assets_Pak().readPrefix((uint32_t)g_ids[g_cur], 8, head) && head.size() == 8) {
        uint16_t A, D;
        memcpy(&A, &head[2], 2);
        memcpy(&D, &head[6], 2);
        size_t need = 16 + (size_t)head[4] * 32 + (size_t)D * 16 + (size_t)head[5] * 12 + (size_t)A * 192;
        if (Assets_Pak().readPrefix((uint32_t)g_ids[g_cur], need, raw)) g_model.parse(raw);
    }
    // Palette order: the ones the game can randomly pick for this vehicle, then its default livery, then the wreck.
    auto hasVariance = [](int pal) {
        for (auto& b : g_model.batches)
            if (b.mask & (1u << pal)) return true;
        return false;
    };
    g_palettes.clear();
    auto vi = g_vehInfo.find(g_ids[g_cur]);
    uint32_t allowed = vi != g_vehInfo.end() ? vi->second.paletteMask : 0xFFFFFFFFu;
    for (int pal = 0; pal < 25; ++pal)
        if ((allowed >> pal & 1) && hasVariance(pal)) g_palettes.push_back(pal);
    if (hasVariance(kPalDefault)) g_palettes.push_back(kPalDefault);
    if (hasVariance(kPalWrecked)) g_palettes.push_back(kPalWrecked);
    for (int pal = 0; pal < 32; ++pal)   // anything else the model has geometry for
        if (hasVariance(pal) && std::find(g_palettes.begin(), g_palettes.end(), pal) == g_palettes.end()) g_palettes.push_back(pal);
    g_variant = 0;
    float ext = 0;
    for (int i = 0; i < 3; ++i) ext = std::max(ext, g_model.bboxMax[i] - g_model.bboxMin[i]);
    g_dist = std::max(ext * 1.4f, 3.f);
    updateTitle();
}

bool ModelView_Init(const std::string& dataDir) {
    if (!Assets_Open(dataDir)) return false;
    std::vector<uint8_t> vi;
    if (Assets_Pak().read(1994, vi) && vi.size() >= 4) {
        uint32_t n;
        memcpy(&n, vi.data(), 4);
        for (uint32_t i = 0; i < n && 4 + (size_t)(i + 1) * 0x138 <= vi.size(); ++i) {
            const uint8_t* r = &vi[4 + (size_t)i * 0x138];
            uint16_t model;
            VehicleInfo info;
            memcpy(&model, r + 2, 2);
            memcpy(&info.paletteMask, r + 4, 4);
            info.name.assign((const char*)r + 8, strnlen((const char*)r + 8, 32));
            size_t dot = info.name.find(".vehicle");
            if (dot != std::string::npos) info.name.resize(dot);
            g_vehInfo[model] = info;
        }
    }
    // Models are the resources whose data begins with "MG" (426 of them: vehicles, then peds/props/world pieces).
    for (int id = 0; (uint32_t)id < Assets_Pak().count(); ++id) {
        std::vector<uint8_t> head;
        if (Assets_Pak().readPrefix((uint32_t)id, 8, head) && head.size() == 8 && head[0] == 'M' && head[1] == 'G') g_ids.push_back(id);
    }
    g_ok = !g_ids.empty();
    if (g_ok) loadCurrent();
    return g_ok;
}

void ModelView_Enter() { if (g_ok) updateTitle(); }

void ModelView_Select(int id) {
    for (size_t i = 0; i < g_ids.size(); ++i)
        if (g_ids[i] == id) { g_cur = (int)i; loadCurrent(); }
}

void ModelView_SetCamera(float yaw, float pitch) { g_yaw = yaw; g_pitch = pitch; }

void ModelView_Key(int k) {
    if (!g_ok) return;
    int n = (int)g_ids.size();
    if (k == SDL_SCANCODE_RIGHT) { g_cur = (g_cur + 1) % n; loadCurrent(); }
    else if (k == SDL_SCANCODE_LEFT) { g_cur = (g_cur + n - 1) % n; loadCurrent(); }
    else if (k == SDL_SCANCODE_V) {
        if (!g_palettes.empty()) g_variant = (g_variant + 1) % (int)g_palettes.size();
        updateTitle();
    }
    else if (k == SDL_SCANCODE_W) g_wire = !g_wire;
    else if (k == SDL_SCANCODE_C) g_partColours = !g_partColours;
    else if (k == SDL_SCANCODE_T) g_textured = !g_textured;
    else if (k == SDL_SCANCODE_G) { g_extras = !g_extras; updateTitle(); }
}

void ModelView_Update() {
    if (!g_ok) return;
    int mx, my;
    Host_GetMouse(&mx, &my);
    bool down = Host_MouseDown(0);
    if (down && g_dragging) {
        g_yaw += (mx - g_lastX) * 0.4f;
        g_pitch = std::max(-89.f, std::min(89.f, g_pitch + (my - g_lastY) * 0.4f));
    }
    g_dragging = down;
    g_lastX = mx; g_lastY = my;
    while (int w = Host_PopWheel()) g_dist = std::max(1.f, g_dist * (w > 0 ? 0.9f : 1.1f));
}

static void perspective(float fovyDeg, float aspect, float zn, float zf) {
    float t = zn * tanf(fovyDeg * 3.14159265f / 360.f);
    glFrustum(-t * aspect, t * aspect, -t, t, zn, zf);
}

void ModelView_Render() {
    int W = (int)OS_ScreenGetWidth(), H = (int)OS_ScreenGetHeight();
    glViewport(0, 0, W, H);
    glClearColor(0.12f, 0.13f, 0.16f, 1.f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    if (!g_ok || g_model.verts.empty()) return;

    glMatrixMode(GL_PROJECTION); glLoadIdentity(); perspective(45.f, (float)W / H, 0.1f, 200.f);
    glMatrixMode(GL_MODELVIEW); glLoadIdentity();
    glTranslatef(0, 0, -g_dist);
    glRotatef(g_pitch - 90.f, 1, 0, 0);   // model space is z-up, y-forward
    glRotatef(g_yaw, 0, 0, 1);
    float cx = (g_model.bboxMin[0] + g_model.bboxMax[0]) / 2, cy = (g_model.bboxMin[1] + g_model.bboxMax[1]) / 2,
          cz = (g_model.bboxMin[2] + g_model.bboxMax[2]) / 2;
    glTranslatef(-cx, -cy, -cz);

    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glEnable(GL_LIGHT0);
    glEnable(GL_NORMALIZE);
    glEnable(GL_COLOR_MATERIAL);
    glLightModeli(GL_LIGHT_MODEL_TWO_SIDE, GL_FALSE);
    glCullFace(GL_BACK);   // cGlInterface init: glCullFace(GL_BACK), default CCW front faces
    glAlphaFunc(GL_GREATER, 8.f / 256.f);   // the shaders' "if (gl_FragColor.a < 8.0 / 256.0) discard;"
    glPolygonMode(GL_FRONT_AND_BACK, g_wire ? GL_LINE : GL_FILL);

    static const float pal[6][3] = {{0.85f, 0.85f, 0.88f}, {0.9f, 0.55f, 0.25f}, {0.35f, 0.7f, 0.9f},
                                    {0.5f, 0.85f, 0.4f},   {0.85f, 0.4f, 0.7f},  {0.9f, 0.85f, 0.3f}};
    const float s = g_model.scale;
    // Material flags (cBucketManager::Draw/Render; the key holds flags << 10):
    //   0x07 lighting on, 0x08 translucent (goes to a later, blended render list), 0x10 no backface culling,
    //   0x20 no depth writes. Opaque batches are drawn first, like render list 0.
    for (int pass = 0; pass < 2; ++pass)
    for (const ModelBatch& b : g_model.batches) {
        uint32_t inst = g_palettes.empty() ? 0xFFFFFFFFu : (1u << g_palettes[g_variant]);
        if (!(b.mask & inst)) continue;
        if (b.node > 0 && !g_extras) continue;
        if (((b.flags & 0x08) != 0) != (pass == 1)) continue;
        if (b.flags & 0x10) glDisable(GL_CULL_FACE); else glEnable(GL_CULL_FACE);
        if (b.flags & 0x07) glEnable(GL_LIGHTING); else glDisable(GL_LIGHTING);
        glDepthMask((b.flags & 0x20) ? GL_FALSE : GL_TRUE);
        if (pass == 1) {
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            glEnable(GL_ALPHA_TEST);
        } else {
            glDisable(GL_BLEND);
            glDisable(GL_ALPHA_TEST);
        }
        GLuint tex = g_textured ? textureFor(b.texture) : 0;
        // Material alpha: cModelInstance::Render passes (a + a * instanceAlpha) >> 8 (0..31, instance alpha 31).
        float alpha = (float)((b.alpha + b.alpha * 31) >> 8) / 31.f;
        if (tex) { glEnable(GL_TEXTURE_2D); glBindTexture(GL_TEXTURE_2D, tex); glColor4f(1, 1, 1, alpha); }
        else {
            glDisable(GL_TEXTURE_2D);
            const float* c = pal[g_partColours ? b.node % 6 : 0];
            glColor3f(c[0], c[1], c[2]);
        }
        const NodeMatrix& nm = g_model.world[b.node];
        glBegin(GL_TRIANGLES);
        for (uint32_t i = 2; i < b.count; ++i) {
            const ModelVertex* v[3] = {&g_model.verts[b.firstVertex + i - 2], &g_model.verts[b.firstVertex + i - 1],
                                       &g_model.verts[b.firstVertex + i]};
            if (i & 1) std::swap(v[1], v[2]);   // strip winding alternates
            if (v[0]->x == v[1]->x && v[0]->y == v[1]->y && v[0]->z == v[1]->z) continue;   // degenerate (strip stitching)
            if (v[1]->x == v[2]->x && v[1]->y == v[2]->y && v[1]->z == v[2]->z) continue;
            if (v[0]->x == v[2]->x && v[0]->y == v[2]->y && v[0]->z == v[2]->z) continue;
            for (int k = 0; k < 3; ++k) {
                float p[3] = {v[k]->x * s, v[k]->y * s, v[k]->z * s};
                float n[3] = {v[k]->nx / 32767.f, v[k]->ny / 32767.f, v[k]->nz / 32767.f};
                float pp[3], nn[3];
                for (int r = 0; r < 3; ++r) {
                    pp[r] = nm.r[r][0] * p[0] + nm.r[r][1] * p[1] + nm.r[r][2] * p[2] + nm.t[r];
                    nn[r] = nm.r[r][0] * n[0] + nm.r[r][1] * n[1] + nm.r[r][2] * n[2];
                }
                glTexCoord2f(v[k]->u / 2048.f, v[k]->v / 2048.f);   // the game's shader: Out_UV = vUV * 0.000488 (1/2048)
                glNormal3fv(nn);   // the model's own vertex normals
                glVertex3fv(pp);
            }
        }
        glEnd();
    }
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    glDisable(GL_TEXTURE_2D);
    glDisable(GL_LIGHTING);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_ALPHA_TEST);
    glDepthMask(GL_TRUE);
}
