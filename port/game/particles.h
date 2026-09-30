// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kryo.to
// See LICENSE in the repository root.
// Particles: a port of cParticleEmitterBase and the emitters the vehicles use.
//
// An emitter owns up to N particles of 28 bytes (sParticle): i16 position / velocity (fractions of the emitter's range,
// Q12), u16 angle + i16 spin, RGB555 colour, i16 size + growth, u16 life (-2 a frame), alpha 0..31 (+ step), flags.
// Positions are relative to the emitter; when the emitter moves a little (< 7 units) the particles are shifted back
// so they stay where they were emitted (cParticleEmitterBase::SetPos / ParticleUpdateLoop). A particle dies when its
// life runs out or it drifts 7 ranges away. Each one is drawn as a quad (ManagedRender) - facing the camera
// (billboard) or lying flat - with one rect of the global sprite sheet (texture 2397, cAssetManager, 26 rects).
#pragma once
#include <cstdint>
#include <memory>
#include <vector>

struct WorldCamera;

struct Particle {   // sParticle
    int16_t p[3], v[3];
    uint16_t angle; int16_t spin;
    uint16_t colour;
    int16_t size, grow;
    uint16_t life;
    uint8_t active, alpha; int8_t alphaStep; uint8_t flags;
};

class Emitter {
public:
    Emitter(const int32_t pos[3], int count, int32_t range, uint8_t sprite, bool billboard);
    virtual ~Emitter() = default;
    void setPos(const int32_t p[3]);          // cParticleEmitterBase::SetPos
    void process();                           // Process -> ParticleUpdateLoop(true)
    void render(const WorldCamera& cam) const;
    bool finished() const { return dying && !alive_; }
    int32_t pos[3];
    bool dying = false;                       // remove once the last particle is gone (flag 0x20)
protected:
    virtual void updateParticle(Particle& p);  // cParticleEmitterBase::UpdateParticle
    void addFromData(const Particle& d);      // AddParticleFromData
    int32_t range_, inv_;                     // +0xD0 = 1 / range
    uint8_t sprite_;
    bool billboard_;
    std::vector<Particle> parts_;
    uint8_t next_ = 0;
    int alive_ = 0;
    int32_t moved_[3] = {0, 0, 0};           // +0xC0 / +0xC8
};

// cParticleEmitterSmoke: range 20, sprite 1, billboard; colours by vehicle damage 0 light .. 3 black
class SmokeEmitter : public Emitter {
public:
    SmokeEmitter(const int32_t pos[3], int count) : Emitter(pos, count, 0x14000, 1, true) {}
    void addParticle(const int32_t vel[3], int colour);
protected:
    void updateParticle(Particle& p) override;
private:
    Particle tmpl_{};
    bool init_ = false;
};

// cParticleEmitterSteam: range 7, 15 particles, sprite 21, camera-facing; while on, a puff every 8 frames that
// rises, grows and fades (city emitters of type 0: steam from vents and manholes)
class SteamEmitter : public Emitter {
public:
    SteamEmitter(const int32_t pos[3], bool on) : Emitter(pos, 15, 0x70000, 21, true), on_(on) {}
    void tick(uint32_t frame);                 // cParticleEmitterSteam::Process (before Emitter::process)
protected:
    void updateParticle(Particle& p) override;
private:
    void addParticle();
    Particle tmpl_{};
    bool on_, init_ = false;
};

// cParticleEmitterFire: ten short-lived, shrinking flames, range four, sprite 12.
class FireEmitter : public Emitter {
public:
    explicit FireEmitter(const int32_t pos[3]) : Emitter(pos, 10, 0x4000, 12, true) {}
    void addParticle();
protected:
    void updateParticle(Particle& p) override;
};

class ExplosionFlash : public Emitter {
public:
    explicit ExplosionFlash(const int32_t pos[3]);
protected:
    void updateParticle(Particle& p) override;
};

class ExplosionCloud : public Emitter {
public:
    explicit ExplosionCloud(const int32_t pos[3]);
protected:
    void updateParticle(Particle& p) override;
};

class ExplosionDebris : public Emitter {
public:
    explicit ExplosionDebris(const int32_t pos[3]);
protected:
    void updateParticle(Particle& p) override;
};

class Particles {
public:
    template <class T, class... A> T* add(A&&... a) {
        emitters_.push_back(std::make_unique<T>(std::forward<A>(a)...));
        return static_cast<T*>(emitters_.back().get());
    }
    void remove(const Emitter* e);   // (the emitter keeps going until its particles are gone)
    void update(uint32_t frame);
    void render(const WorldCamera& cam) const;
private:
    std::vector<std::unique_ptr<Emitter>> emitters_;
};

Particles& TheParticles();

// immsprite3d::RenderWorldScale*: one rect of the sprite sheet as a quad centred on pos, spanning +-sx along ax and
// +-sy along ay, colour 0xAABBGGRR (alpha blended, no depth write)
void DrawSheetSprite(int sprite, uint32_t abgr, const float pos[3], const float ax[3], const float ay[3], float sx, float sy);
