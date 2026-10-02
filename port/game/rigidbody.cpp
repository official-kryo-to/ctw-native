// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kryo.to
// See LICENSE in the repository root.
#include "rigidbody.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

namespace {
int32_t mulq(int64_t a, int64_t b) { return (int32_t)((a*b) >> 12); }
int64_t isqrt(int64_t v) { return v > 0 ? (int64_t)std::sqrt((double)v) : 0; }
void normalise(int32_t v[3]) {
    const double l2 = (double)v[0]*v[0] + (double)v[1]*v[1] + (double)v[2]*v[2];
    if (l2 <= 0) return;
    const float inv = 1.f / std::sqrt((float)l2);
    for (int k = 0; k < 3; ++k) {
        float f = inv*v[k]; v[k] = (int32_t)((f >= 0 ? .5f : -.5f) + f*4096.f);
    }
}
void multiplyQuat(const int32_t a[4], const int32_t b[4], int32_t out[4]) {
    out[0] = (int32_t)(((int64_t)a[3]*b[0] + (int64_t)a[0]*b[3] + (int64_t)a[1]*b[2] - (int64_t)a[2]*b[1]) >> 12);
    out[1] = (int32_t)(((int64_t)a[3]*b[1] + (int64_t)a[1]*b[3] + (int64_t)a[2]*b[0] - (int64_t)a[0]*b[2]) >> 12);
    out[2] = (int32_t)(((int64_t)a[3]*b[2] + (int64_t)a[2]*b[3] + (int64_t)a[0]*b[1] - (int64_t)a[1]*b[0]) >> 12);
    out[3] = (int32_t)(((int64_t)a[3]*b[3] - (int64_t)a[0]*b[0] - (int64_t)a[1]*b[1] - (int64_t)a[2]*b[2]) >> 12);
}
void normaliseQuat(int32_t q[4]) {
    int64_t l2 = 0; for (int i = 0; i < 4; ++i) l2 += (int64_t)q[i]*q[i];
    if (!l2) { q[3] = 4096; return; }
    const int64_t scale = (0x100000000000000LL / l2) * isqrt(l2*4);
    for (int i = 0; i < 4; ++i) q[i] = (int32_t)((scale*q[i] + 0x100000000000LL) >> 45);
}
}

void RigidBody::init(const Collision::Prop& p, const PropLibrary::Physics& primitive, int32_t mass, bool planar) {
    *this = RigidBody{};
    planar_ = planar;
    mass_ = mass > 0 ? mass : 0;
    invMass_ = mass_ ? (int32_t)((0x100000000000LL / mass_) >> 20) : 0;
    hx = primitive.half[0]; hy = primitive.half[1]; hz = primitive.half[2];
    std::copy(primitive.cg, primitive.cg + 3, cgLocal_);
    std::copy(primitive.centre, primitive.centre + 3, collOffset_);
    // LockPhysicalProperites: the game's box inertia uses mass/6 and clamps inverse inertia at 0x1FD7.
    const int32_t half[3] = {hx,hy,hz};
    const int64_t m6 = mass_ / 6;
    for (int i = 0; i < 3; ++i) {
        const int64_t a = half[(i+1)%3]*2LL, b = half[(i+2)%3]*2LL;
        const int32_t sum = (int32_t)((a*a >> 12) + (b*b >> 12));
        const int32_t inertia = mulq(sum,m6);
        int32_t inverse = inertia > 0 ? (int32_t)((0x100000000000LL / inertia) >> 20) : 0;
        invInertia_[i] = inverse > 0x2000 ? 0x1FD7 : inverse;
    }
    // CalcSpheres: the longest dimension chooses the axis; the shortest chooses the radius, minimum 0.5.
    int axis = hx > hy ? 0 : 1;
    if (hz >= half[axis]) axis = 2;
    sphereR_ = std::max(0x800, std::min({hx,hy,hz}));
    const int32_t den = (int32_t)(((int64_t)sphereR_*0x1A6700000LL) >> 32);
    const int32_t count = den ? (int32_t)(((int64_t)sphereR_*-0x599 + (int64_t)half[axis]*0x2000) / den) : 0;
    sphereCount_ = std::clamp((count + 0x1000) >> 12, 2, 6);
    std::copy(collOffset_, collOffset_ + 3, sphereFirst_);
    sphereFirst_[axis] += half[axis] - sphereR_;
    if (axis != 2) sphereFirst_[2] = sphereR_;
    sphereStep_[axis] = 2*(sphereR_ - half[axis]) / (sphereCount_ - 1);
    if (axis == 0) { // CalcSpheres uses a Q12 division on x, unlike the integer divisions on y/z.
        sphereStep_[0] = (int32_t)((((int64_t)2*(sphereR_ - hx)*0x100000000LL) /
                                    ((sphereCount_ - 1)*4096)) >> 20);
    }
    pos[0] = p.x; pos[1] = p.y; pos[2] = p.z;
    const double angle = p.heading*3.141592653589793/65536.0;
    q_[2] = (int32_t)std::lround(-std::sin(angle)*4096);
    q_[3] = (int32_t)std::lround(std::cos(angle)*4096);
    normaliseQuat(q_);
    syncFromIntegrator();
}

void RigidBody::syncFromIntegrator() {
    const int64_t x=q_[0], y=q_[1], z=q_[2], w=q_[3];
    const int32_t R[3][3] = {
        {(int32_t)((0x800000-y*y-z*z)>>11),(int32_t)((x*y-z*w)>>11),(int32_t)((x*z+y*w)>>11)},
        {(int32_t)((x*y+z*w)>>11),(int32_t)((0x800000-x*x-z*z)>>11),(int32_t)((y*z-x*w)>>11)},
        {(int32_t)((x*z-y*w)>>11),(int32_t)((y*z+x*w)>>11),(int32_t)((0x800000-x*x-y*y)>>11)}};
    for (int k=0;k<3;++k) { right[k]=(int16_t)R[0][k]; fwd[k]=(int16_t)R[1][k]; up[k]=(int16_t)R[2][k]; }
    for (int i=0;i<3;++i) for (int j=0;j<3;++j) {
        int64_t sum=0;
        for (int k=0;k<3;++k) sum += (int64_t)mulq(R[k][i],invInertia_[k])*R[k][j];
        invIWorld_[i][j] = (int32_t)(sum >> 12);
    }
    for (int k=0;k<3;++k) {
        const int32_t offset = (int32_t)(((int64_t)cgLocal_[0]*right[k] + (int64_t)cgLocal_[1]*fwd[k] + (int64_t)cgLocal_[2]*up[k]) >> 12);
        if (physics_) pos[k] = cgWorld_[k] - offset;
        else cgWorld_[k] = pos[k] + offset;
    }
}

void RigidBody::setToPhysics(bool active) {
    if (active && !mass_) return;
    if (active && !physics_) worldCG(cgWorld_);
    if (!active) { std::fill(vel,vel+3,0); std::fill(angVel,angVel+3,0); std::fill(force_,force_+3,0); std::fill(torque_,torque_+3,0); }
    physics_ = active;
}
void RigidBody::worldCG(int32_t out[3]) const { std::copy(cgWorld_,cgWorld_+3,out); }
void RigidBody::setLocalCG(const int32_t centre[3]) {
    std::copy(centre,centre+3,cgLocal_);
    const bool active=physics_; physics_=false; syncFromIntegrator(); physics_=active;
}
void RigidBody::velocityAt(const int32_t p[3], int32_t out[3]) const {
    int32_t r[3]; for(int k=0;k<3;++k) r[k]=p[k]-cgWorld_[k];
    for(int k=0;k<3;++k) out[k] = vel[k] + (int32_t)(((int64_t)r[(k+1)%3]*angVel[(k+2)%3] - (int64_t)r[(k+2)%3]*angVel[(k+1)%3]) >> 12);
}
int32_t RigidBody::speed() const { return (int32_t)isqrt((int64_t)vel[0]*vel[0]+(int64_t)vel[1]*vel[1]+(int64_t)vel[2]*vel[2]); }
void RigidBody::applyWorldForce(const int32_t p[3], const int32_t f[3], int) {
    if (!mass_) return;
    int32_t r[3]; for(int k=0;k<3;++k) r[k]=p[k]-cgWorld_[k];
    for(int k=0;k<3;++k) {
        torque_[k] += (int32_t)(((int64_t)f[(k+1)%3]*r[(k+2)%3] - (int64_t)f[(k+2)%3]*r[(k+1)%3]) >> 12);
        force_[k] += f[k];
    }
}
void RigidBody::recalcKinematics() {
    if (!physics_ || !mass_) return;
    for(int k=0;k<3;++k) {
        const int32_t angular = (int32_t)(((int64_t)invIWorld_[k][0]*torque_[0]+(int64_t)invIWorld_[k][1]*torque_[1]+(int64_t)invIWorld_[k][2]*torque_[2]) >> 12);
        angVel[k] += (int32_t)(((int64_t)angular*17) >> 9);
        const int32_t accel = mulq(invMass_,force_[k]);
        vel[k] += (int32_t)(((int64_t)accel*17) >> 9);
    }
    std::fill(force_,force_+3,0); std::fill(torque_,torque_+3,0);
    // cDynamicProp::RecalcKinematics's 0x59524 velocity cap.
    const int64_t l2 = (int64_t)vel[0]*vel[0]+(int64_t)vel[1]*vel[1]+(int64_t)vel[2]*vel[2];
    if (l2 >= 0x1F2A4AE001LL) { normalise(vel); for(int k=0;k<3;++k) vel[k]=mulq(vel[k],0x59524); }
}
void RigidBody::integrateStep(int32_t fraction) {
    const int32_t dt = (fraction*17) >> 9;
    int32_t omega[4] = {angVel[0] >> 1,angVel[1] >> 1,angVel[2] >> 1,0}, delta[4];
    multiplyQuat(q_,omega,delta);
    for(int k=0;k<4;++k) q_[k] += mulq(delta[k],dt);
    for(int k=0;k<3;++k) cgWorld_[k] += mulq(vel[k],dt);
    if(planar_) q_[0]=q_[1]=0;
    normaliseQuat(q_); syncFromIntegrator();
}
void RigidBody::damp() {
    for(int k=0;k<3;++k) { vel[k]=mulq(vel[k],0xF3C); angVel[k]=mulq(angVel[k],0xF3C); }
    if(planar_) { angVel[0]=angVel[1]=0; q_[0]=q_[1]=0; }
}
void RigidBody::rotate(const int32_t axis[3], int16_t angle) {
    const double half = angle*3.141592653589793/65536.0;
    const int32_t s=(int32_t)std::lround(std::sin(half)*4096);
    int32_t rotation[4]={mulq(-axis[0],s),mulq(-axis[1],s),mulq(-axis[2],s),(int32_t)std::lround(std::cos(half)*4096)}, q[4];
    multiplyQuat(q_,rotation,q); std::copy(q,q+4,q_); normaliseQuat(q_);
    // Smash1 rotates the entity about its placement; SetToPhysics then rebuilds the centre of gravity.
    const bool active = physics_; physics_ = false; syncFromIntegrator(); physics_ = active;
}
void RigidBody::matrix(float M[16]) const {
    for(int k=0;k<3;++k) { M[k]=right[k]/4096.f; M[4+k]=fwd[k]/4096.f; M[8+k]=up[k]/4096.f; M[12+k]=pos[k]/4096.f; }
    M[3]=M[7]=M[11]=0; M[15]=1;
}
void RigidBody::worldPosition(const int32_t local[3], int32_t out[3]) const {
    for (int k = 0; k < 3; ++k) out[k] = pos[k] + (int32_t)(
        ((int64_t)local[0]*right[k] + (int64_t)local[1]*fwd[k] + (int64_t)local[2]*up[k]) >> 12);
}
void RigidBody::spheres(int32_t out[6][4], int& count) const {
    count=sphereCount_;
    int32_t first[3], step[3];
    const int16_t* axes[3]={right,fwd,up};
    for(int k=0;k<3;++k) {
        int64_t start=0,delta=0;
        for(int j=0;j<3;++j) { start+=(int64_t)sphereFirst_[j]*axes[j][k]; delta+=(int64_t)sphereStep_[j]*axes[j][k]; }
        first[k]=pos[k]+(int32_t)(start >> 12); step[k]=(int32_t)(delta >> 12);
    }
    for(int i=0;i<count;++i) {
        for(int k=0;k<3;++k) out[i][k]=first[k]+i*step[k];
        out[i][3]=sphereR_;
    }
}
void RigidBody::bboxVerts(int32_t out[8][3]) const {
    static const int signs[8][3]={{1,1,1},{-1,1,1},{-1,-1,1},{1,-1,1},{-1,-1,-1},{1,-1,-1},{1,1,-1},{-1,1,-1}};
    for(int i=0;i<8;++i) for(int k=0;k<3;++k) out[i][k]=pos[k]+(int32_t)(
        ((int64_t)(collOffset_[0]+signs[i][0]*hx)*right[k]+(int64_t)(collOffset_[1]+signs[i][1]*hy)*fwd[k]+(int64_t)(collOffset_[2]+signs[i][2]*hz)*up[k]) >> 12);
}
int32_t RigidBody::impactTerm(const int32_t n[3], const int32_t r[3]) const {
    if(!mass_) return 0;
    int32_t rn[3],a[3],c[3];
    for(int k=0;k<3;++k) rn[k]=(int32_t)(((int64_t)r[(k+1)%3]*n[(k+2)%3]-(int64_t)r[(k+2)%3]*n[(k+1)%3]) >> 12);
    for(int k=0;k<3;++k) a[k]=(int32_t)(((int64_t)invIWorld_[k][0]*rn[0]+(int64_t)invIWorld_[k][1]*rn[1]+(int64_t)invIWorld_[k][2]*rn[2]) >> 12);
    for(int k=0;k<3;++k) c[k]=(int32_t)(((int64_t)a[(k+1)%3]*r[(k+2)%3]-(int64_t)a[(k+2)%3]*r[(k+1)%3]) >> 12);
    return (int32_t)(((int64_t)c[0]*n[0]+(int64_t)c[1]*n[1]+(int64_t)c[2]*n[2]) >> 12);
}

void RigidBody::calcImpactEnv(const Rec& rc,bool friction) {
    int32_t v[3],r[3]; velocityAt(rc.p,v);
    int32_t vn=(int32_t)(((int64_t)v[0]*rc.n[0]+(int64_t)v[1]*rc.n[1]+(int64_t)v[2]*rc.n[2]) >> 12);
    if(vn >= 0) vn=-4;
    for(int k=0;k<3;++k) r[k]=rc.p[k]-cgWorld_[k];
    const int32_t den=invMass_+impactTerm(rc.n,r);
    if(den <= 0) return;
    const uint32_t u=(uint32_t)(-vn-4096), u3=std::min(u,0xE000u);
    const int64_t e=(int32_t)u >= 0 ? (((uint64_t)(u3*0xE3E)*0x12492493ULL) >> 32)&0x1FFF000 : 0;
    const int64_t j=((e*0x100000-0x1FD700000000LL)/den) >> 20;
    const int32_t impulse=mulq(j,vn)*30;
    int32_t f[3];
    for(int k=0;k<3;++k) {
        const int32_t vt=v[k]-mulq(vn,rc.n[k]);
        f[k]=mulq(impulse,rc.n[k])+(friction ? mulq(mulq(-mass_,0xCC),vt) : 0);
    }
    collided_=true; applyWorldForce(rc.p,f,4); recalcKinematics();
}

void RigidBody::springImpact(const Rec& rc) {
    int32_t v[3]; velocityAt(rc.p,v);
    const int32_t depth=std::min(rc.depth,0x800);
    const int32_t vn=(int32_t)(((int64_t)v[0]*rc.n[0]+(int64_t)v[1]*rc.n[1]+(int64_t)v[2]*rc.n[2]) >> 12);
    const int32_t force=mulq(std::max(vn,0),-0x64000)+depth*0x80;
    if(force <= 0) return;
    int32_t vt[3]; for(int k=0;k<3;++k) vt[k]=v[k]-mulq(vn,rc.n[k]);
    const int32_t speed=(int32_t)isqrt((int64_t)vt[0]*vt[0]+(int64_t)vt[1]*vt[1]+(int64_t)vt[2]*vt[2]);
    normalise(vt);
    const int32_t friction=std::min(speed,mulq(depth,0x1C000));
    int32_t f[3]; for(int k=0;k<3;++k) f[k]=mulq(mulq(force,rc.n[k])-mulq(vt[k],friction),mass_);
    collided_=true; applyWorldForce(rc.p,f,4); recalcKinematics();
}

void RigidBody::process(Collision& col) {
    if(!physics_) return;
    recalcKinematics();
    collided_=frozen_=false;
    if(col.ok()) { fullCollision(&col,true); fullSprings(&col); }
    else integrateStep(0x1000);
}

void RigidBody::fullCollision(Collision* col, bool) {   // cPhysicalIntegrator::FullCollision
    Collision::Candidates cand;
    int32_t R = speed() / 0x19 + (int32_t)(((int64_t)hy * 3) >> 1);
    col->candidates(pos, R, false, cand);
    int32_t frac = 0x1000;
    int iters = 8;
    std::vector<Rec> recs;
    while (true) {
        int32_t sv[3] = {cgWorld_[0], cgWorld_[1], cgWorld_[2]}, sVel[3] = {vel[0], vel[1], vel[2]}, sAng[3] = {angVel[0], angVel[1], angVel[2]};
        int32_t sq[4] = {q_[0], q_[1], q_[2], q_[3]};
        int32_t before[6][4], after[6][4], vb[8][3], va[8][3];
        int n;
        spheres(before, n);
        bboxVerts(vb);
        integrateStep(frac);
        spheres(after, n);
        bboxVerts(va);
        int32_t best = 0x64000;
        recs.clear();
        auto add = [&](const Rec& rc) {
            if (rc.t > best) return;
            if (rc.t < best) recs.clear();
            recs.push_back(rc);
            best = rc.t;
        };
        for (const Collision::Cyl* c : cand.cyls) {

            for (int i = 0; i < n; ++i) {
                Rec rc{};
                if (Collision::sweptSphereVCylinder(before[i], after[i], after[i][3], *c, rc.p, rc.n, rc.t)) add(rc);
            }
        }
        for (const Collision::Box* b : cand.boxes) {
            {
                for (int i = 0; i < n; ++i) {
                    Rec rc{};
                    if (!Collision::sweptSphereVBox(before[i], after[i], after[i][3], *b, rc.p, rc.t) || rc.t < 0) continue;
                    for (int k = 0; k < 3; ++k) rc.n[k] = (before[i][k] - rc.p[k]) + mulq(after[i][k] - before[i][k], rc.t);
                    if (!rc.n[0] && !rc.n[1] && !rc.n[2]) continue;
                    normalise(rc.n);
                    add(rc);
                }
            }
            for (int v = 0; v < 8; ++v) {   // the box corners (0x1E3)
                Rec rc{};
                if (!Collision::sweptVertVBox(vb[v], va[v], *b, rc.p, rc.n, rc.t) || rc.t < 0 || rc.n[2] <= -0xF34) continue;
                if ((int64_t)rc.n[2] * (rc.p[2] - pos[2]) + (int64_t)rc.n[0] * (rc.p[0] - pos[0]) + (int64_t)rc.n[1] * (rc.p[1] - pos[1]) >= 1) continue;
                add(rc);
            }
        }
        for (const Collision::TriRef& tr : cand.tris) {
            for (int i = 0; i < n; ++i) {
                Rec rc{};
                if (Collision::sweptSphereVTri(before[i], after[i], after[i][3], tr, rc.p, rc.n, rc.t) && rc.t >= 0) add(rc);
            }
        }
        {   // the ground plane under the body (GetGroundSimple): corners going through it
            int32_t radius = (int32_t)isqrt((int64_t)hx * hx + (int64_t)hy * hy + (int64_t)hz * hz);
            Collision::Ground g = col->ground(pos[0] / 4096.f, pos[1] / 4096.f, (pos[2] + radius) / 4096.f);
            int32_t gz = (int32_t)lroundf(g.z * 4096.f);
            {
                for (int v = 0; v < 8; ++v) {
                    int32_t zb = vb[v][2], za = va[v][2];
                    if (za >= zb) continue;
                    int32_t h = zb - za, d = zb - gz;
                    if (h - d == 0 || d > h) continue;
                    Rec rc{};
                    rc.t = d <= 0 ? 0 : (int32_t)(((int64_t)d * 4096) / h);
                    for (int k = 0; k < 3; ++k) rc.p[k] = vb[v][k] + mulq(rc.t, va[v][k] - vb[v][k]);
                    rc.n[0] = 0; rc.n[1] = 0; rc.n[2] = 0x1000;
                    rc.depth = h - d;
                    rc.surface = g.surface;
                    add(rc);
                }
            }
        }
        if (recs.empty()) break;

        for (int k = 0; k < 3; ++k) { cgWorld_[k] = sv[k]; vel[k] = sVel[k]; angVel[k] = sAng[k]; }
        memcpy(q_, sq, sizeof q_);
        syncFromIntegrator();
        int64_t tt = (iters <= 3 && best <= 3) ? 4 : best;
        int32_t step = (int32_t)((tt * frac) >> 12);
        integrateStep(step);
        for (const Rec& rc : recs) calcImpactEnv(rc, iters > 3 || best > 3);
        --iters;
        frac -= step;
        if (iters == 0) {
            frozen_ = true;
            return;
        }
    }
}

void RigidBody::fullSprings(Collision* col) {   // cPhysicalIntegrator::FullSpringCollision
    Collision::Candidates cand;
    int32_t R = speed() / 0x19 + (int32_t)(((int64_t)hy * 3) >> 1);
    col->candidates(pos, R, false, cand);
    int32_t sp[6][4], vb[8][3];
    int n;
    spheres(sp, n);
    bboxVerts(vb);
    for (const Collision::Box* b : cand.boxes)
        for (int i = 0; i < n; ++i) {
            Rec rc{};
            if (Collision::sphereVBox(sp[i], sp[i][3], *b, rc.p, rc.n, rc.depth)) springImpact(rc);
        }
    for (const Collision::TriRef& tr : cand.tris) {
        for (int i = 0; i < n; ++i) {
            Rec rc{};
            if (Collision::sphereVTri(sp[i], sp[i][3], tr, rc.p, rc.n, rc.depth)) {

                springImpact(rc);
            }
        }
        if (std::abs(tr.tri->n[2]) > 0x199)
            for (int v = 0; v < 8; ++v) {   // corners against walkable slopes
                int32_t a[3] = {vb[v][0] + (tr.tri->n[0] >> 1), vb[v][1] + (tr.tri->n[1] >> 1), vb[v][2] - 0x4CC + (tr.tri->n[2] >> 1)};
                int32_t b[3] = {vb[v][0], vb[v][1], vb[v][2] - 0x4CC};
                Rec rc{};
                if (!Collision::sweptVertVTri(a, b, tr, rc.p, rc.n, rc.t)) continue;
                const int32_t* V0 = tr.verts + tr.tri->v[0] * 3;
                rc.depth = (int32_t)((((int64_t)V0[0] * rc.n[0] + (int64_t)V0[1] * rc.n[1] + (int64_t)V0[2] * rc.n[2]) -
                                      ((int64_t)rc.n[0] * b[0] + (int64_t)rc.n[1] * b[1] + (int64_t)rc.n[2] * b[2])) >> 12);
                if (rc.depth > 0) {

                    springImpact(rc);
                }
            }
    }
    for (const Collision::Cyl* c : cand.cyls)
        for (int i = 0; i < n; ++i) {
            int32_t d[3] = {sp[i][0] - c->x, sp[i][1] - c->y, (sp[i][2] - c->z) - c->h};
            if (d[2] == 0 || sp[i][2] - c->z < c->h) d[2] = 0;
            int64_t d2 = ((int64_t)d[0] * d[0] + (int64_t)d[1] * d[1] + (int64_t)d[2] * d[2]) & ~0xFFFLL;
            int64_t rr = (int64_t)sp[i][3] + c->r;
            if (d2 >= rr * rr) continue;
            normalise(d);
            Rec rc{};
            rc.n[0] = d[0]; rc.n[1] = d[1]; rc.n[2] = d[2];
            rc.depth = (sphereR_ - (int32_t)isqrt(d2)) + c->r;
            int32_t k = c->r - rc.depth;
            rc.p[0] = c->x + mulq(d[0], k); rc.p[1] = c->y + mulq(d[1], k); rc.p[2] = c->z + mulq(d[2], k);
            if (c->pad == 0) springImpact(rc);
        }
    {   // the corners float 0.3 above the ground (UseSpringCollision)
        for (int v = 0; v < 8; ++v) {
            Collision::Ground g = col->ground(vb[v][0] / 4096.f, vb[v][1] / 4096.f, (vb[v][2] + 0x1000) / 4096.f);
            int32_t gz = (int32_t)lroundf(g.z * 4096.f);
            if (gz >= 1) continue;   // (the game only springs off ground at or below 0)
            if (-0x4CC < gz - vb[v][2]) {
                Rec rc{};
                rc.depth = (gz - vb[v][2]) + 0x4CC;
                rc.n[2] = 0x1000;
                rc.p[0] = vb[v][0]; rc.p[1] = vb[v][1]; rc.p[2] = vb[v][2];
                springImpact(rc);
            }
        }
    }
}
