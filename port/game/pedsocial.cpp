// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kryo.to
// See LICENSE in the repository root.
#include "peds.h"
#include "game.h"
#include "fixedmath.h"
#include "random.h"
#include "sound.h"
#include <algorithm>
#include <cmath>

namespace {
int64_t distance(const int32_t* a,const int32_t* b) {
    int64_t n=0;for(int k=0;k<3;++k){const int64_t d=(int64_t)a[k]-b[k];n+=d*d;}return n;
}
Pedestrians::Ped* findPed(Game& game,uint32_t uid) {
    for(auto& ped:game.peds.peds) if(ped.uid==uid)return &ped;
    return nullptr;
}
int16_t face(const Player& from,const Player& to) {
    return (int16_t)(atan2f((float)(to.pos[0]-from.pos[0]),(float)(to.pos[1]-from.pos[1]))*10430.f);
}
void wait(Game& game,Pedestrians::Ped& ped,bool facing=false,int16_t heading=0) {
    Player::Input in;in.maxLevel=facing?0:2;in.moving=facing;in.heading=heading;
    ped.body.act(in,&game.pedSprites);ped.movementPrepared=true;
}
}

void Pedestrians::updateSocialSensor(Game& game,Ped& ped) {
    // Ordinary on-foot friend-cache branches of 0x87d818, 0x877558, 0x877620 and 0x877940.
    // Native scheduling uses the shared entity seed. Stable IDs supply the phase until that pool is ported.
    if (((game.frame+ped.uid)&15)!=15 || ped.vehicle || ped.body.dead) return;
    constexpr int32_t pullback=10240, reach=163840+pullback;
    constexpr int64_t range=(int64_t)reach*reach;
    const int32_t* previous=nullptr;
    if(ped.socialFriendVehicle) {
        for(const auto& car:game.cars) if(car.uid==ped.socialFriend && !car.dead()) previous=car.pos;
    } else if(auto* friendPed=findPed(game,ped.socialFriend);friendPed && !friendPed->body.dead) previous=friendPed->body.pos;
    if(previous && distance(previous,ped.body.pos)>range) previous=nullptr;
    if(!previous) {ped.socialFriend=0;ped.socialFriendVehicle=false;}
    auto consider=[&](uint32_t uid,bool vehicle,const int32_t pos[3],int type,int subtype) {
        const auto* own=record(ped.type,ped.subtype);
        const auto* other=record(type,subtype);
        if(own && other && type>=0 && type<(int)firstRecord_.size()) {
            const size_t index=(size_t)firstRecord_[type]+subtype;
            if(index<56 && (own->enemies[index>>3]>>(index&7)&1) && !(own->friends[index>>3]>>(index&7)&1))return;
        }
        int32_t delta[3];int64_t length=0;
        for(int k=0;k<3;++k){delta[k]=pos[k]-ped.body.pos[k]+(int32_t)(((int64_t)ped.body.fwd[k]*pullback)>>12);length+=(int64_t)delta[k]*delta[k];}
        if(!length || length>range)return;
        Normalise3(delta);
        int64_t dot=0;for(int k=0;k<3;++k)dot+=(int64_t)delta[k]*ped.body.fwd[k];
        if((dot>>12)<2048)return;
        if(previous) {
            if(distance(pos,ped.body.pos)>=distance(previous,ped.body.pos))return;
            if(vehicle && !ped.socialFriendVehicle)return; // a vehicle cannot displace an existing ped friend
        }
        ped.socialFriend=uid;ped.socialFriendVehicle=vehicle;previous=pos;
    };
    for(const auto& other:game.peds.peds)
        if(other.uid!=ped.uid && !other.vehicle && !other.body.dead && !other.body.hidden)
            consider(other.uid,false,other.body.pos,other.type,other.subtype);
    for(const auto& car:game.cars) {
        if(car.dead())continue;
        int type=-1,subtype=0;
        if(auto* driver=findPed(game,(uint32_t)std::max(car.seatUser[0],0))) {type=driver->type;subtype=driver->subtype;}
        else if(car.seatUser[0]==-2)type=13;
        consider(car.uid,true,car.pos,type,subtype);
    }
}

bool Pedestrians::canTalk(const Ped& ped) const {
    // Ordinary ambient actors: native capability gates at 0x858800 and 0x887d40.
    // The order host also rejects replacing the active reaction/vehicle/flee tasks represented here.
    return ped.type==1 && !ped.ambientDisabled && !ped.talkingCooldown && !ped.vehicle &&
        !ped.body.dead && !ped.body.hidden && !ped.body.attached && !ped.body.reacting() &&
        !ped.flee.on && ped.rollFrames<0 && ped.talk.role==Ped::Talk::None;
}

void Pedestrians::startTalk(Ped& ped) {
    ped.talk={};ped.talk.role=Ped::Talk::Initiator;
    ped.talk.limit=(uint8_t)(Rand16Critical(3)+1); // constructor 0x71e78c
    ped.talkingCooldown=840; // 0x8587ec
}

void Pedestrians::finishTalk(Ped& ped) {
    if(ped.talk.role==Ped::Talk::Initiator) {
        ped.wander.phase=Ped::Wander::Rejoin;ped.wander.joining=false;
    }
    ped.wander.idleAnimation=false;
    ped.talk={};
}

void Pedestrians::talkIdle(Game& game,Ped& ped) {
    auto& task=ped.talk;
    const int16_t heading=task.role==Ped::Talk::Initiator?
        (int16_t)(findPed(game,task.partner)->talk.heading^0x8000):task.heading;
    // cIdle flags 7 branch at 0x711fb0: timed gestures 266/267, legs 274.
    if(task.idleTimer && --task.idleTimer==0) {task.idle=false;task.animation=false;task.gesture=0;wait(game,ped,true,heading);return;}
    Player::Input in;in.maxLevel=0;in.moving=true;in.heading=heading;
    in.taskAnimation=task.animation || task.gesture;
    ped.body.act(in,&game.pedSprites);ped.movementPrepared=true;
    if(task.gesture) {ped.body.playOneShot(task.gesture,274,false,true);task.gesture=0;task.animation=true;}
    if(task.animation && !ped.body.stepOneShot(&game.pedSprites))return;
    Rand32Critical(5); // native idle recurrence field, retained even though flags 7 do not inspect it
    const int upper=Rand16Critical(2)==1?267:266;
    Rand32Critical(1); // the shipped flip roll always returns zero, but still consumes this stream
    task.gesture=upper;task.animation=false; // newly attached one-shot runs on the next slice
}

bool Pedestrians::talk(Game& game,Ped& ped) {
    auto& task=ped.talk;
    if(task.role==Ped::Talk::None)return false;
    auto* partner=findPed(game,task.partner);
    if(task.role==Ped::Talk::Initiator && task.state==0) {
        partner=ped.socialFriendVehicle?nullptr:findPed(game,ped.socialFriend);
        if(!partner || !canTalk(*partner)) {finishTalk(ped);wait(game,ped);return true;}
        task.partner=partner->uid;task.state=1;
        partner->talk={};partner->talk.role=Ped::Talk::Responder;
        partner->talk.partner=ped.uid;partner->talk.state=20;partner->talk.timer=60;
        wait(game,ped);return true;
    }
    if(!partner || partner->body.dead || partner->body.reacting() || partner->vehicle ||
        partner->flee.on || partner->talk.role==Ped::Talk::None || partner->talk.partner!=ped.uid) {
        finishTalk(ped);wait(game,ped);return true;
    }
    if(task.state==12) { // original master asks the slave to finish before completing itself
        if(task.role==Ped::Talk::Initiator)partner->talk.state=12;
        finishTalk(ped);wait(game,ped);return true;
    }
    if(task.role==Ped::Talk::Responder) {
        // Preserve the native child-task ordering: while a child exists, only that child is processed.
        if(task.state==20) {
            if(!task.approachStarted) {
                task.approachStarted=true;ped.talkingCooldown=840;wait(game,ped);return true;
            }
            task.heading=face(ped.body,partner->body);
            const int32_t target[2]={partner->body.pos[0]+2*partner->body.fwd[1],partner->body.pos[1]-2*partner->body.fwd[0]};
            const int64_t dx=(int64_t)ped.body.pos[0]-target[0],dy=(int64_t)ped.body.pos[1]-target[1];
            const int64_t speed2=distance(ped.body.vel,std::array<int32_t,3>{}.data());
            const int32_t radius=std::max(4096,(int32_t)((136LL*((int32_t)std::sqrt((double)speed2)>>1))>>12));
            if(dx*dx+dy*dy<=((int64_t)radius*radius&~4095LL)) {
                task.state=21;
                if(partner->talk.state==1) {partner->talk.state=10;partner->talk.timer=61;}
                wait(game,ped);
            } else gotoTarget(game,ped,target,2);
            return true;
        }
        if(task.idle) {talkIdle(game,ped);return true;}
        if(task.state==10) {
            if(!task.timer) {
                task.timer=60;task.state=21;
                if(partner->talk.state==11) {
                    partner->talk.state=partner->talk.exchanges>=partner->talk.limit?12:10;
                    if(partner->talk.state==10)partner->talk.timer=61;
                }
            } else {
                --task.timer;task.idle=true;task.idleTimer=task.timer;task.animation=false;task.gesture=0;
                TheSound().conversation(game,ped.uid); // native request at 0x71ef54
            }
            wait(game,ped,true,task.heading);return true;
        }
        wait(game,ped,task.state==11,task.heading);return true;
    }
    if(task.state==10) {
        const int16_t heading=(int16_t)(partner->talk.heading^0x8000);
        if(task.timer>=61) {
            partner->talk.state=11;--task.timer;++task.exchanges;
            TheSound().conversation(game,ped.uid); // native request at 0x71e924
        } else if(!task.timer) {
            task.idle=task.animation=false;task.gesture=0;task.state=11;partner->talk.state=10;
        } else {
            --task.timer;
            if(task.idle) {talkIdle(game,ped);return true;}
            task.idle=true;task.idleTimer=task.timer;task.animation=false;task.gesture=0;
        }
        wait(game,ped,true,heading);return true;
    }
    wait(game,ped);return true;
}
