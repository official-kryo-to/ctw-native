#include "particles.h"
#include "cargens.h"   // Rand32Critical
#include "gfx/assets.h"
#include "world/worldrenderer.h"
#include <glad/gl.h>
#include <algorithm>
#include <cmath>
#include <cstdlib>

static Particles g_particles;
Particles& TheParticles() { return g_particles; }

// cAssetManager (resource gGameDir[9]): the sprite sheet texture and its rects {x, y, w, h} in pixels
static const uint16_t kSheetTexture = 2397;
static const uint16_t kSheet[26][4] = {
    {206, 70, 32, 32}, {138, 138, 63, 64}, {206, 2, 32, 64}, {110, 206, 16, 32}, {130, 206, 16, 32}, {2, 2, 64, 64},
    {70, 2, 64, 64}, {138, 2, 64, 64}, {205, 138, 32, 32}, {150, 206, 16, 32}, {244, 106, 8, 8}, {2, 70, 64, 64},
    {2, 206, 32, 32}, {70, 70, 64, 64}, {138, 70, 64, 64}, {2, 138, 64, 64}, {244, 118, 2, 1}, {70, 138, 64, 64},
    {170, 206, 16, 32}, {226, 106, 14, 14}, {190, 206, 16, 32}, {38, 206, 32, 32}, {242, 82, 8, 7}, {242, 70, 8, 8},
    {206, 106, 16, 16}, {74, 206, 32, 32}};

static uint32_t randNC(uint32_t n) { return Rand32Critical(n); }   // Rand32NonCritical

Emitter::Emitter(const int32_t p[3], int count, int32_t range, uint8_t sprite, bool billboard)
    : range_(range), inv_(range ? (int32_t)((0x100000000000LL / range) >> 20) : 0), sprite_(sprite), billboard_(billboard) {
    pos[0] = p[0]; pos[1] = p[1]; pos[2] = p[2];
    parts_.assign((size_t)std::max(count, 1), Particle{});
}

void Emitter::setPos(const int32_t p[3]) {   // small moves: remember the offset so the particles stay put
    int32_t d[3] = {p[0] - pos[0], p[1] - pos[1], p[2] - pos[2]};
    moved_[0] = d[0]; moved_[1] = d[1]; moved_[2] = d[2];
    if (std::abs(d[0]) < 0x7001 && std::abs(d[1]) < 0x7001 && std::abs(d[2]) < 0x7001) return;
    pos[0] = p[0]; pos[1] = p[1]; pos[2] = p[2];
    for (Particle& q : parts_) q.active = 0;   // (a teleport: the game resets its particles)
    alive_ = 0;
    moved_[0] = moved_[1] = moved_[2] = 0;
}

void Emitter::addFromData(const Particle& d) {   // cParticleEmitterBase::AddParticleFromData
    int n = (int)parts_.size();
    if (next_ == 0xFF) next_ = 0;
    int i = next_;
    if (parts_[i].active) {
        int j = i;
        do { j = j + 1 < n ? j + 1 : 0; } while (parts_[j].active && j != i);
        if (parts_[j].active) { next_ = (uint8_t)(i + 1 < n ? i + 1 : 0); return; }
        i = j;
    }
    parts_[i] = d;
    parts_[i].active = 1;
    ++alive_;
    next_ = (uint8_t)(i + 1 < n ? i + 1 : 0);
}

void Emitter::updateParticle(Particle& q) {   // cParticleEmitterBase::UpdateParticle
    for (int k = 0; k < 3; ++k) q.p[k] = (int16_t)(q.p[k] + q.v[k]);
    uint16_t life = (uint16_t)(q.life - 2);
    q.angle = (uint16_t)(q.angle + q.spin);
    q.life = life;
    q.size = (int16_t)(q.size + q.grow);
    if (q.alphaStep) {
        int a = std::clamp((int)(int8_t)q.alpha + q.alphaStep, 1, 31);
        q.alpha = (uint8_t)a;
        if (life < 3 && a > 1) q.life = life | 4;
    }
}

void Emitter::process() {   // Process -> ParticleUpdateLoop(true)
    if (moved_[0] || moved_[1] || moved_[2]) {
        for (int k = 0; k < 3; ++k) pos[k] += moved_[k];
    }
    for (Particle& q : parts_) {
        if (!q.active) continue;
        for (int k = 0; k < 3; ++k) q.p[k] = (int16_t)(q.p[k] - (int16_t)((uint32_t)(moved_[k] * inv_) >> 12));
        bool out = false;
        for (int k = 0; k < 3; ++k) out |= std::abs((q.size + q.p[k]) >> 12) >= 7;
        if (out || q.life <= 2) { q.life = 0; q.active = 0; --alive_; continue; }
        updateParticle(q);
    }
    moved_[0] = moved_[1] = moved_[2] = 0;
}

void Emitter::render(const WorldCamera& cam) const {   // cParticleEmitterBase::ManagedRender
    if (!alive_) return;
    GLuint tex = Assets_Texture(kSheetTexture);
    int tw = 256, th = 256;
    Assets_TextureSize(kSheetTexture, &tw, &th);
    const uint16_t* r = kSheet[sprite_ < 26 ? sprite_ : 0];
    float u0 = r[0] / (float)tw, v0 = r[1] / (float)th, u1 = (r[0] + r[2]) / (float)tw, v1 = (r[1] + r[3]) / (float)th;
    if (tex) { glEnable(GL_TEXTURE_2D); glBindTexture(GL_TEXTURE_2D, tex); } else glDisable(GL_TEXTURE_2D);
    float R = range_ / 4096.f;
    glBegin(GL_QUADS);
    for (const Particle& q : parts_) {
        if (!q.active) continue;
        float s = q.size / 4096.f * R;
        float a = q.angle * 9.587378e-05f;
        float sn = sinf(a) * s, cs = cosf(a) * s;
        float e1[3], e2[3];
        if (billboard_) {
            for (int k = 0; k < 3; ++k) {
                e1[k] = sn * cam.right[k] - cs * cam.up[k];
                e2[k] = cs * cam.right[k] + sn * cam.up[k];
            }
        } else {
            e1[0] = sn; e1[1] = -cs; e1[2] = 0;
            e2[0] = cs; e2[1] = sn; e2[2] = 0;
        }
        if ((q.flags | 2) == 3) for (float& v : e1) v = -v;
        if ((q.flags & 0xFE) == 2) for (float& v : e2) v = -v;
        float c[3];
        for (int k = 0; k < 3; ++k) c[k] = pos[k] / 4096.f + q.p[k] / 4096.f * R - e1[k] - e2[k];
        uint16_t col = q.colour;
        glColor4ub((GLubyte)((col & 0x1F) << 3), (GLubyte)((col >> 5 & 0x1F) << 3), (GLubyte)((col >> 10 & 0x1F) << 3),
                   (GLubyte)(q.alpha * 255 / 31));
        glTexCoord2f(u0, v0); glVertex3f(c[0], c[1], c[2]);
        glTexCoord2f(u0, v1); glVertex3f(c[0] + 2 * e1[0], c[1] + 2 * e1[1], c[2] + 2 * e1[2]);
        glTexCoord2f(u1, v1); glVertex3f(c[0] + 2 * e1[0] + 2 * e2[0], c[1] + 2 * e1[1] + 2 * e2[1], c[2] + 2 * e1[2] + 2 * e2[2]);
        glTexCoord2f(u1, v0); glVertex3f(c[0] + 2 * e2[0], c[1] + 2 * e2[1], c[2] + 2 * e2[2]);
    }
    glEnd();
}

// ============================================================================================== smoke
void SmokeEmitter::addParticle(const int32_t vel[3], int colour) {   // cParticleEmitterSmoke::AddParticle(tv3d, colour)
    if (!init_) {
        tmpl_ = Particle{};
        tmpl_.life = 0x32;
        tmpl_.alpha = 0; tmpl_.alphaStep = 2;
        tmpl_.grow = (int16_t)((uint32_t)(inv_ * 0x333) >> 12);
        init_ = true;
    }
    tmpl_.p[0] = (int16_t)((uint32_t)(inv_ * ((int)randNC(0x1000) - 0x800)) >> 12);
    tmpl_.p[1] = (int16_t)((uint32_t)(((int)randNC(0x1000) - 0x800) * inv_) >> 12);
    tmpl_.p[2] = 0;
    tmpl_.colour = 0x7FFF;
    tmpl_.size = (int16_t)(((int64_t)inv_ * (int64_t)(randNC(0x19A) + 0x599)) >> 12);
    tmpl_.grow = (int16_t)((uint32_t)(inv_ * 0x333) >> 12);
    switch (colour) {   // eVehicleSmokeColour
        case 0: tmpl_.alphaStep = 0xA; break;
        case 1: tmpl_.alphaStep = 0x10; tmpl_.colour = 0x5294; break;
        case 2: tmpl_.alphaStep = 0x16; tmpl_.colour = 0x3DEF; break;
        case 3: tmpl_.alphaStep = 0x19; tmpl_.colour = 0x294A; break;
    }
    int32_t v[3];
    for (int k = 0; k < 3; ++k) v[k] = (int32_t)(((int64_t)(int32_t)(((int64_t)vel[k] * 0x66600000LL) >> 32) * inv_) >> 12);
    for (int k = 0; k < 3; ++k)   // DoesV3dOverflowV3d16
        if (std::abs((v[k] + tmpl_.size) >> 12) > 6) return;
    tmpl_.v[0] = (int16_t)v[0];
    tmpl_.v[1] = (int16_t)v[1];
    tmpl_.v[2] = (int16_t)((uint32_t)(inv_ * 0x4CD) >> 12);
    addFromData(tmpl_);
}

void SmokeEmitter::updateParticle(Particle& q) {   // cParticleEmitterSmoke::UpdateParticle
    for (int k = 0; k < 3; ++k) q.p[k] = (int16_t)(q.p[k] + q.v[k]);
    uint16_t oldLife = q.life;
    uint16_t life = (uint16_t)(oldLife - 2);
    q.angle = (uint16_t)(q.angle + q.spin);
    q.life = life;
    q.size = (int16_t)(q.size + q.grow);
    int a = (int8_t)q.alpha;
    int na;
    if (life < 0x14) na = a - 1;
    else na = a < q.alphaStep ? a + 2 : a;
    int c = std::clamp(na, 0, 31);
    q.alpha = (uint8_t)c;
    if (life < 5 && c > 1) { q.life = (uint16_t)(oldLife + 2); return; }
    if (na < 1) q.life = 0;
}

// ============================================================================================== all emitters
void Particles::remove(const Emitter* e) {
    for (auto& p : emitters_)
        if (p.get() == e) p->dying = true;
}

void Particles::update() {
    for (auto& e : emitters_) e->process();
    emitters_.erase(std::remove_if(emitters_.begin(), emitters_.end(), [](const std::unique_ptr<Emitter>& e) { return e->finished(); }),
                    emitters_.end());
}

void Particles::render(const WorldCamera& cam) const {
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);
    glDisable(GL_LIGHTING);
    glDisable(GL_CULL_FACE);
    glEnable(GL_DEPTH_TEST);
    for (const auto& e : emitters_) e->render(cam);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    glDisable(GL_TEXTURE_2D);
}

void DrawSheetSprite(int sprite, uint32_t c, const float p[3], const float ax[3], const float ay[3], float sx, float sy) {
    GLuint tex = Assets_Texture(kSheetTexture);
    int tw = 256, th = 256;
    Assets_TextureSize(kSheetTexture, &tw, &th);
    const uint16_t* r = kSheet[sprite >= 0 && sprite < 26 ? sprite : 0];
    float u0 = r[0] / (float)tw, v0 = r[1] / (float)th, u1 = (r[0] + r[2]) / (float)tw, v1 = (r[1] + r[3]) / (float)th;
    if (tex) { glEnable(GL_TEXTURE_2D); glBindTexture(GL_TEXTURE_2D, tex); } else glDisable(GL_TEXTURE_2D);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);
    glDisable(GL_LIGHTING);
    glDisable(GL_CULL_FACE);
    glColor4ub((GLubyte)(c & 0xFF), (GLubyte)(c >> 8 & 0xFF), (GLubyte)(c >> 16 & 0xFF), (GLubyte)(c >> 24));
    glBegin(GL_QUADS);
    glTexCoord2f(u0, v0); glVertex3f(p[0] - ax[0] * sx - ay[0] * sy, p[1] - ax[1] * sx - ay[1] * sy, p[2] - ax[2] * sx - ay[2] * sy);
    glTexCoord2f(u1, v0); glVertex3f(p[0] + ax[0] * sx - ay[0] * sy, p[1] + ax[1] * sx - ay[1] * sy, p[2] + ax[2] * sx - ay[2] * sy);
    glTexCoord2f(u1, v1); glVertex3f(p[0] + ax[0] * sx + ay[0] * sy, p[1] + ax[1] * sx + ay[1] * sy, p[2] + ax[2] * sx + ay[2] * sy);
    glTexCoord2f(u0, v1); glVertex3f(p[0] - ax[0] * sx + ay[0] * sy, p[1] - ax[1] * sx + ay[1] * sy, p[2] - ax[2] * sx + ay[2] * sy);
    glEnd();
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
}
