#include "followcam.h"
#include "world/collision.h"
#include "world/worldrenderer.h"
#include <cmath>
#include <cstdlib>

static int fastsin(int a) { return (int)(sinf((float)a * 9.587378e-05f) * 4096.f); }
static int32_t mulq(int64_t a, int64_t b) { return (int32_t)((a * b) >> 12); }
static const int32_t kCameraRadius = 0x14CD;   // CameraRadius

void FollowPedCam::reset(const int32_t t[3], int16_t heading) {
    height = mulq(0x23000, (int32_t)(heightScale * 4096.f + 0.5f));   // cFollowPedCam: height * GetCamHeightScale
    for (int i = 0; i < 3; ++i) target_[i] = t[i];
    yaw = (uint16_t)heading;
    int cp = std::abs(fastsin((int16_t)55000 + 0x4000));
    pos_[0] = t[0] + mulq((int64_t)cp * -0x18, fastsin((int16_t)yaw));
    pos_[1] = t[1] + mulq((int64_t)cp * -0x18, fastsin((int16_t)yaw + 0x4000));
    pos_[2] = t[2] + height + 0x2000;
    vel_[0] = vel_[1] = vel_[2] = 0;
    pitch_ = 55000;
    pitchVel_ = 0;
}

// cFollowPedCam::CanSeeTargetFromDefaultPos: the line from 2 units above the target to where the camera would
// sit at the default tilt (0x16D8 from vertical), against boxes.
bool FollowPedCam::canSeeTarget(Collision* col) const {
    if (!col || !col->ok()) return true;
    int s = fastsin((int16_t)yaw), c = fastsin((int16_t)yaw + 0x4000), k = std::abs(fastsin(0x16D8));
    int32_t a[3] = {target_[0], target_[1], target_[2] + 0x2000};
    int32_t b[3] = {target_[0] + mulq((int64_t)k * -0x18, s), target_[1] + mulq((int64_t)k * -0x18, c), target_[2] + height + 0x2000};
    return !col->lineHitsBoxes(a, b, true);
}

// cFollowPedCam::TryToFaceAngle (following the player, not aiming, not in "behind" mode)
void FollowPedCam::tryToFaceAngle(int16_t pedHeading, const int32_t v[3], int div, int maxStep) {
    int64_t speed2 = ((int64_t)v[0] * v[0] + (int64_t)v[1] * v[1] + (int64_t)v[2] * v[2]) >> 12;
    if (speed2 < 0x28) return;   // only while the ped moves
    int32_t diff = (int16_t)(pedHeading - (int16_t)yaw);
    int32_t absd = std::abs(diff);
    if (absd > 0x7C72) { maxStep = 1; div = 1; }   // nearly opposite: don't swing round
    int32_t step = diff;
    if ((int16_t)div < absd) {
        int16_t d = (int16_t)div;
        if ((div & 0xFFFE) == 0) d = 1;
        step = diff / d;
    }
    if (step > maxStep) step = maxStep;
    if (step < -maxStep) step = -maxStep;
    yaw = (uint16_t)(yaw + step);
}

void FollowPedCam::update(const int32_t t[3], int16_t pedHeading, const int32_t pedVel[3], Collision* col) {
    int32_t old[3] = {pos_[0], pos_[1], pos_[2]};

    // pitch target (cFollowPedCam::Update)
    int32_t pitchTarget;
    bool canSee = canSeeTarget(col);
    if (canSee) pitchTarget = 55000;
    else {
        pitchTarget = 0xC000;
        int32_t d[2] = {target_[0] - pos_[0], target_[1] - pos_[1]};
        float cp = cosf((int16_t)pitch_ * 9.587378e-05f);   // the camera's forward, flattened (matrix row 2)
        int32_t f[2] = {(int32_t)(sinf((int16_t)yaw * 9.587378e-05f) * cp * 4096.f), (int32_t)(cosf((int16_t)yaw * 9.587378e-05f) * cp * 4096.f)};
        int64_t dl = (int64_t)d[0] * d[0] + (int64_t)d[1] * d[1], fl = (int64_t)f[0] * f[0] + (int64_t)f[1] * f[1];
        if (dl > 0x28000 && fl > 0x28000 && col && col->ok()) {
            double dn = std::sqrt((double)dl), fn = std::sqrt((double)fl);
            if ((d[0] * (double)f[0] + d[1] * (double)f[1]) / (dn * fn) > 0xE66000 / 16777216.0) {
                int32_t a[3] = {target_[0], target_[1], target_[2] + 0x2000}, b[3] = {target_[0], target_[1], target_[2] + 0x66000};
                pitchTarget = col->lineHitsBoxes(a, b, false) ? -0x2928 : -0x4000;   // something overhead: stay tilted
            }
        }
    }
    int32_t diff = (int32_t)(int16_t)(uint16_t)(pitchTarget - pitch_) * 4096;
    pitchVel_ = mulq(diff, 399) + mulq(pitchVel_, 0x7CE);
    pitch_ = (uint16_t)(pitch_ + (int16_t)(pitchVel_ >> 12));

    for (int i = 0; i < 3; ++i) target_[i] = t[i];

    // ProcessFacingWander: turn (the player's third of the step), then the position spring
    int div = canSee ? 2 : 3, maxStep = canSee ? 100 : 300;
    tryToFaceAngle(pedHeading, pedVel, div, maxStep / 3);
    const int32_t gain = 0x333;
    int s = fastsin((int16_t)yaw), c = fastsin((int16_t)yaw + 0x4000);
    int cp = std::abs(fastsin((int16_t)pitch_ + 0x4000));
    int32_t vz = mulq((int64_t)(target_[2] + height - pos_[2]) + 0x2000, gain << 1) + mulq(vel_[2], 0x38D);
    int32_t vx = mulq(gain, (int64_t)(target_[0] - pos_[0]) + mulq((int64_t)cp * -0x18, s)) + mulq(vel_[0], 0x38D);
    int32_t vy = mulq(gain, (int64_t)(target_[1] - pos_[1]) + mulq((int64_t)cp * -0x18, c)) + mulq(vel_[1], 0x38D);
    vel_[0] = vx; vel_[1] = vy; vel_[2] = vz;
    int32_t want[3] = {pos_[0] + vx, pos_[1] + vy, pos_[2] + vz};

    // keep the camera out of buildings: sweep old -> new, slide along what it hits (up to 4 times)
    int32_t from[3] = {old[0], old[1], old[2]}, contact[3], n[3];
    if (col && col->ok() && col->sweptSphereHitsBoxes(from, want, kCameraRadius, contact, n)) {
        for (int it = 3;; --it) {
            int32_t m[3] = {(int32_t)(n[0] * 0x10280 >> 16), (int32_t)(n[1] * 0x10280 >> 16), (int32_t)(n[2] * 0x10280 >> 16)};
            int32_t d = (int32_t)(((int64_t)(want[1] - contact[1]) * m[1] + (int64_t)(want[0] - contact[0]) * m[0] +
                                   (int64_t)(want[2] - contact[2]) * m[2]) >> 12);
            for (int i = 0; i < 3; ++i) {
                want[i] = want[i] - mulq(d, m[i]) + mulq(kCameraRadius, m[i]);
                from[i] = contact[i] + mulq(kCameraRadius, m[i]);
            }
            if (it == 0) { want[0] = from[0]; want[1] = from[1]; want[2] = from[2]; break; }
            if (!col->sweptSphereHitsBoxes(from, want, kCameraRadius, contact, n)) break;
        }
    }
    pos_[0] = want[0]; pos_[1] = want[1]; pos_[2] = want[2];
}

void FollowPedCam::position(float out[3]) const {
    for (int i = 0; i < 3; ++i) out[i] = pos_[i] / 4096.f;
}

void FollowPedCam::toWorldCamera(WorldCamera& cam) const {
    position(cam.eye);
    // cBaseCam::RecalculateMatrix: RotZ(yaw) then RotX(-0x4000 - pitch); pitch 0xC000 = looking straight down.
    // (WorldCamera's yaw turns counter-clockwise, the game's clockwise.)
    float pitchDeg = (int16_t)pitch_ * 360.f / 65536.f;
    float yawDeg = -(int16_t)yaw * 360.f / 65536.f;
    cam.setYawPitch(yawDeg, pitchDeg);
    cam.fovY = 60.f;
    cam.zNear = 0x666 / 4096.f;   // 0.4
    cam.zFar = 150.f;
}
