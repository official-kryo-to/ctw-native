// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kryo.to
// See LICENSE in the repository root.
#include "game.h"
#include "trafficavoidance.h"
#include <algorithm>
#include <climits>
#include <cstdio>

static int failures;
static void check(bool ok, const char* message) {
    if (!ok) { std::fprintf(stderr, "FAIL: %s\n", message); ++failures; }
}

struct VehicleTestAccess { static void simple(Vehicle& car, bool value) { car.simple_ = value; } };

static Vehicle makeCar(uint32_t uid, int x = 0, int y = 0) {
    const VehicleInfo info{};
    const int32_t at[3] = {x*4096, y*4096, 0};
    Vehicle car;
    car.init(info, 0, at, 0, 0);
    car.uid = uid; car.hx = 4096; car.hy = 8192; car.hz = 4096;
    car.setToPhysics(true);
    return car;
}

static void prepare(Game& game) {
    std::fill(game.player.pos, game.player.pos+3, 0);
    game.scheduler.reset(); game.localArea.reset(game.scheduler);
}

static void seatedDriver(Game& game) {
    game.playerCar=0; game.player.hidden=true; game.cars[0].seatUser[0]=-2;
}

static void nextFrame(Game& game, TrafficAvoidance& cache) {
    ++game.frame;
    game.localArea.update(game);
    cache.update(game);
}

static void rebuild(Game& game, TrafficAvoidance& cache) {
    for (int i=0;i<64;++i) {
        nextFrame(game, cache);
        if (game.localArea.rebuiltEntities()) return;
    }
    check(false, "the local-area schedule reaches an entity rebuild");
}

static void predictions() {
    const int32_t origin[3]{}, moving[3]={8*4096,0,0}, still[3]{}, tenAhead[3]={10*4096,0,0};
    TrafficAvoidance::Prediction hit;
    check(TrafficAvoidance::predict(origin,moving,4096,tenAhead,still,4096,INT_MAX,hit),
          "eight units per second catches a stationary obstacle ten units ahead");
    check(hit.time==131072 && hit.source[0]==8*4096 && hit.obstacle[0]==10*4096 && hit.duration==65536,
          "the native time scale places contact at one second with two units between centres");
    check(TrafficAvoidance::predict(origin,moving,4096,tenAhead,still,4096,131072,hit) &&
          !TrafficAvoidance::predict(origin,moving,4096,tenAhead,still,4096,131071,hit),
          "an intercept at the exact time horizon is accepted, a later one is rejected");
    const int32_t retreating[3]={4*4096,0,0};
    hit={};
    check(TrafficAvoidance::predict(origin,moving,4096,tenAhead,retreating,4096,INT_MAX,hit) &&
          hit.time==262144 && hit.source[0]==16*4096 && hit.obstacle[0]==18*4096,
          "prediction advances both centres and follows their relative closing speed");
    check(!TrafficAvoidance::predict(origin,moving,4096,tenAhead,moving,4096,INT_MAX,hit),
          "equal velocities do not manufacture an intercept");
    const int32_t belowThreshold[3]={2912,0,0}, aboveThreshold[3]={2944,0,0};
    check(!TrafficAvoidance::predict(origin,belowThreshold,4096,tenAhead,still,4096,INT_MAX,hit) &&
          TrafficAvoidance::predict(origin,aboveThreshold,4096,tenAhead,still,4096,INT_MAX,hit),
          "the original relative-speed threshold excludes slow near-parallel contacts");
    const int32_t tangent[3]={10*4096,2*4096,0}, penetrating[3]={10*4096,2*4096-1,0};
    check(!TrafficAvoidance::predict(origin,moving,4096,tangent,still,4096,INT_MAX,hit) &&
          TrafficAvoidance::predict(origin,moving,4096,penetrating,still,4096,INT_MAX,hit),
          "the discriminant gate rejects exact tangency while retaining a genuine crossing");
}

struct TrafficAvoidanceTestAccess {
    static void pedestrianSources() {
        Game game;prepare(game);TrafficAvoidance cache;
        game.cars.push_back(makeCar(1,0,12));
        game.cars[0].vel[1]=-16*4096;
        game.cars[0].fwd[1]=-4096;
        Pedestrians::Ped ped;ped.uid=1;
        std::fill(ped.body.pos,ped.body.pos+3,0);
        ped.target[1]=20*4096;
        game.peds.peds.push_back(ped);
        const int32_t carTarget[3]={100*4096,0,0};cache.setTarget(1,carTarget);
        rebuild(game,cache);
        TrafficAvoidance::Intercept hit;
        check(cache.interceptPed(game,1,hit) && hit.uid==1 && !hit.pedestrian && hit.time<30*4096,
              "pedestrian sources use their task target and detect an approaching vehicle within thirty ticks");
        check(hit.sourceRadius==Player::kRadius,
              "pedestrian prediction uses its sphere radius and native five-percent inflation");
        game.peds.peds[0].body.setHeading((int16_t)0x8000);
        rebuild(game,cache);
        check(!cache.interceptPed(game,1,hit),"pedestrian interception keeps the original facing half-plane");
        game.peds.peds[0].body.setHeading(0);game.peds.peds[0].body.attached=true;
        rebuild(game,cache);
        check(!cache.interceptPed(game,1,hit),"attached pedestrians cannot be interception sources");
        game.peds.peds[0].body.attached=false;game.peds.peds[0].body.dead=true;
        rebuild(game,cache);
        check(!cache.interceptPed(game,1,hit),"dead pedestrians cannot request avoidance predictions");
    }
    static void capacities() {
        Game game; prepare(game);
        TrafficAvoidance cache;
        for (uint32_t i=1;i<=12;++i) game.cars.push_back(makeCar(i,(int)i));
        game.cars.push_back(makeCar(1000));
        for (uint32_t i=1;i<=14;++i) {
            Pedestrians::Ped ped; ped.uid=i; ped.body.pos[0]=(int32_t)i*4096;
            ped.body.pos[1]=ped.body.pos[2]=0; game.peds.peds.push_back(ped);
        }
        rebuild(game,cache);
        check(cache.cars_.size()==8 && cache.peds_.size()==10,
              "entity refresh retains at most eight vehicles and ten peds, including the player");
        check(std::all_of(cache.cars_.begin(),cache.cars_.end(),[](const auto& e){return e.ref.uid<=7 || e.ref.uid==1000;}) &&
              std::any_of(cache.cars_.begin(),cache.cars_.end(),[](const auto& e){return e.ref.uid==1000;}) &&
              std::any_of(cache.peds_.begin(),cache.peds_.end(),[](const auto& e){return e.ref.kind==TrafficAvoidance::Kind::Player;}),
              "nearby entities displace distant ones instead of truncating arbitrary candidate scans");
        game.cars.erase(std::remove_if(game.cars.begin(),game.cars.end(),[](const auto& car){return car.uid==1 || car.uid==1000;}),game.cars.end());
        nextFrame(game,cache);
        check(cache.cars_.size()==8 && cache.cars_[0].ref.uid==1000 &&
              cache.cars_[7].ref.kind==TrafficAvoidance::Kind::None &&
              cache.cars_[6].ref.uid==7,
              "two expired slots preserve native by-value counts and skip rechecking the expired tail swap");
        nextFrame(game,cache);
        check(cache.cars_.size()==8 && cache.cars_[0].ref.kind==TrafficAvoidance::Kind::None &&
              cache.cars_[6].ref.uid==7,
              "later cache resets preserve the same cleared tail slots until a scheduled rebuild");

        Game boundary; prepare(boundary);
        boundary.cars.push_back(makeCar(1,100)); boundary.cars.push_back(makeCar(2,100));
        --boundary.cars[1].pos[0];
        TrafficAvoidance edge; rebuild(boundary,edge);
        check(edge.cars_.size()==1 && edge.cars_[0].ref.uid==2,
              "the one-hundred-unit refresh radius excludes its exact outer boundary");
    }

    static void dimensionsAndMargins() {
        Vehicle car=makeCar(1);
        int32_t size[3];
        const int32_t north[3]={0,4096,0}, east[3]={4096,0,0}, diagonal[3]={4096,4096,0};
        TrafficAvoidance::dimensions(car,north,size);
        check(size[0]==4096 && size[1]==8192 && size[2]==0,
              "a north-facing car retains its width and length along a northbound avoidance path");
        TrafficAvoidance::dimensions(car,east,size);
        check(size[0]==8192 && size[1]==4096, "a perpendicular path exchanges the projected box dimensions");
        TrafficAvoidance::dimensions(car,diagonal,size);
        check(size[0]==8688 && size[1]==8688, "diagonal box projection includes both width and length");
        for (bool simple : {false,true}) {
            Game game; prepare(game);
            game.cars={makeCar(1),makeCar(2,0,20)};
            seatedDriver(game);
            game.cars[0].vel[1]=8*4096;
            VehicleTestAccess::simple(game.cars[0],simple);
            TrafficAvoidance cache; rebuild(game,cache);
            TrafficAvoidance::Intercept hit;
            check(cache.intercept(game,game.cars[0],hit,true,false) && hit.uid==2 &&
                  hit.sourceRadius==4300 && hit.obstacleRadius==(simple ? 4192 : 4729),
                  "simple physics contracts the native avoidance margin; full physics expands it");
        }
    }

    static void scheduledReuse() {
        Game game; prepare(game);
        game.cars={makeCar(1),makeCar(2,0,10),makeCar(3,0,20)};
        seatedDriver(game);
        game.cars[0].vel[1]=8*4096;
        TrafficAvoidance cache; rebuild(game,cache);
        TrafficAvoidance::Intercept hit;
        check(cache.intercept(game,game.cars[0],hit,true,false) && hit.uid==2,
              "the initial query chooses the earliest predicted vehicle intercept");
        game.cars[1].pos[0]=20*4096;
        nextFrame(game,cache);
        check(!cache.intercept(game,game.cars[0],hit,true,false),
              "between entity refreshes the native query recomputes its previous candidate only");
        rebuild(game,cache);
        check(cache.intercept(game,game.cars[0],hit,true,false) && hit.uid==3,
              "the next scheduled entity refresh discovers the replacement obstacle");
        bool checkedRoadFrame=false;
        for (int i=0;i<32;++i) {
            ++game.frame; game.localArea.update(game);
            if (!game.localArea.rebuiltEntities() && !game.localArea.entityCacheResetDue()) {
                auto& source=cache.cars_[0]; source.computed=source.velocityCached=true;
                cache.update(game);
                check(source.computed && source.velocityCached,
                      "the scheduled road-only refresh preserves entity computation flags");
                checkedRoadFrame=true; break;
            }
            cache.update(game);
        }
        check(checkedRoadFrame, "the alternating schedule reaches its road-only refresh");
    }

    static void pedestriansAndQueue() {
        Game game; prepare(game);
        game.cars={makeCar(1)}; game.cars[0].vel[1]=8*4096;
        seatedDriver(game);
        Pedestrians::Ped ped; ped.uid=51;
        ped.body.pos[0]=ped.body.pos[2]=0; ped.body.pos[1]=10*4096;
        game.peds.peds.push_back(ped);
        TrafficAvoidance cache; rebuild(game,cache);
        TrafficAvoidance::Intercept hit;
        check(cache.intercept(game,game.cars[0],hit,false,true) && hit.pedestrian && hit.uid==51,
              "physical drivers predict contacts with on-foot pedestrians");
        game.peds.peds[0].body.dead=true; nextFrame(game,cache);
        check(!cache.intercept(game,game.cars[0],hit,false,true),
              "a dead ped loses the original alive capability even while its body remains collidable");
        game.peds.peds[0].body.dead=false; game.peds.peds[0].body.attached=true;
        nextFrame(game,cache);
        check(!cache.intercept(game,game.cars[0],hit,false,true),
              "a ped attached to a vehicle does not create an on-foot intercept");

        Game queued; prepare(queued);
        queued.cars={makeCar(1),makeCar(2,0,20)}; queued.cars[0].vel[1]=8*4096;
        seatedDriver(queued);
        TrafficAvoidance queue; rebuild(queued,queue);
        check(queue.intercept(queued,queued.cars[0],hit,true,false), "queue test has a predicted contact");
        int32_t near[3]={0,4096,0};
        check(!queue.queue(queued,queued.cars[0],near) && near[1]==4096,
              "an aim point before the predicted contact is preserved");
        int32_t far[3]={0,100*4096,0};
        check(queue.queue(queued,queued.cars[0],far) && far[0]==0 && far[1]>hit.source[1],
              "queuing retains the original subtraction sign for its positive radius argument");
        queued.cars[0].vel[1]=0; far[1]=100*4096;
        check(!queue.queue(queued,queued.cars[0],far) && far[1]==100*4096,
              "a stationary vehicle bypasses queuing despite the predictive fallback velocity");
        queued.cars[0].vel[1]=8*4096; queued.playerCar=-1; nextFrame(queued,queue);
        check(!queue.intercept(queued,queued.cars[0],hit),
              "a booked driver handle alone cannot replace the original actually-seated driver flag");
        check(!queue.intercept(queued,makeCar(99),hit), "entities outside the scheduled cache have no intercept query");
    }
};

int main() {
    predictions();
    TrafficAvoidanceTestAccess::pedestrianSources();
    TrafficAvoidanceTestAccess::capacities();
    TrafficAvoidanceTestAccess::dimensionsAndMargins();
    TrafficAvoidanceTestAccess::scheduledReuse();
    TrafficAvoidanceTestAccess::pedestriansAndQueue();
    return failures ? 1 : 0;
}
