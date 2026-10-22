// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kryo.to
// See LICENSE in the repository root.
#include "game.h"
#include "random.h"
#include <algorithm>
#include <cmath>

namespace {
int64_t distanceSquared(const int32_t a[3],const int32_t b[3]) {
    int64_t result=0;
    for (int k=0;k<3;++k) { const int64_t delta=(int64_t)a[k]-b[k];result+=delta*delta; }
    return result;
}
int32_t dot(const int32_t a[3],const int16_t b[3]) {
    int64_t result=0;for (int k=0;k<3;++k) result+=(int64_t)a[k]*b[k];return (int32_t)(result>>12);
}
int pickCourse() {
    // Both manoeuvre flags are set by the arrest task: the six-way instruction-level selection at 0x6fa644.
    const unsigned pick=Rand16Critical(6);
    return pick==0 || pick==2 || pick==3 ? 1 : 2;
}
// maths::InterceptPoint's Q24 roots at 0x6df524. Keep the native division-before-shift truncation, including
// its bounded horizon for distant targets and its near-linear/discriminant branches.
bool interceptTime(const int32_t delta[3],const int32_t velocity[3],int64_t sourceSpeedSquared,int32_t maximum,int32_t& time) {
    const int32_t zero[3]{};
    const int64_t c=distanceSquared(delta,zero);
    if (c>0x2710000000LL) { time=maximum;return true; }
    int64_t b=0;for (int k=0;k<3;++k) b+=(int64_t)delta[k]*velocity[k];
    const int64_t a=distanceSquared(velocity,zero)-sourceSpeedSquared;
    const int64_t discriminant=((int64_t)((uint64_t)b*(uint64_t)b)>>24)-
                               ((int64_t)((uint64_t)a*(uint64_t)c)>>24);
    if (discriminant<=0x68D) { time=maximum;return true; }
    int64_t root;
    if (std::llabs(a)<=0x68D) {
        if (!b) { time=maximum;return true; }
        root=(-c/(2*b))*0x1000000LL;
        if (root<0) return false;
    } else {
        const int64_t square=(int64_t)(uint32_t)std::sqrt((double)discriminant)*4096;
        const int64_t first=((-b-square)/a)*0x1000000LL,second=((square-b)/a)*0x1000000LL;
        if (first<0 && second<0) return false;
        root=first<0 ? second : second<0 ? first : std::min(first,second);
    }
    time=(int32_t)(std::min(root,(int64_t)maximum*4096)>>12);return true;
}
}

// Supported entity-target branches of aggressive driving (0x6fab04): select the target-relative side, follow
// the extracted offset transitions, and retain the native 60/180-frame ram/holdoff windows. Mounted weapons,
// takedown stamping and event callbacks still require their original task/event dependencies.
void Traffic::pursuitTarget(const Game& g,const Vehicle& v,Driver::Pursuit& state,int32_t out[3]) {
    const bool inCar=g.playerCar>=0 && g.playerCar<(int)g.cars.size();
    const Vehicle* target=inCar ? &g.cars[g.playerCar] : nullptr;
    const int32_t* position=target ? target->pos : g.player.pos;
    const int32_t* velocity=target ? target->vel : g.player.vel;
    const int16_t* right=target ? target->right : nullptr;
    const int16_t* forward=target ? target->fwd : g.player.fwd;
    const int16_t pedRight[3]={forward[1],(int16_t)-forward[0],0};
    if (!right) right=pedRight;
    if (!state.initialized) {
        state.course=state.offset=(uint8_t)pickCourse();state.initialized=true;
    }
    const bool expired=state.timer && !--state.timer;
    int index=state.offset;
    if ((state.course==9 || state.course==10) && index<8 && ((0x87u>>index)&1))
        index=drivingProps_.pursuitRemap(index);
    int32_t offset[2]{};
    drivingProps_.pursuitOffset(index,offset);
    // Entity targets apply their forward offset, predict motion, then apply the lateral offset (0x6ee67c).
    int32_t delta[3];
    for (int k=0;k<3;++k) { out[k]=position[k]+(int32_t)(((int64_t)forward[k]*offset[1])>>12);delta[k]=out[k]-v.pos[k]; }
    int64_t alignment=0;for (int k=0;k<3;++k) alignment+=(int64_t)forward[k]*v.fwd[k];
    const int32_t ahead=dot(delta,v.fwd);
    if (ahead>0 || alignment>0) {
        const int32_t maximum=ahead>0 ? (alignment<0 ? 0x1000 : 0x2800) : 0x2000;
        const int32_t zero[3]{};int32_t time=0;
        if (!interceptTime(delta,velocity,distanceSquared(v.vel,zero),maximum,time)) {
            const int32_t speed=(int32_t)std::sqrt((double)distanceSquared(velocity,zero));
            time=speed ? std::clamp((int32_t)(std::sqrt((double)distanceSquared(delta,zero))*4096/speed),0,maximum) : 0;
        }
        for (int k=0;k<3;++k) out[k]+=(int32_t)(((int64_t)velocity[k]*time)>>12);
    }
    for (int k=0;k<3;++k) out[k]+=(int32_t)(((int64_t)right[k]*offset[0])>>12);
    if (distanceSquared(v.pos,out)>0x190000000LL) {
        int32_t away[3];
        const int32_t length=(int32_t)std::sqrt((double)distanceSquared(v.pos,position));
        for (int k=0;k<3;++k) away[k]=length ? (int32_t)(((int64_t)v.pos[k]-position[k])*4096/length) : 0;
        state.course=(uint8_t)pickCourse();
        const int32_t side=dot(away,right), along=dot(away,forward);
        uint8_t sector;
        if (side>3784) sector=2;
        else if (side < -3784) sector=1;
        else if (along>3784) sector=7;
        else sector=(uint8_t)((along>0 ? 5 : 3)+(side>0 ? 1 : 0));
        if (sector==3 || sector==4) sector=state.course==1 ? 3 : 4;
        state.offset=sector;
    }
    const bool reached=state.course<11 && distanceSquared(v.pos,out)<=(int64_t)0x2800*0x2800;
    if (reached) {
        if (state.offset!=state.course && state.course!=9 && state.course!=10) {
            bool even=false;
            if (state.course==1) even=state.offset<=4 && state.offset!=1;
            else if (state.course==2) even=state.offset<=7 && ((1u<<state.offset)&0xE2);
            else if (state.course==7) even=state.offset<=5 && ((1u<<state.offset)&0x2B);
            else if (state.course==0) even=state.offset<=7 && ((1u<<state.offset)&0xD4);
            const int next=drivingProps_.pursuitNext(2*state.offset+(even ? 0 : 1));
            if (next>=0) state.offset=(uint8_t)next;
        } else if (state.course>=9 && state.course<=14) {
            state.course=(uint8_t)pickCourse();state.timer=0;
        } else if (state.course<=2 && (int64_t)velocity[0]*velocity[0]+(int64_t)velocity[1]*velocity[1]+
                                    (int64_t)velocity[2]*velocity[2]>603979776LL) {
            // One star always holds off from left/right rams, independently of the percentage roll.
            const int level=std::clamp(g.wanted.level(),0,6);
            const bool holding=g.wanted.tables().ramBlockPercent[level]<=(int)Rand16Critical(100) ||
                               (level==1 && (state.course==1 || state.course==2));
            if (holding) { state.course=state.offset=(uint8_t)(state.course==1 ? 12 : state.course==2 ? 13 : 14);state.timer=180; }
            else { state.course=9;state.timer=60; }
        }
    } else if (expired && state.course>=9 && state.course<=14) state.course=(uint8_t)pickCourse();
}

// Police target acquisition (0x70701c) and the in-car arrest handoff. The road-network route search,
// water/layer restrictions and mounted weapons are outside this direct physical pursuit branch.
bool Traffic::enforce(Game& g,Vehicle& v,Driver& d) {
    Pedestrians::Ped* officer=nullptr;
    for (auto& ped:g.peds.peds)
        if (ped.uid==(uint32_t)v.seatUser[0] && ped.vehicle==v.uid && ped.seat==0 && ped.type==10 && !ped.body.dead) {
            officer=&ped;break;
        }
    const int level=g.wanted.level();
    if (!d.cop || !officer || !level || g.player.dead || g.arrested() || !drivingProps_.policeData()) {
        if (d.pursuit.active) {
            if (officer && !officer->enforcer.leavingVehicle) officer->enforcer.pursuing=false;
            v.pursuing=false;v.siren=false;d.pursuit={};d.rejoining=d.aimValid=d.following=false;
            d.speedValue=(uint8_t)drivingProps_.speedValue(2);d.recovery={};
        }
        return false;
    }
    const bool inCar=g.playerCar>=0 && g.playerCar<(int)g.cars.size();
    const Vehicle* target=inCar ? &g.cars[g.playerCar] : nullptr;
    const int32_t* position=target ? target->pos : g.player.pos;
    const int64_t distance=distanceSquared(v.pos,position);
    if (!d.pursuit.active && level==1 && distance>=0x190000000LL) return false; // strictly within twenty units
    d.pursuit.active=true;v.pursuing=true;
    if (v.hasSiren() && v.infoId!=7) v.siren=true;
    officer->enforcer.pursuing=true;
    v.setToPhysics(true);d.underPhysics=true;
    bool overturned=false;
    if (target) {
        const auto ground=g.collision.ground(target->pos[0]/4096.f,target->pos[1]/4096.f,target->pos[2]/4096.f);
        int64_t upDot=0;for (int k=0;k<3;++k) upDot+=(int64_t)target->up[k]*(int32_t)std::lround(ground.normal[k]*4096.f);
        overturned=upDot < -0xF33000; // IsVehicleUpsideDown: relative to the ground normal, beyond about 162 degrees
    }
    const bool footReady=distance<=1677721600LL && (!target || target->health()<20 || overturned || target->speed()<4096);
    if (footReady || d.pursuit.leaving) {
        d.controls={};
        const int64_t along=(int64_t)v.vel[0]*v.fwd[0]+(int64_t)v.vel[1]*v.fwd[1]+(int64_t)v.vel[2]*v.fwd[2];
        d.controls.throttle=along<0 ? 4096 : -4096;d.controls.handbrake=along>=0;
        d.movementRequested=false;
        if (v.speed()<14634) {
            d.pursuit.leaving=true;officer->enforcer.leavingVehicle=true;
            // exitOccupant normally erases its traffic driver; this callback is inside that driver's update.
            g.peds.exitOccupant(g,v,0,false);
        }
        return true;
    }
    int32_t aim[3];pursuitTarget(g,v,d.pursuit,aim);
    const int32_t* motion=target ? target->vel : g.player.vel;
    d.speedValue=(uint8_t)drivingProps_.speedValue(4);
    d.controls=AIDriving::towards(v,drivingProps_,aim,(int32_t)d.speedValue<<12,motion);
    const DrivingData data(v,aim,motion);
    d.recovery.update(v,drivingProps_,data,d.stuck,d.controls);
    d.movementRequested=true;d.lastFrame=g.frame;
    return true;
}
