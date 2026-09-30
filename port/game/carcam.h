// The driving camera (cFollowCarCam2), 20.12 fixed point.
//   - Height 28 units above the car (phones; 35 otherwise) plus 7.5 x (per-frame speed)², lowered when the car
//     is fast and high up; it hangs back along the car's facing by 10 units minus the same speed terms.
//   - Springs towards that point (0.1 of the gap per frame, keeping 500/4096 of its speed), slides along
//     buildings (a 1.3-unit sphere), turns to look at the car (faster with speed) and pitches to -0x2B1C.
//   - Reversing faster than 8 units/s for a second swings it round to the front (state 1).
#pragma once
#include <cstdint>

class Collision;
class Vehicle;
struct WorldCamera;

class FollowCarCam {
public:
    void setBehind(const Vehicle& car);   // cFollowCarCam2::SetCameraBehindTarget
    void update(const Vehicle& car, Collision* col);
    void toWorldCamera(WorldCamera& out) const;
    void position(int32_t out[3]) const { out[0] = pos_[0]; out[1] = pos_[1]; out[2] = pos_[2]; }
    uint16_t yaw() const { return yaw_; }

private:
    int32_t pos_[3] = {0, 0, 0}, vel_[3] = {0, 0, 0};
    uint16_t pitch_ = 0xD4E4, yaw_ = 0;
    int state_ = 0;              // +0x134
    int8_t reverseFrames_ = 0;   // +0x13E
};
