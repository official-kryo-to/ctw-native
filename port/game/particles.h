// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kryo.to
// See LICENSE in the repository root.
// Particles: a port of cParticleEmitterBase and vehicle/street-furniture emitters.
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
    virtual void process(uint32_t hours = 12u << 12); // Process -> ParticleUpdateLoop(true); time is Q12
    virtual void render(const WorldCamera& cam) const;
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

// cParticleEmitterPaper: one burst of horizontal quads, range four, sprite 9 (or untextured).
// Colours are 0xBBGGRR; ambient is cTimeCycle::Colour(0xD).
class PaperEmitter : public Emitter {
public:
    PaperEmitter(const int32_t pos[3], int count, int16_t size, uint32_t colourA, uint32_t colourB,
                 bool textured, uint32_t ambient);
    void process(uint32_t hours = 12u << 12) override;
protected:
    void updateParticle(Particle& p) override;
private:
    uint16_t colours_[2];
    int count_;
    int16_t size_;
    bool emitted_ = false;
};

// cParticleEmitterSmashedWood: horizontal quads; count scales with the collision radius, at most 12.
class WoodEmitter : public Emitter {
public:
    WoodEmitter(const int32_t pos[3], int16_t vx, int16_t vy, int32_t strength,
                uint32_t colourA, uint32_t colourB, bool tint, uint32_t ambient);
protected:
    void updateParticle(Particle& p) override;
private:
    bool moving_;
};

// cParticleEmitterGarbage: rubbish from a smashed bin. Unattached (smash effects 13 and 37) it bursts eight pieces;
// attached to its bin (effect 35) it spills four, then one every other frame while the bin moves faster than
// 2 units/s. Pieces take one of six colours, darker at night, fall with gravity 0x51 and stop at -3.5 units.
class GarbageEmitter : public Emitter {
public:
    GarbageEmitter(const int32_t pos[3], int16_t size, bool attached);
    void process(uint32_t hours = 12u << 12) override;
    void follow(const int32_t pos[3], int64_t speedSquared) { setPos(pos); speed2_ = speedSquared; }
    void release() { attached_ = false; dying = true; }   // the bin stopped or went away
protected:
    void updateParticle(Particle& p) override;
private:
    void create(uint32_t hours);   // CreateRandomParticle
    int16_t size_;
    bool attached_;
    uint16_t frames_ = 0;
    int64_t speed2_ = 0;
};

// cParticleEmitterSmashedGlass: two horizontal shards on the first Process, coloured by time of day.
class GlassEmitter : public Emitter {
public:
    explicit GlassEmitter(const int32_t pos[3]) : Emitter(pos,2,0x4000,19,false) { dying = true; }
    void process(uint32_t hours = 12u << 12) override;
protected:
    void updateParticle(Particle& p) override;
private:
    bool emitted_ = false;
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

// cParticleEmitterRain: the local player's rain (cGeneralParticleController, 63 drops, range 7, attached to the
// player). AddParticle(n) drops n new streaks 25 units up, within 17 units across, falling 2 units a frame (3 when
// n >= 5), each turned to face the camera; UpdateParticle wraps them within 3.5 ranges of the emitter and kills
// them below it. ManagedRender draws each as a 0.08-unit wide, 4-unit long quad of the centre column of effect
// sprite 20, in the time cycle's ambient colour (Colour(0xD)) at alpha 0x70.
class RainEmitter : public Emitter {
public:
    explicit RainEmitter(const int32_t pos[3]) : Emitter(pos, 63, 0x70000, 20, false) {}
    void addDrops(unsigned n, uint16_t cameraYaw);   // cParticleEmitterRain::AddParticle(uint)
    void render(const WorldCamera& cam) const override;
    uint32_t tint = 0xFF808080u;                     // Colour(0xD), set each frame
protected:
    void updateParticle(Particle& p) override;
private:
    Particle tmpl_{};
    bool init_ = false;
};

class Particles {
public:
    template <class T, class... A> T* add(A&&... a) {
        emitters_.push_back(std::make_unique<T>(std::forward<A>(a)...));
        return static_cast<T*>(emitters_.back().get());
    }
    void remove(const Emitter* e);   // (the emitter keeps going until its particles are gone)
    void update(uint32_t frame, uint32_t hours = 12u << 12);
    void render(const WorldCamera& cam) const;
    int emitterCount() const { return (int)emitters_.size(); }   // the debug overlay
private:
    std::vector<std::unique_ptr<Emitter>> emitters_;
    friend struct ParticlesTestAccess;
};

Particles& TheParticles();

// immsprite3d::RenderWorldScale*: one rect of the sprite sheet as a quad centred on pos, spanning +-sx along ax and
// +-sy along ay, colour 0xAABBGGRR (alpha blended, no depth write)
void DrawSheetSprite(int sprite, uint32_t abgr, const float pos[3], const float ax[3], const float ay[3], float sx, float sy);
