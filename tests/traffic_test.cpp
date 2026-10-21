// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kryo.to
// See LICENSE in the repository root.
#define SDL_MAIN_HANDLED
#include "game.h"
#include "driving.h"
#include "random.h"
#include <SDL.h>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>

static int failures;
static void check(bool ok,const char* message) { if (!ok) { std::fprintf(stderr,"FAIL: %s\n",message);++failures; } }

struct TrafficTestAccess {
    static void spawnRegressions(Game& game) {
        Traffic traffic;
        roads(game.roads);
        game.cars.clear();
        const int32_t eye[3]={1000*4096,1000*4096,10*4096};
        game.view.set(eye,0,0xC000,1024,768);
        game.player.pos[0]=30*4096;game.player.pos[1]=game.player.pos[2]=0;
        game.player.vel[0]=game.player.vel[1]=game.player.vel[2]=0;
        game.player.setHeading(0);
        game.localArea.roadLinks_={{{0,0,1,0},{0,0,0,0},100*4096,0,4096}};
        int a,b,lane;int32_t position[3],ratio;
        RandInit(0);
        check(traffic.createPos(game,2,a,b,position,lane,ratio) && a==1 && b==0,
              "approaching traffic retains the best cached link beyond the ten-unit early-accept band");
        check(position[0]==0 && position[1]>60*4096 && position[1]<63*4096 && lane>=0 && lane<6,
              "cached road spawning uses the native fixed-point chord and endpoint direction gates");
        check(traffic.linkCursor_==0,"approaching spawn queries restore their network cursor");
        game.localArea.roadLinks_.clear();
        check(!traffic.createPos(game,2,a,b,position,lane,ratio),
              "uncached global roads cannot substitute for the loaded-sector road-link cache");
        Traffic::ZoneInfo zone{};
        traffic.generate(game,1,zone);
        check(traffic.spawnFallback_==4,"a failed ordinary spawn enables four successful fallback spawns");
        game.localArea.roadLinks_={{{0,0,0,0},{0,0,1,0},100*4096,0,4096}};
        check(traffic.createPos(game,2,a,b,position,lane,ratio),
              "fallback spawning permits an endpoint rejected by the approaching-direction preference");
        game.localArea.roadLinks_.clear();
    }
    static void roads(RoadNetwork& roads) {
        roads.nodes_={{0,4,0,0,0,0},{1,4,0,800,0,0},{2,4,0,1600,0,0}};
        roads.links_={1,2,0};
    }
    static void splineHeight(RoadNetwork& roads) {
        AISpline spline;
        roads.nodes_[0].z=-4;
        spline.set(roads,0,1,0,0,1,false,-1);
        check(spline.z()==0,"the native land/water height boundary includes node height minus four");
        roads.nodes_[0].z=-5;
        spline.set(roads,0,1,0,0,1,false,-1);
        check(spline.z()==-30720,"a water spline uses the native fixed minus seven-and-a-half-unit height");
        roads.nodes_[0].z=0;
        spline.inc(roads,200*4096,false);
        check(spline.z()==-30720,"spline advancement retains its initial water classification");
        spline.set(roads,0,1,0,0,1,false,-1);
        check(spline.z()==0,"resetting a spline refreshes the initial node's height classification");
    }
    static void attach(Traffic& traffic,Game& g,Vehicle& vehicle) {
        Traffic::Driver driver;
        driver.spline.set(g.roads,0,1,0,0,1,false,vehicle.uid&255);
        driver.lastFrame=g.frame;
        traffic.drivers_[vehicle.uid]=driver;
    }
    static bool props(Traffic& traffic,const std::string& path) { return traffic.drivingProps_.load(path); }
    static bool stuck(const Traffic& traffic,uint32_t uid) { return traffic.drivers_.at(uid).stuck; }
    static void blockedSpeed(Traffic& traffic,Game& game,Vehicle& vehicle) {
        Traffic::Driver driver;driver.spline.set(game.roads,0,1,0,0,1,false,0);
        vehicle.blocked=true;RandInit(17);traffic.forwardSpeed(game,vehicle,driver);
        vehicle.blocked=false;
    }
    static void proximity(Traffic& traffic,Game& game) { traffic.proximity(game); }
    static int vehicleType(Traffic& traffic,Game& game) {
        game.vehicleInfos.assign(1,VehicleInfo{});
        traffic.lists_={{0}};traffic.makeups_.resize(1);traffic.makeups_[0].entries={{0,1}};
        return traffic.vehicleFromMakeup(game,0,0);
    }
    static bool passedTarget(Traffic& traffic,Game& game,Vehicle vehicle) {
        Traffic::Driver driver;driver.aim[0]=driver.aim[1]=20*4096;
        driver.previousTargetCheck[0]=driver.previousTargetCheck[1]=0;
        vehicle.pos[0]=vehicle.pos[1]=30*4096;
        const uint32_t frame=game.frame;game.frame=8;
        const bool completed=traffic.completedAim(game,vehicle,driver);
        game.frame=frame;return completed;
    }
};

struct VehicleTestAccess { static void gear(Vehicle& vehicle,int gear) {vehicle.gear_=(int8_t)gear;} };

static Vehicle car() {
    VehicleInfo info{};
    auto s16=[&](int off,int16_t n){std::memcpy(info.raw+off,&n,2);};
    auto s32=[&](int off,int32_t n){std::memcpy(info.raw+off,&n,4);};
    s16(0x28,2*4096);s16(0x2A,4*4096);s16(0x2C,4096);
    s32(0x50,4096);s32(0x60,40*4096);s32(0x64,4*4096);s32(0x10C,4);
    const int32_t origin[3]{};
    Vehicle vehicle;vehicle.init(info,0,origin,0,0);vehicle.uid=71;
    vehicle.engineOn=true;vehicle.seatUser[0]=1;
    return vehicle;
}

int main() {
    SDL_Init(0);
    const auto directory=std::filesystem::temp_directory_path()/"ctw-traffic-synthetic";
    std::filesystem::create_directories(directory);
    const auto file=directory/"driving_props.bin";
    {
        std::ofstream out(file,std::ios::binary);
        const char magic[]="CTWDRIV2";out.write(magic,8);
        int8_t values[103][5][2]{};values[0][0][0]=8;values[0][0][1]=-10;
        out.write((const char*)values,sizeof values);
        uint16_t first[4]={124,130,0,0},second[4]={90,95,0,0};
        int32_t steer[10][2]={{4096,-2048},{2048,-4096}};
        out.write((const char*)first,sizeof first);out.write((const char*)second,sizeof second);
        out.write((const char*)steer,sizeof steer);
    }
    DrivingProps props;
    check(!props.load((directory/"missing.bin").string()),"missing driving data remains explicitly unavailable");
    check(props.load(file.string()),"synthetic signed driving table loads");
    int32_t acceleration=0,deceleration=0;
    check(props.accelDecel(0,1,acceleration,deceleration) && acceleration==32768 && deceleration==40960,
          "positive acceleration and negative deceleration bytes have the native signed interpretation");
    check(props.accelDecel(0,2,acceleration,deceleration) && acceleration==32768 && deceleration==40960,
          "an empty gear falls back once to the preceding gear");
    check(props.accelDecel(0,3,acceleration,deceleration) && acceleration==4096 && deceleration==4096,
          "two empty gears use ComputeDrivingIK's explicit one-unit defaults");
    check(!props.accelDecel(103,1,acceleration,deceleration),"out-of-range vehicle indices cannot read adjacent data");

    Vehicle vehicle=car();
    const int32_t forward[3]={0,100*4096,0},left[3]={-100*4096,0,0};
    DrivingConstraints constraints{4096,4096,32768,40960};
    DrivingData data(vehicle,forward);
    check(data.angle==0 && data.distance==100*4096 && data.steering(constraints)==0,"a forward target needs no steering");
    check(data.gas(constraints,6*4096)==4096,"stationary traffic accelerates toward its six-unit speed limit");
    DrivingData turn(vehicle,left);
    check(turn.angle==-0x4000 && turn.steering(constraints)==-4096,"a target to the left saturates left steering");
    turn.angle=50;check(turn.steering(constraints)==0,"the native fifty-angle-unit deadband is inclusive");
    turn.angle=51;check(turn.steering(constraints)==1,"steering resumes immediately outside the native deadband");
    data.forwardSpeed=10*4096;
    check(data.gas(constraints,10*4096)==0 && data.gas(constraints,0)==-4096,"speed matching coasts and overspeed applies the brake");
    data.angle=4096;
    check(data.timeToTurn(constraints)==4096 && data.speedForSteering(constraints)==128*4096,
          "turn time and half-turn speed constraint preserve native Q12 scaling");
    data.distance=0;data.targetVelocity[1]=4*4096;
    check(data.speedForDeceleration(constraints)==8192,
          "moving-target deceleration preserves the shipped square-root term rather than a substituted physics formula");
    VehicleTestAccess::gear(vehicle,0);
    DrivingConstraints neutral;
    check(AIDriving::constraints(vehicle,props,neutral) && neutral.acceleration==143892 && neutral.deceleration==86413,
          "neutral uses the native vehicle-type PatchUp branch rather than an empty controller");
    VehicleTestAccess::gear(vehicle,-1);
    check(AIDriving::constraints(vehicle,props,neutral) && neutral.turnRate==10010,
          "reverse preserves the native fixed turn-rate limit");
    VehicleTestAccess::gear(vehicle,1);
    DrivingRecovery recovery;Vehicle::Controls reverse;
    DrivingData recoveryData(vehicle,left);
    check(recovery.update(vehicle,props,recoveryData,true,reverse) && reverse.throttle==-4096 && reverse.steer==4096,
          "a stuck car starts its extracted reverse pattern toward the native side");
    for (int i=0;i<33;++i) recovery.update(vehicle,props,recoveryData,false,reverse);
    check(reverse.steer==-2048 && reverse.throttle==-4096,"the extracted reverse threshold changes steering phase");
    for (int i=0;i<30;++i) recovery.update(vehicle,props,recoveryData,false,reverse);
    check(reverse.handbrake && reverse.throttle==4096 && reverse.steer==0,"the final sixty recovery frames pull forward with the handbrake");
    for (int i=0;i<60;++i) recovery.update(vehicle,props,recoveryData,false,reverse);
    check(!recovery.active(),"the reverse maneuver releases its controls at the exact timer boundary");

    Game& game=TheGame();game.cars.clear();game.peds.clear();game.player.hidden=true;game.playerCar=-1;game.task.car=-1;
    TrafficTestAccess::roads(game.roads);
    TrafficTestAccess::splineHeight(game.roads);
    check(TrafficTestAccess::passedTarget(game.traffic,game,vehicle),
          "a diagonal crossing of the native target corridor completes instead of turning back after an overshoot");
    RandInit(17);const uint32_t next32=Rand32Critical(0);Rand16Critical(10);const uint32_t next16=Rand16Critical(0);
    RandInit(17);int nextNode=1;
    AISpline::randomNode(game.roads,nextNode,0,nullptr,-1,false,false);
    check(nextNode==2 && Rand32Critical(0)==next32 && Rand16Critical(0)==next16,
          "road selection advances the native sixteen-bit stream without disturbing population randomness");
    TrafficTestAccess::blockedSpeed(game.traffic,game,vehicle);
    check(Rand32Critical(0)==next32 && Rand16Critical(0)==next16,
          "traffic obstacle horn timing uses the native sixteen-bit stream");
    RandInit(17);
    check(TrafficTestAccess::vehicleType(game.traffic,game)==0 && Rand32Critical(0)==next32 && Rand16Critical(0)==next16,
          "vehicle makeup selection preserves the native sixteen-bit stream and leaves the independent stream alone");
    AISpline spline;spline.set(game.roads,0,1,0,0,1,false,-1);
    const int32_t onRoad[3]={0,20*4096,0};
    const int32_t projected=spline.project(game.roads,onRoad);
    check(projected>=0 && projected<4096 && spline.param()>0,
          "straight road projection preserves the native fraction truncation within one unit");
    const int32_t beyond[3]={0,200*4096,0};spline.project(game.roads,beyond);
    check(spline.param()==4091,"rejoin projection stops short of the segment endpoint as in SetRatio");
    check(TrafficTestAccess::props(game.traffic,file.string()),"traffic uses the user-local driving table");
    game.cars.push_back(vehicle);auto& npc=game.cars.back();
    game.peds.peds.emplace_back();auto& occupant=game.peds.peds.back();
    occupant.vehicle=npc.uid;occupant.body.hidden=occupant.body.attached=true;
    std::copy(npc.pos,npc.pos+3,occupant.body.pos);occupant.body.pos[1]+=4096;
    TrafficTestAccess::proximity(game.traffic,game);
    check(!npc.blocked,"a traffic car does not treat its own hidden seated driver as a pedestrian obstacle");
    occupant.vehicle=0;occupant.body.hidden=occupant.body.attached=false;
    TrafficTestAccess::proximity(game.traffic,game);
    check(npc.blocked,"a pedestrian genuinely on foot in front of a traffic car still makes it brake");
    occupant.body.pos[2]=0x5000;TrafficTestAccess::proximity(game.traffic,game);
    check(!npc.blocked,"pedestrians at the native five-unit height boundary do not block ground traffic");
    game.peds.clear();
    npc.pos[0]=20*4096;npc.pos[1]=20*4096;npc.setToPhysics(true);
    TrafficTestAccess::attach(game.traffic,game,npc);
    game.traffic.update(game);
    const auto controls=game.traffic.controls(npc.uid);
    check(game.traffic.count()==1 && npc.physicsActive(),"collision-woken traffic retains its driver's road task");
    check(controls.steer<0 && controls.throttle>0,"a displaced traffic driver turns and accelerates back toward the road");
    for (int i=0;i<45;++i) { game.traffic.afterPhysics(game);game.traffic.update(game); }
    check(!TrafficTestAccess::stuck(game.traffic,npc.uid),"native stuck records wait through forty-five motionless frames");
    game.traffic.afterPhysics(game);game.traffic.update(game);
    check(TrafficTestAccess::stuck(game.traffic,npc.uid) && game.traffic.controls(npc.uid).throttle==-4096,
          "forty-six motionless frames trigger the native reverse recovery");
    npc.pos[0]+=4096;game.traffic.afterPhysics(game);
    check(!TrafficTestAccess::stuck(game.traffic,npc.uid),"one unit of movement clears the stuck record");
    check(game.traffic.controls(999999).throttle==0,"unowned vehicles receive no traffic input");
    game.traffic.forget(npc.uid);
    npc.setToPhysics(false);npc.pos[0]=0;npc.pos[1]=20*4096;npc.setToPhysics(true);
    TrafficTestAccess::attach(game.traffic,game,npc);
    for (int i=0;i<5;++i) game.traffic.update(game);
    check(game.traffic.count()==1 && npc.physicsActive(),"on screen a rejoined car keeps driving under physics (no snap)");
    npc.screenTimer=0;
    for (int i=0;i<5 && npc.physicsActive();++i) game.traffic.update(game);
    check(game.traffic.count()==1 && !npc.physicsActive(),"off screen on a straight road it goes back on rails");
    check(game.traffic.driveAway(game,npc) && npc.physicsActive() && game.traffic.drivingAway(npc.uid),
          "a driver reacting with reaction 3 goes under physics (cVehicle::HandleReaction)");
    for (int i=0;i<3;++i) game.traffic.update(game);
    check(npc.physicsActive() && game.traffic.controls(npc.uid).throttle>0,
          "a driver driving away follows the road under physics instead of returning to rails");
    npc.seatUser[0]=-1;game.traffic.update(game);
    check(game.traffic.count()==0,"traffic releases a driver task when its seat becomes empty");
    game.cars.clear();
    TrafficTestAccess::spawnRegressions(game);
    std::filesystem::remove(file);std::filesystem::remove(directory);
    SDL_Quit();
    return failures ? 1 : 0;
}
