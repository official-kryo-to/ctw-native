// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kryo.to
// See LICENSE in the repository root.
// The minigames' touch gestures: cStrokeRegion and its variables.
//
// A region follows the latest touch (cStrokeRegion::Update): idle until a touch lands inside it (IsValidInput), then
// each frame the move from the last point (ComputeDelta) goes to its bound variables, up to three, as
// d' = ModifyChange(d) then AddChange(d'). A touch that leaves the region stops the tracking until it comes back;
// letting go ends it. Each change of the touch state (0 none, 1 inside, 2 outside) is told to the variables first
// (ConsiderTouchState).
//   cConvexStrokeRegion  inside a convex outline (cConvexRegion::IsInside: every edge's cross product >= 0)
//   cFatLineStrokeRegion a rectangle around a -> b (half width, back past a, front past b); the move along a -> b
//                        (cLinearStrokePath)
//   cWindingStrokeRegion a ring about a centre (inner <= distance <= outer); the turn about the centre,
//                        asin(cross(last direction, new direction)) (cCircularStrokePath)
// Variables share one ActualVariable (value clamped to [min, max]):
//   Momentum  the last change is its velocity; untouched (or outside) it keeps moving, x K a frame; IsMoving while
//             touched or faster than 0.1
//   OneWay    keeps only changes the right way (mode 0: > 0, 1: >= 0, 2: < 0, 3: <= 0), remembers if the last was
// Gamepad input (cOSWGamepad) is not ported: the PC drives the minigames with the mouse.
#pragma once
#include <vector>

struct StrokeVec { float x = 0, y = 0; };

struct StrokeValue {   // cStrokeRegion::ActualVariable
    float value = 0, min = 0, max = 0;
    void set(float v) { value = v < min ? min : (v > max ? max : v); }   // (SetValue: min first, as the original)
};

class StrokeVariable {   // cStrokeRegion::Variable
public:
    explicit StrokeVariable(StrokeValue* v = nullptr) : value_(v) {}
    virtual ~StrokeVariable() = default;
    virtual void considerTouchState(int) {}
    virtual float modifyChange(float d) { return d; }
    virtual void addChange(float d) { if (value_) value_->set(value_->value + d); }
    virtual void update() {}
    StrokeValue* value_;
};

class StrokeMomentum : public StrokeVariable {   // cStrokeVariable_Momentum
public:
    using StrokeVariable::StrokeVariable;
    float k = 0, velocity = 0;
    int mode = 1;
    void considerTouchState(int state) override {
        static const int kModes[3] = {1, 0, 1};   // dword_485D5C
        if (state >= 0 && state <= 2) mode = kModes[state];
    }
    void addChange(float d) override { StrokeVariable::addChange(d); velocity = d; }
    void update() override {
        if (mode != 1) return;
        if (value_) value_->set(value_->value + velocity);
        velocity = k * velocity;
    }
    bool isMoving() const { return mode == 0 || velocity > 0.1f; }
};

class StrokeOneWay : public StrokeVariable {   // cStrokeVariable_OneWay
public:
    using StrokeVariable::StrokeVariable;
    int mode = 0;
    bool rightWay = false;
    float modifyChange(float d) override;
};

class StrokeRegion {   // cStrokeRegion
public:
    virtual ~StrokeRegion() = default;
    void bind(StrokeVariable* v) { if (count_ < 3) vars_[count_++] = v; }
    void unbindAll() { vars_[2] = nullptr; count_ = 0; }   // (UnBindAll clears the count and the third slot)
    void reset() { state_ = 0; touchState_ = -1; }
    // touch: down at (x, y) in the app's space, or released (cTouchScreen's last touch; none while never touched)
    void update(bool anyTouch, bool released, float x, float y);
    int state() const { return state_; }

protected:
    virtual bool isValidInput(const StrokeVec&) const { return true; }
    virtual float computeDelta(const StrokeVec& cur, const StrokeVec& last) const = 0;

private:
    void broadcastTouchState(int s);
    void broadcastDelta(float d);
    int state_ = 0, touchState_ = -1, count_ = 0;
    bool tracking_ = false;   // (a touch was taken when the tracking started)
    StrokeVec last_;
    StrokeVariable* vars_[3] = {nullptr, nullptr, nullptr};
};

class StrokeFatLine : public StrokeRegion {   // cFatLineStrokeRegion (cConvexStrokeRegion + cLinearStrokePath)
public:
    void initialise(const StrokeVec& a, const StrokeVec& b, float halfWidth, float back, float front);
    StrokeVec position(float t) const;   // cLinearStrokePath::CalculatePosition: a + dir x clamp(t, 0, length)
    float length() const { return length_; }
    const StrokeVec& start() const { return origin_; }

protected:
    bool isValidInput(const StrokeVec& p) const override;
    float computeDelta(const StrokeVec& cur, const StrokeVec& last) const override {
        return (cur.y - last.y) * dir_.y + (cur.x - last.x) * dir_.x;
    }

private:
    std::vector<StrokeVec> outline_;
    StrokeVec dir_, origin_;
    float length_ = 0;
};

class StrokeWinding : public StrokeRegion {   // cWindingStrokeRegion + cCircularStrokePath
public:
    void initialise(const StrokeVec& centre, float inner, float outer) { centre_ = centre; inner_ = inner; outer_ = outer; }

protected:
    bool isValidInput(const StrokeVec& p) const override;
    float computeDelta(const StrokeVec& cur, const StrokeVec& last) const override;

private:
    StrokeVec centre_;
    float inner_ = 0, outer_ = 0;
};
