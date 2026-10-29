// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kryo.to
// See LICENSE in the repository root.
#include "vehicle.h"
#include "gfx/model.h"
#include "cargens.h"
#include <algorithm>
#include <cmath>

namespace {
int32_t mul(int64_t a, int64_t b) { return (int32_t)((a*b) >> 12); }
void normalise(int32_t q[4]) {
    int64_t n = 0; for (int k=0;k<4;++k) n += (int64_t)q[k]*q[k];
    if (!n) { q[3]=4096; return; }
    const int64_t scale = (0x100000000000000LL/n)*(int64_t)std::sqrt((double)n*4);
    for (int k=0;k<4;++k) q[k]=(int32_t)((scale*q[k]+0x100000000000LL)>>45);
}
void rotation(const int32_t q[4], float r[3][3]) {
    const float x=q[0]/4096.f,y=q[1]/4096.f,z=q[2]/4096.f,w=q[3]/4096.f;
    r[0][0]=1-2*(y*y+z*z); r[0][1]=2*(x*y-z*w); r[0][2]=2*(x*z+y*w);
    r[1][0]=2*(x*y+z*w); r[1][1]=1-2*(x*x+z*z); r[1][2]=2*(y*z-x*w);
    r[2][0]=2*(x*z-y*w); r[2][1]=2*(y*z+x*w); r[2][2]=1-2*(x*x+y*y);
}
void quaternion(const float r[3][3], int32_t out[4]) {
    float q[4]{};
    const float trace=r[0][0]+r[1][1]+r[2][2];
    if (trace>0) {
        const float s=std::sqrt(trace+1.f)*2;
        q[3]=s/4; q[0]=(r[2][1]-r[1][2])/s; q[1]=(r[0][2]-r[2][0])/s; q[2]=(r[1][0]-r[0][1])/s;
    } else {
        int i=0; if(r[1][1]>r[i][i]) i=1; if(r[2][2]>r[i][i]) i=2;
        const int j=(i+1)%3,k=(i+2)%3;
        const float s=std::sqrt(std::max(0.f,1+r[i][i]-r[j][j]-r[k][k]))*2;
        if (s==0) { out[3]=4096; return; }
        q[i]=s/4; q[j]=(r[j][i]+r[i][j])/s; q[k]=(r[k][i]+r[i][k])/s; q[3]=(r[k][j]-r[j][k])/s;
    }
    for(int k=0;k<4;++k) out[k]=(int32_t)std::lround(q[k]*4096);
    normalise(out);
}
}

void Vehicle::damageDoors(unsigned amount) { // cVehicle::DamageDoors
    if (amount<7 || Rand16Critical(16)<7) return;
    const unsigned roll=Rand16Critical(100);
    const int slot=roll<25 ? 0 : roll<50 ? 1 : roll<55 ? 2 : roll<60 ? 3 : 4;
    if (debris_[slot].detached) return;
    if (flapBits_ & (1<<slot)) { pendingDebris_[slot]=0x4000; return; }
    // The original also gates this on the virtual-driver flag. Our traffic owns real pedestrians;
    // ordinary seat occupancy is not equivalent to that flag.
    if (slot!=4 && (flapBits_ & 0xE)) return;
    flapBits_ |= 1<<slot;
}

void Vehicle::flapDoors() { // cVehicle::FlapDoors, before cVehicleDoor::Update
    unsigned active=flapBits_;
    for(int i=0;i<5;++i) if(debris_[i].detached) active &= ~(1u<<i);
    if(!active) return;
    int64_t spin=0,forward=0;
    for(int k=0;k<3;++k) {
        const int32_t acceleration=flapPreviousVelocity_[k]-vel[k]-mul(vel[k],0x199)
            + (k==2 ? mul(-0x1D000,0x333) : 0);
        forward+=(int64_t)acceleration*fwd[k];
        spin+=(int64_t)angVel[k]*up[k];
        flapPreviousVelocity_[k]=vel[k];
    }
    const int32_t drive=(int32_t)((((2*std::abs(spin)) & 0xFFFFFFFE000LL)+forward)>>12);
    for(int i=0;i<4;++i) if((active & (1<<i)) && doorNode_[i]>0) {
        Door& d=doors_[i];
        const int32_t spring=d.angle<40 ? (40-d.angle)*0x4CC : 0;
        d.speed=(int8_t)std::clamp(d.speed+((spring+drive-d.speed*0xCC)>>12),-128,127);
    }
    if((active & 16) && doorNode_[4]>0) {
        Door& d=doors_[4];
        int32_t bootDrive=(int32_t)(forward>>12);
        if(bootReverse_) bootDrive=-bootDrive;
        if(d.angle<40) {
            bootDrive-=(40-d.angle)*0x4CC;
            if(d.angle<10) { bootDrive=std::min(bootDrive,0); d.angle=0; }
            else if(d.angle>=31) bootDrive+=4096;
        } else bootDrive+=4096;
        d.speed=(int8_t)std::clamp(d.speed-((bootDrive+d.speed*0xCC)>>12),-128,127);
    }
}

bool Vehicle::createDebris(int slot, int32_t strength, const Model& model) { // cVehicle::CreateDebris
    if (slot<0 || slot>=5 || debris_[slot].detached) return false;
    const int node=doorNode_[slot];
    if(node<1 || node>=(int)model.world.size()) return false;
    Debris& d=debris_[slot];
    const NodeMatrix& initial=model.world[node];
    const int16_t* axes[3]={right,fwd,up};
    float r[3][3]{};
    int32_t local[3];
    for(int k=0;k<3;++k) local[k]=(int32_t)std::lround(initial.t[k]*4096);
    localToWorld(local,d.pos);
    for(int i=0;i<3;++i) for(int j=0;j<3;++j)
        for(int k=0;k<3;++k) r[i][j] += axes[k][i]/4096.f*initial.r[k][j];
    quaternion(r,d.q);
    int32_t direction[3]={d.pos[0]-pos[0],d.pos[1]-pos[1],0};
    const double length=std::hypot((double)direction[0],(double)direction[1]);
    if(length) for(int k=0;k<2;++k) direction[k]=(int32_t)std::lround(direction[k]*4096/length);
    for(int k=0;k<3;++k) d.velocity[k]=vel[k]+mul(direction[k],strength*2);
    const int32_t random[3]={(int32_t)Rand32NonCritical(0x14000)-0xA000,
                             (int32_t)Rand32NonCritical(0x14000)-0xA000,
                             (int32_t)Rand32NonCritical(0x1E000)+0x1E000};
    const int32_t scale=(int32_t)(((int64_t)strength*0x19A00000)>>32);
    for(int k=0;k<3;++k) d.velocity[k]+=mul(random[k],scale);
    d.angular[0]=(int32_t)Rand32NonCritical(0x14000)-0xA000;
    d.angular[1]=(int32_t)Rand32NonCritical(0x2000)-0x1000;
    d.angular[2]=(int32_t)Rand32NonCritical(0x14000)-0xA000;
    if(slot==4) std::swap(d.angular[1],d.angular[2]);
    d.detached=true; flapBits_ |= 1<<slot;
    return true;
}

void Vehicle::explosionDebris(const Model* model) {
    if (!model) return;
    // cVehicle::Process's explosive destruction branch: a separate critical roll for each flap.
    for(int slot=0;slot<5;++slot) if(Rand16Critical(255)>=65) createDebris(slot,0xA000,*model);
}

uint32_t Vehicle::destructionState() const {
    uint32_t state = (uint32_t)(flapBits_ & 31) << 9;
    for (int i = 0; i < 5; ++i) if (debris_[i].detached) state |= 1u << (14 + i);
    for (int i = 0; i < 2; ++i) if (tyre_[i].burst) state |= 1u << (19 + i);
    return state | ((uint32_t)std::clamp((int)burnTimer_, 0, 1023) << 21);
}

void Vehicle::getVisual(CtwVehicleVisual& s, uint32_t frame) const {
    s = {}; s.size = sizeof s;
    s.flags = (engineOn ? 1u : 0u) | (siren ? 2u : 0u) | ((uint32_t)indicators << 2) |
              (hazards ? 16u : 0u) | (railBraking ? 32u : 0u);
    s.controls = b64_ & 0x106; s.burnout = burnout_;
    for (int i = 0; i < 2; ++i) {
        const auto& t = tyre_[i];
        s.tyres |= ((t.spinning ? 1u : 0u) | (t.rimSparks ? 2u : 0u) | (t.onGround ? 4u : 0u)) << (i * 3);
    }
    s.steer = steerAngle_; s.bike_lean = bikeLean_; s.bike_pitch = bikePitch_; s.bike_kick = bikeKick_;
    s.doors_open = doorOpenBits_;
    for (int i = 0; i < 5; ++i) s.door_angles[i] = doors_[i].angle;
    s.frame = visualFrame(frame);
    s.source = visualSource();
}

void Vehicle::setVisual(const CtwVehicleVisual& s, uint32_t frame, int preserveDoor) {
    engineOn = (s.flags & 1) && !dead_; siren = (s.flags & 2) != 0;
    indicators = (uint8_t)((s.flags >> 2) & 3); hazards = (s.flags & 16) != 0; railBraking = (s.flags & 32) != 0;
    b64_ = (uint16_t)((b64_ & ~0x106u) | (s.controls & 0x106)); burnout_ = (uint8_t)s.burnout;
    for (int i = 0; i < 2; ++i) {
        auto& t = tyre_[i]; t.spinning = (s.tyres & (1u << (i*3))) != 0;
        t.rimSparks = (s.tyres & (2u << (i*3))) != 0; t.onGround = (s.tyres & (4u << (i*3))) != 0;
    }
    steerAngle_ = s.steer; bikeLean_ = s.bike_lean; bikePitch_ = s.bike_pitch; bikeKick_ = s.bike_kick;
    for (int i = 0; i < 5; ++i) if (i != preserveDoor && !(localDoors_ & (1u << i))) {
        doors_[i].angle = (uint16_t)s.door_angles[i]; doors_[i].speed = 0;
        if (s.doors_open & (1u << i)) { doorOpenBits_ |= 1u << i; doors_[i].mode = 0; }
        else { doorOpenBits_ &= (uint8_t)~(1u << i); doors_[i].mode = 0; }
    }
    visualFrameOffset_ = s.frame - frame;
    visualSource_ = s.source;
}

void Vehicle::setReplicatedDestruction(uint32_t state, const Model* model) {
    for (int i = 0; i < 5; ++i) {
        if ((flapBits_ & (1u << i)) && !(state & (1u << (9 + i)))) {
            doors_[i] = Door{}; doorOpenBits_ &= (uint8_t)~(1u << i);
        }
        if (state & (1u << (14 + i))) {
            if (!debris_[i].detached && model) createDebris(i, dead_ ? 0xA000 : 0x4000, *model);
            // Missing model data must not lose the persistent detached state.
            if (!debris_[i].detached) { debris_[i].detached = true; debris_[i].pos[2] = -0x14000; }
        } else if (debris_[i].detached) debris_[i] = Debris{};
        pendingDebris_[i] = 0;
    }
    flapBits_ = (uint8_t)((state >> 9) & 31);
    for (int i = 0; i < 2; ++i) tyre_[i].burst = (state & (1u << (19 + i))) != 0;
    burnTimer_ = (int16_t)((state >> 21) & 1023);
    if (dead_) smokeLife_ = burnTimer_;
}

void Vehicle::processDebris(const Model* model) {
    // cWheeledVehicle determines the hatch hinge direction from its initial Y translation.
    if(model && doorNode_[4]>0 && doorNode_[4]<(int)model->nodes.size())
        bootReverse_=model->nodes[doorNode_[4]].t[1]<0;
    for(int slot=0;slot<5;++slot) {
        Debris& d=debris_[slot];
        // cDebris::Process deliberately has no ground bounce; fragments fall below the world.
        if(d.detached && d.pos[2]>=-0x13FFF) {
            d.velocity[2]-=0xF68;
            for(int k=0;k<3;++k) {
                d.velocity[k]-=mul(d.velocity[k],0xCC);
                d.pos[k]+=(int32_t)(((int64_t)d.velocity[k]*17)>>9);
            }
            const int32_t x=d.angular[0]/2,y=d.angular[1]/2,z=d.angular[2]/2;
            const int32_t* q=d.q;
            const int32_t delta[4]={(int32_t)(((int64_t)q[3]*x+(int64_t)q[1]*z-(int64_t)q[2]*y)>>12),
                (int32_t)(((int64_t)q[3]*y+(int64_t)q[2]*x-(int64_t)q[0]*z)>>12),
                (int32_t)(((int64_t)q[3]*z+(int64_t)q[0]*y-(int64_t)q[1]*x)>>12),
                (int32_t)((-(int64_t)q[0]*x-(int64_t)q[1]*y-(int64_t)q[2]*z)>>12)};
            for(int k=0;k<4;++k) d.q[k]+=(int32_t)(((int64_t)delta[k]*17)>>9);
            normalise(d.q);
        }
        if(pendingDebris_[slot] && model) { createDebris(slot,pendingDebris_[slot],*model); pendingDebris_[slot]=0; }
    }
}

bool Vehicle::debrisNode(int node, const float car[16], NodeMatrix& out) const {
    for(int slot=0;slot<5;++slot) {
        const Debris& d=debris_[slot];
        if(!d.detached || doorNode_[slot]!=node) continue;
        float r[3][3]; rotation(d.q,r);
        out={}; out.parent=0;
        // Convert the independent world transform into render-car space. Children then inherit the
        // detached root normally; neither car movement nor suspension can drag a fragment along.
        for(int i=0;i<3;++i) {
            float invLength=0; for(int k=0;k<3;++k) invLength+=car[i*4+k]*car[i*4+k];
            invLength=invLength>0 ? 1/invLength : 1;
            for(int k=0;k<3;++k) {
                const float inverse=car[i*4+k]*invLength;
                out.t[i]+=inverse*(d.pos[k]/4096.f-car[12+k]);
                for(int j=0;j<3;++j) out.r[i][j]+=inverse*r[k][j];
            }
        }
        return true;
    }
    return false;
}
