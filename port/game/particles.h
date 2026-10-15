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
class Particles;

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
    friend class Particles;
    friend struct ParticlesTestAccess;
    virtual void updateParticle(Particle& p);  // cParticleEmitterBase::UpdateParticle
    void addFromData(const Particle& d);      // AddParticleFromData
    int32_t range_, inv_;                     // +0xD0 = 1 / range
    uint8_t sprite_;
    bool billboard_;
    bool additive_ = false;                  // cEffectRenderable +0x11: emissive pass before ordinary alpha
    std::vector<Particle> parts_;
    uint8_t next_ = 0;
    int alive_ = 0;
    int32_t moved_[3] = {0, 0, 0};           // +0xC0 / +0xC8
    uint32_t frame_ = 0;
    Particles* owner_ = nullptr;
    int32_t smokeOrigin_[3]{};
};

// cParticleEmitterWaterSplash, type 1: the horizontal spray at a water stream's source.
class WaterSplashEmitter : public Emitter {
public:
    WaterSplashEmitter(const int32_t pos[3], const int32_t velocity[3], uint16_t colour);
    void direction(const int32_t velocity[3]);
    void addParticle() { addFromData(tmpl_); }
    void tick(uint32_t frame) { frame_=frame; Emitter::process(); }
protected:
    void updateParticle(Particle& particle) override;
private:
    Particle tmpl_{};
};

// cParticleEmitterSmoke: range 20, sprite 1, billboard; colours by vehicle damage 0 light .. 3 black
class SmokeEmitter : public Emitter {
public:
    SmokeEmitter(const int32_t pos[3], int count) : Emitter(pos, count, 0x14000, 1, true) {}
    void addParticle(const int32_t vel[3], int colour);
    void addParticleFromData(const Particle& p) { addFromData(p); }
    bool followPlayer = false; // explosion smoke belongs to the local player's general particle controller
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
    // AddParticle(pos, colour, scale): one cloud at pos in an RGB555 colour, 0.5..0.7 x scale wide, rising
    // 0.05..0.1 a frame for 200 life steps (cGeneralParticleController::AddSteamCloud)
    void addCloud(const int32_t at[3], uint16_t colour, int16_t scale);
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
    explicit FireEmitter(const int32_t pos[3]) : Emitter(pos, 10, 0x4000, 12, true) { additive_ = true; }
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

// cParticleEmitterSmashedSlats: six long strips from the two sides of a broken gate.
class SlatsEmitter : public Emitter {
public:
    SlatsEmitter(const int32_t pos[3], const int32_t velocity[3], const int32_t endA[3], const int32_t endB[3],
                 uint32_t colourA, uint32_t colourB, bool textured, bool slow, uint32_t ambient);
    void render(const WorldCamera& cam) const override;
protected:
    void updateParticle(Particle& p) override;
    void vertices(const Particle& p, float out[4][3]) const;
};

class ExplosionFlash : public Emitter {
public:
    explicit ExplosionFlash(const int32_t pos[3], int type = 4, int32_t scale = 4096);
protected:
    void updateParticle(Particle& p) override;
};

class ExplosionCloud : public Emitter {
public:
    // cParticleEmitterComplexExplsn for generic/ordinary vehicle blasts (types 0/4).
    // Phase zero is the main cloud; other phases are random bursts. Big-vehicle circles remain unported.
    ExplosionCloud(const int32_t pos[3], int type = 4, unsigned phase = 1, int16_t scale = 4096,
                   int32_t vx = 0, int32_t vy = 0, unsigned count = 8);
    void process(uint32_t hours = 12u << 12) override;
protected:
    void updateParticle(Particle& p) override;
private:
    int16_t scale_;
    std::vector<Particle> smoke_;
};

class ExplosionDebris : public Emitter {
public:
    explicit ExplosionDebris(const int32_t pos[3], int type = 4);
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

// cParticleEmitterBlood: the local general particle controller's 16 blood drops (range 0x70000, flat quads of
// sprite 0), attached to the player like the rain. AddParticle(cSimpleMover) puts one dark red, spinning drop at the
// victim for ten frames (cParticleEmitterBase::UpdateParticle, no gravity). At this range the velocities scale to at
// most a few position units a frame, so the drops barely drift. The cut-in-half offset (cSimpleMover +0x348 bit 7)
// is not reachable: CutPedInHalf is not ported.
class BloodEmitter : public Emitter {
public:
    explicit BloodEmitter(const int32_t pos[3]) : Emitter(pos, 16, 0x70000, 0, false) {}
    void addParticle(const int32_t victim[3]);
private:
    Particle tmpl_{};
    bool init_ = false;
};

// cParticleEmitterPetrolFlame: five flat, additive flames of sprite 5 (range 4) under a molotov's central fire.
// Each new flame takes a random angle, a size from the fire's width, and fades in by 3 until its alpha passes 7, then
// out by 2; it shrinks by half a range a frame. The template keeps its last (scaled) position and climb.
class PetrolFlameEmitter : public Emitter {
public:
    PetrolFlameEmitter(const int32_t pos[3], int16_t size)
        : Emitter(pos, 5, 0x4000, 5, false), size_(size) { additive_ = true; }
    void addParticle();   // AddParticle(cSimpleMover*)
protected:
    void updateParticle(Particle& p) override;
private:
    Particle tmpl_{};
    bool init_ = false;
    int16_t size_;
};

// cParticleEmitterSparks: the local general particle controller's 15 sparks (range 7, flat additive quads of
// sprite 10). AddParticle puts one at pos moving along dir (Q12 units a frame, optionally jittered by +-0.1 across),
// turned to face its motion (less angle when useAngle), alpha 1 + 30 x scale, a colour of base plus a random part
// within each channel of colourRand, fading by 2 a step; each frame its colour brightens towards white.
class SparksEmitter : public Emitter {
public:
    explicit SparksEmitter(const int32_t pos[3], int count = 15) : Emitter(pos, count, 0x70000, 10, false) {
        additive_ = true;
    }
    void addParticle(const int32_t at[3], const int16_t dir[3], uint8_t life, bool useAngle, int16_t angle, bool jitter,
                     uint16_t colour, uint16_t colourRand, int16_t scale);
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
        emitters_.back()->owner_ = this;
        return static_cast<T*>(emitters_.back().get());
    }
    void remove(const Emitter* e);   // (the emitter keeps going until its particles are gone)
    void update(uint32_t frame, uint32_t hours = 12u << 12, const int32_t* playerPos = nullptr);
    void render(const WorldCamera& cam) const;
    int emitterCount() const { return (int)emitters_.size(); }   // the debug overlay
    bool contains(const Emitter* e) const;   // a weak pointer's check: the emitter has not been removed
private:
    std::vector<std::unique_ptr<Emitter>> emitters_;
    friend struct ParticlesTestAccess;
};

Particles& TheParticles();

// immsprite3d::RenderWorldScale*: one rect of the sprite sheet as a quad centred on pos, spanning +-sx along ax and
// +-sy along ay, colour 0xAABBGGRR (alpha blended, or GL_SRC_ALPHA/GL_ONE when additive; no depth write)
void DrawSheetSprite(int sprite, uint32_t abgr, const float pos[3], const float ax[3], const float ay[3], float sx, float sy,
                     bool additive = false);
