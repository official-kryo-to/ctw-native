// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kryo.to
// See LICENSE in the repository root.
#include "doors.h"
#include "fixedmath.h"
#include "game.h"
#include "particles.h"
#include "sound.h"
#include "gfx/assets.h"
#include <glad/gl.h>
#include <cmath>

namespace {
// cPropEffectRenderable: the effect list's entry for a prop that draws itself (ManagedRender), in the ordinary
// alpha pass; it goes once the prop has
class PropEffectRenderable : public Emitter {
public:
    explicit PropEffectRenderable(std::weak_ptr<const DoorProp> prop)
        : Emitter(prop.lock()->pos, 0, 0, 0, false), prop_(std::move(prop)) {}
    void process(uint32_t) override { if (prop_.expired()) dying = true; }
    void render(const WorldCamera&) const override {
        if (auto p = prop_.lock()) p->render();
    }
private:
    std::weak_ptr<const DoorProp> prop_;
};

int32_t mul12(int32_t a, int32_t b) { return (int32_t)(((int64_t)a * b) >> 12); }
}

std::shared_ptr<DoorProp> DoorProp::create(const int32_t pos[3], int heading, int32_t width, int32_t height, uint8_t hold,
                                           bool open) {
    auto door = std::make_shared<DoorProp>(pos, heading, width, height, hold);
    TheParticles().add<PropEffectRenderable>(std::weak_ptr<const DoorProp>(door));
    if (open) door->open(true);
    return door;
}

DoorProp::DoorProp(const int32_t p[3], int heading, int32_t width, int32_t height, uint8_t hold)
    : halfWidth_((int16_t)((uint16_t)width >> 1)), height_((int16_t)((uint16_t)height >> 1)), hold_(hold) {
    for (int k = 0; k < 3; ++k) pos[k] = p[k];
    // SimpleMover::SetHeading(degrees x 0xB6); the matrix's right, forward (as Player::setHeading) and up
    const float a = (int16_t)(heading * 0xB6) * 9.587378e-05f;
    fwd[0] = (int16_t)(sinf(a) * 4096.f);
    fwd[1] = (int16_t)(cosf(a) * 4096.f);
    fwd[2] = 0;
    right[0] = fwd[1];
    right[1] = (int16_t)-fwd[0];
    right[2] = 0;
}

void DoorProp::offsetInWorld(const int32_t l[3], int32_t out[3]) const {
    // the right / forward / up rows, each product >> 12 on its own (up is +z here)
    out[0] = pos[0] + mul12(l[0], right[0]) + mul12(l[1], fwd[0]);
    out[1] = pos[1] + mul12(l[0], right[1]) + mul12(l[1], fwd[1]);
    out[2] = pos[2] + mul12(l[2], 0x1000);
}

void DoorProp::open(bool autoClose) {
    if (state_ == 3) progress_ = (uint8_t)(100 - progress_);   // turned round while closing
    state_ = 1;
    autoClose_ = autoClose;
}

void DoorProp::close(bool autoClose) {
    if (state_ == 1) progress_ = (uint8_t)(100 - progress_);
    state_ = 3;
    autoClose_ = autoClose;
}

void DoorProp::process() {
    const uint8_t c = progress_;
    if (c == 0xFF) return;
    if (c == 0) {
        if (autoClose_) state_ = 4;
        else { state_ = 0; progress_ = 100; }
        return;
    }
    if (c <= 50 && state_ == 1) {   // open: hold it, or close by itself
        if (hold_ == 0) {
            if (!autoClose_) { state_ = 2; return; }
        } else {
            state_ = 2;
            if (hold_ - 1 != 0) { hold_ = (uint8_t)(hold_ - 1); return; }
            if (autoClose_) state_ = 3;   // ("auto closing")
            return;
        }
    } else if ((state_ | 2) != 3) return;
    progress_ = c > speed_ ? (uint8_t)(c - speed_) : 0;
}

void DoorProp::render() const {
    const unsigned c = progress_;
    if (c == 0 || c == 100) return;
    // how far it is open, 0 .. 0x1000: (c - 50) / 50 closed from 100 to 50, c / 25 from 25 down to 0
    int32_t open;
    if (c >= 0x33) open = 0x1000 - (int32_t)(((uint64_t)((c << 12) - 0x32000) * 0x51EB851Fu) >> 36);
    else open = (int32_t)((((uint64_t)(c << 12) * 0x51EB852u) >> 32) << 1);
    open = std::min((int32_t)(int16_t)open, 0x1000);
    int32_t d[3] = {fwd[0], fwd[1], fwd[2]};
    Normalise3(d);
    const int32_t w = halfWidth_, h = height_;
    const int32_t a = mul12(d[1], 0x266), e = mul12(d[0], 0x266);   // 0.15 along the facing
    const int32_t b = mul12(w, -d[0]), f = mul12(d[1], w);           // half the width to the left
    const int32_t hx = e - f + pos[0], hy = a - b + pos[1];          // the hinge
    const int32_t fx = hx + mul12(2 * f, open), fy = hy + mul12(2 * b, open);   // the free edge
    const int32_t px = e + f + pos[0], py = a + b + pos[1];          // the far side of the doorway
    const float top = (float)((int64_t)h << 5) / 65536.f;            // (z is not the prop's own)
    const float openTop = (float)((((int64_t)h * open) >> 7) & ~0xFLL) / 65536.f;
    int texture = 0;
    uint16_t quad[4], tri[4];
    if (!Assets_EffectSprite(20, texture, quad) || !Assets_EffectSprite(11, texture, tri)) return;
    int tw = 256, th = 256;
    Assets_TextureSize(texture, &tw, &th);
    const GLuint tex = Assets_Texture(texture);
    if (tex) { glEnable(GL_TEXTURE_2D); glBindTexture(GL_TEXTURE_2D, tex); } else glDisable(GL_TEXTURE_2D);
    glColor4ub(0, 0, 0, 0xFF);   // vertex colour 0xFF000000
    auto vtx = [&](int32_t x, int32_t y, float z, int u, int v) {
        glTexCoord2f(u / (float)tw, v / (float)th);
        glVertex3f(x / 4096.f, y / 4096.f, z);
    };
    glBegin(GL_TRIANGLE_STRIP);   // the opening: the rect turned so its u runs down the door
    vtx(hx, hy, top, quad[0], quad[1]);
    vtx(hx, hy, 0.f, quad[0] + quad[2], quad[1]);
    vtx(fx, fy, top, quad[0], quad[1] + quad[3]);
    vtx(fx, fy, 0.f, quad[0] + quad[2], quad[1] + quad[3]);
    glEnd();
    const int tu = (uint16_t)(tri[0] + (tri[2] >> 1)), tv = (uint16_t)(tri[1] + (tri[3] >> 1));
    glBegin(GL_TRIANGLE_STRIP);
    vtx(px, py, 0.f, tu, tv);
    vtx(hx, hy, 0.f, tu, tv);
    vtx(hx, hy, openTop, tu, tv);
    glEnd();
}

// ---------------------------------------------------------------------------------------------- cDoorHandler

void DoorHandler::start(Game& g, const int32_t pos[3], int heading, Action action, std::function<void()> done,
                        bool placeInside) {
    // CreateDoor: WorldImpl::CreateDoor(0, false, 2.5 wide, 4 high, no hold) (cDoorHandler's +0xF8 / +0xFC / +0x100);
    // it cannot be harmed or targeted, and nothing collides with it
    auto door = DoorProp::create(pos, heading, 0x2800, 0x4000, 0, false);
    g.doors.push_back(door);
    door_ = door.get();
    done_ = std::move(done);
    doMainStuff(g, action, placeInside);
}

void DoorHandler::doMainStuff(Game& g, Action action, bool placeInside) {
    Player& p = g.player;
    static const int32_t kFront[3] = {0, 0x2000, 0}, kBehind[3] = {0, -0x2000, 0};   // qword_479EF8, qword_47CAB8
    static const int32_t kOut[3] = {0, 0x3000, 0}, kIn[3] = {0, -0x3000, 0};        // qword_478B50, qword_479B00
    int32_t to[3];
    door_->offsetInWorld(action == kEnter ? kFront : kBehind, to);
    door_->offsetInWorld(action == kEnter ? kIn : kOut, through_);
    int32_t area = 0x1000;
    if (action == kEnter) area = 0x2000;
    else if (placeInside) {   // put him inside, facing out (Ped::SetPosition(.., true): the point as it is, at the
        door_->offsetInWorld(kIn, p.pos);   // door's height)
        p.vel[0] = p.vel[1] = p.vel[2] = 0;
        turnTo(g, through_);
    }
    // Ped::SetGoTo + SetWalking (the leaving player also stops colliding), then DoTurn now if he is already in the
    // circle (Area::SetToCircularArea, Location::Contains), else once he is within 1 unit
    g.scriptWalk.on = true;
    g.scriptWalk.target[0] = to[0];
    g.scriptWalk.target[1] = to[1];
    g.scriptWalk.target[2] = to[2];
    g.scriptWalk.walking = true;
    if (action == kLeave) p.collisionProof = true;
    const int32_t dx = p.pos[0] - to[0], dy = p.pos[1] - to[1];
    if ((int64_t)dx * dx + (int64_t)dy * dy <= (int64_t)mul12(area, 0x1000) * area) {
        stop();
        doTurn(g);
    } else {
        whenNear(to, 0x1000, &DoorHandler::doTurn);
    }
}

void DoorHandler::stop() {
    waitState_ = nullptr;
    nearState_ = nullptr;
}

void DoorHandler::whenNear(const int32_t at[3], int32_t radius, State s) {
    near_[0] = at[0];
    near_[1] = at[1];
    nearR_ = radius;
    nearState_ = s;
}

void DoorHandler::tick(Game& g) {
    // cVicinityCallback::Update: inside once r^2 (r x r >> 12, << 12) is at least the 2D distance squared
    if (nearState_) {
        const int64_t dx = g.player.pos[0] - near_[0], dy = g.player.pos[1] - near_[1];
        if (((int64_t)(int32_t)(((int64_t)nearR_ * nearR_) >> 12) << 12) >= dx * dx + dy * dy) {
            State s = nearState_;
            nearState_ = nullptr;
            (this->*s)(g);
        }
    }
    if (waitState_ && --waitFrames_ <= 0) {
        State s = waitState_;
        waitState_ = nullptr;
        (this->*s)(g);
    }
}

void DoorHandler::turnTo(Game& g, const int32_t at[3]) {   // heading -ATan2(x - tx, ty - y)
    const float dx = (float)(at[0] - g.player.pos[0]), dy = (float)(at[1] - g.player.pos[1]);
    g.player.setHeading((int16_t)(atan2f(dx, dy) * 10430.378f));
}

void DoorHandler::doTurn(Game& g) {   // DoTurn: Ped::SetIdle, a frame
    stop();
    state_ = &DoorHandler::doTurn;
    g.scriptWalk.on = false;
    waitThen(1, &DoorHandler::finishTurn);
}

void DoorHandler::finishTurn(Game& g) {   // FinishTurn: face the far side, a frame
    stop();
    turnTo(g, through_);
    waitThen(1, &DoorHandler::openDoor);
}

void DoorHandler::openDoor(Game& g) {   // OpenDoor: the animation (a male ped), the door at speed 2, 5 frames
    stop();
    g.player.startOpenDoorAnim();
    if (door_) {
        door_->open(false);
        TheSound().playResident(0x27F, 0x3C);   // SoundImpl::PlaySimpleSFX(0x27F, 0x3C, ..)
        door_->setSpeed(2);
    }
    waitThen(5, &DoorHandler::finishWalking);
}

void DoorHandler::finishWalking(Game& g) {
    // FinishWalking: through the wall to the far side (Ped::SetGoTo flags 0xC00000), closing the door once within
    // 1 unit of it or after 90 frames, whichever comes first (Ped::SetStopAnim leaves the door's reaction alone)
    g.player.collisionProof = true;
    g.scriptWalk.on = true;
    for (int k = 0; k < 3; ++k) g.scriptWalk.target[k] = through_[k];
    g.scriptWalk.walking = true;
    whenNear(through_, 0x1000, &DoorHandler::closeDoor);
    waitThen(0x5A, &DoorHandler::closeDoor);
}

void DoorHandler::closeDoor(Game&) {   // CloseDoor: speed 2, 15 frames
    stop();
    if (door_) {
        door_->close(false);
        door_->setSpeed(2);
    }
    waitThen(0xF, &DoorHandler::finishClose);
}

void DoorHandler::finishClose(Game& g) {   // FinishClose: sfx 0x189, then Finished
    TheSound().playResident(0x189, 0x3C);
    stop();
    finished(g);
}

void DoorHandler::finished(Game& g) {
    // Finished: the door is released (and goes), the player collidable again and released, then the callback
    stop();
    state_ = nullptr;
    for (size_t i = 0; i < g.doors.size(); ++i)
        if (g.doors[i].get() == door_) { g.doors.erase(g.doors.begin() + (long)i); break; }
    door_ = nullptr;
    g.player.collisionProof = false;
    if (auto fn = std::move(done_)) { done_ = nullptr; fn(); }
}
