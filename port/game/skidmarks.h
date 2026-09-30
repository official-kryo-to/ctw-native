// Skid marks: a port of cSkidmarkManager / cSkidmark and cWheeledVehicle::AddSkidmarks.
//
// 16 marks. A mark belongs to one wheel (the game uses the tyre's address; here car id x 4 + wheel) and stores up to
// 125 pairs of points (a strip 0.2 wide across the wheel's path, relative to its first point). A point is added
// every frame (every other frame for cars off screen) while the wheel spins / slides; the mark stays "live" for
// `hold` frames after the last point, then waits 60 frames and fades out (alpha 12/31 -> 0) before it is freed.
#pragma once
#include <cstdint>

class Skidmarks {
public:
    void addPoint(uint32_t id, const int32_t p[3], const int32_t dir[3], int hold);   // AddSkidPoint
    void process();
    void render() const;
private:
    struct Mark {
        uint32_t id = 0;
        uint8_t wait = 0x78, count = 0, active = 0, hold = 0, alpha = 0xC, fade = 1;
        int32_t origin[3] = {0, 0, 0};   // x16
        int32_t pts[250][3];             // x16, relative to origin: left, right, left, right ...
    };
    Mark marks_[16];
    void addToMark(Mark& m, const int32_t p[3], const int32_t dir[3], int hold);
};

Skidmarks& TheSkidmarks();
