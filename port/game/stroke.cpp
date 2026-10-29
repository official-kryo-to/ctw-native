// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kryo.to
// See LICENSE in the repository root.
#include "stroke.h"
#include <cmath>

float StrokeOneWay::modifyChange(float d) {   // cStrokeVariable_OneWay::ModifyChange
    rightWay = true;
    bool keep = true;
    switch (mode) {
    case 0: keep = d > 0; break;
    case 1: keep = !(d < 0); break;
    case 2: keep = d < 0; break;
    case 3: keep = !(d > 0); break;
    default: return d;
    }
    if (keep) return d;
    rightWay = false;
    return 0;
}

void StrokeRegion::broadcastTouchState(int s) {
    for (StrokeVariable* v : vars_) if (v) v->considerTouchState(s);
}

void StrokeRegion::broadcastDelta(float d) {
    for (StrokeVariable* v : vars_)
        if (v) v->addChange(v->modifyChange(d));
}

// cStrokeRegion::Update (without the gamepad branch): idle, tracking (1) or touched outside (2)
void StrokeRegion::update(bool anyTouch, bool released, float x, float y) {
    const StrokeVec p{x, y};
    auto enter = [&] {   // loc_82E284: tracking from this point
        last_ = p;
        if (touchState_ != 1) broadcastTouchState(1);
        touchState_ = state_ = 1;
    };
    auto outside = [&] {   // loc_82E340
        if (touchState_ != 2) broadcastTouchState(2);
        touchState_ = state_ = 2;
    };
    auto letGo = [&] {     // loc_82E13C
        if (touchState_ != 0) broadcastTouchState(0);
        touchState_ = 0;
        state_ = 0;
    };
    switch (state_) {
    case 0:
        if (!anyTouch) {   // no touch on the screen
            if (touchState_ != 0) broadcastTouchState(0);
            touchState_ = 0;
            return;
        }
        if (released) return;
        if (isValidInput(p)) enter();
        else outside();
        return;
    case 1:
        if (released) { letGo(); return; }
        if (!isValidInput(p)) { outside(); return; }
        broadcastDelta(computeDelta(p, last_));
        last_ = p;
        return;
    case 2:
        if (released) { letGo(); return; }
        if (isValidInput(p)) enter();
        return;
    }
}

// cFatLineStrokeRegion::Initialise: the outline about a -> b, then cLinearStrokePath::SetDirection(a, b)
void StrokeFatLine::initialise(const StrokeVec& a, const StrokeVec& b, float w, float back, float front) {
    float dx = b.x - a.x, dy = b.y - a.y;
    const float l = 1.0f / std::sqrt(dy * dy + dx * dx);
    dx *= l;
    dy *= l;
    const float nx = dy * w, ny = -dx * w;
    outline_ = {
        {a.x + nx - dx * back, a.y + ny - dy * back},
        {b.x + nx + dx * front, b.y + ny + dy * front},
        {b.x - nx + dx * front, b.y - ny + dy * front},
        {a.x - nx - dx * back, a.y - ny - dy * back},
    };
    float ux = b.x - a.x, uy = b.y - a.y;
    const float k = 1.0f / std::sqrt(ux * ux + uy * uy);
    dir_ = {ux * k, uy * k};
    origin_ = a;
    length_ = std::sqrt((b.y - a.y) * (b.y - a.y) + (b.x - a.x) * (b.x - a.x));
}

StrokeVec StrokeFatLine::position(float t) const {
    float s = 0;
    if (!(t < 0)) s = length_ < t ? length_ : t;
    return {dir_.x * s + origin_.x, dir_.y * s + origin_.y};
}

bool StrokeFatLine::isValidInput(const StrokeVec& q) const {   // cConvexRegion::IsInside
    const size_t n = outline_.size();
    for (size_t i = 0; i < n; ++i) {
        const StrokeVec& p0 = outline_[i];
        const StrokeVec& p1 = outline_[(i + 1) % n];   // (the outline is closed with its first point)
        const float c = (p1.x - p0.x) * (q.y - p0.y) - (q.x - p0.x) * (p1.y - p0.y);
        if (!(c >= 0)) return false;   // (B.PL: a NaN stops too)
    }
    return true;
}

bool StrokeWinding::isValidInput(const StrokeVec& p) const {   // cWindingStrokeRegion::IsValidInput
    const float dy = p.y - centre_.y, dx = p.x - centre_.x;
    const float d = std::sqrt(dy * dy + dx * dx);
    return !(d < inner_) && !(d > outer_);
}

float StrokeWinding::computeDelta(const StrokeVec& cur, const StrokeVec& last) const {   // cCircularStrokePath
    float cy = cur.y - centre_.y, cx = cur.x - centre_.x;
    float ly = last.y - centre_.y, lx = last.x - centre_.x;
    const float cl = 1.0f / std::sqrt(cy * cy + cx * cx), ll = 1.0f / std::sqrt(ly * ly + lx * lx);
    cx *= cl; cy *= cl; lx *= ll; ly *= ll;
    float s = lx * cy + cx * -ly;
    if (s < -1.0f) s = -1.0f;
    else if (!(s <= 1.0f)) s = 1.0f;   // (B.LE: a NaN becomes 1)
    return std::asin(s);
}
