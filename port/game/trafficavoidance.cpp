// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kryo.to
// See LICENSE in the repository root.
#include "trafficavoidance.h"
#include "fixedmath.h"
#include "game.h"
#include <algorithm>
#include <cmath>

namespace {
int64_t squareLength(const int32_t v[3]) {
    return (int64_t)v[0]*v[0] + (int64_t)v[1]*v[1] + (int64_t)v[2]*v[2];
}
int64_t distanceSquared(const int32_t a[3], const int32_t b[3]) {
    const int32_t v[3] = {a[0]-b[0], a[1]-b[1], a[2]-b[2]};
    return squareLength(v);
}
int32_t scaled(int32_t a, int32_t b) { return (int32_t)(((int64_t)a*b)>>12); }
int32_t radiusMargin(int32_t radius, int32_t factor) { return std::min(scaled(radius, factor), 4096); }
bool driverSeated(const Game& game, const Vehicle& car) {
    // Native cSeats +32 mask 0x110 is an actual seated driver or a virtual driver, rather than a booking.
    // The port materializes traffic drivers as peds; it has no separate virtual-driver representation.
    if (car.seatUser[0]==-2) {
        return game.playerCar>=0 && game.playerCar<(int)game.cars.size() &&
               game.cars[game.playerCar].uid==car.uid && game.player.hidden;
    }
    if (car.seatUser[0]<=0) return false;
    for (const auto& ped : game.peds.peds)
        if (ped.uid==(uint32_t)car.seatUser[0] && ped.vehicle==car.uid && ped.seat==0 && ped.body.attached) return true;
    return false;
}
}

struct TrafficAvoidance::Entity {
    Ref ref;
    const Vehicle* car = nullptr;
    const Player* ped = nullptr;
    const Pedestrians::Ped* person = nullptr;
    const int32_t* pos = nullptr;
    const int32_t* vel = nullptr;
    const int16_t* fwd = nullptr;
    bool valid() const { return pos != nullptr; }
    bool alive() const { return car ? !car->dead() : ped && !ped->dead; }
    bool attached() const { return ped && (ped->attached || ped->hidden); }
};

TrafficAvoidance::Entity TrafficAvoidance::resolve(Game& game, Ref ref) {
    Entity entity;
    entity.ref = ref;
    if (ref.kind == Kind::None) return entity;
    if (ref.kind == Kind::Vehicle) {
        for (const Vehicle& car : game.cars) if (car.uid == ref.uid) { entity.car = &car; break; }
        if (entity.car) { entity.pos = entity.car->pos; entity.vel = entity.car->vel; entity.fwd = entity.car->fwd; }
    } else {
        if (ref.kind == Kind::Player) entity.ped = &game.player;
        else for (const auto& ped : game.peds.peds) if (ped.uid == ref.uid) {
            entity.ped = &ped.body; entity.person = &ped; break;
        }
        if (entity.ped) { entity.pos = entity.ped->pos; entity.vel = entity.ped->vel; entity.fwd = entity.ped->fwd; }
    }
    return entity;
}

void TrafficAvoidance::reset() { cars_.clear(); peds_.clear(); targets_.clear(); }

void TrafficAvoidance::add(std::vector<Entry>& entries, size_t capacity, Ref ref, int64_t distance) {
    // The native constructor quantizes squared world distance before its integer square root.
    Entry value;
    value.ref = ref;
    value.distance = (int64_t)(int32_t)std::sqrt((double)((distance >> 24) << 24)) * 4096;
    if (entries.size() < capacity) { entries.push_back(value); return; }
    size_t farthest = 0;
    for (size_t i = 1; i < entries.size(); ++i)
        if (entries[i].distance > entries[farthest].distance) farthest = i;
    if (value.distance < entries[farthest].distance) entries[farthest] = value;
}

void TrafficAvoidance::resetEntries(Game& game, std::vector<Entry>& entries) {
    // ResetEntityCache receives its count by value. Its local tail contracts, but the owner's slot count
    // remains unchanged until the next rebuild. It advances after a swap even if that last slot expired too.
    size_t remaining=entries.size();
    for (size_t i=0; i<remaining; ++i) {
        if (!resolve(game, entries[i].ref).valid()) {
            --remaining;
            entries[i]=entries[remaining];
            entries[remaining].ref={};
            entries[remaining].obstacle={};
            entries[remaining].force=true;
        }
        Entry& entry = entries[i];
        if (!entry.found || !resolve(game, entry.obstacle).valid()) entry.found = false;
        entry.computed = entry.velocityCached = false;
    }
}

void TrafficAvoidance::update(Game& game) {
    if (game.localArea.rebuiltEntities()) {
        cars_.clear(); peds_.clear();
        constexpr int64_t radiusSquared = 10000LL * 4096 * 4096;
        auto insert = [&](Ref ref) {
            const Entity entity = resolve(game, ref);
            if (!entity.valid()) return;
            const int64_t d2 = distanceSquared(entity.pos, game.player.pos);
            if (d2 >= radiusSquared) return;
            if (ref.kind == Kind::Vehicle) add(cars_, 8, ref, d2);
            else add(peds_, 10, ref, d2);
        };
        for (const Vehicle& car : game.cars) insert({Kind::Vehicle, car.uid});
        insert({Kind::Player, 0});
        for (const auto& ped : game.peds.peds) insert({Kind::Pedestrian, ped.uid});
    } else if (game.localArea.entityCacheResetDue()) {
        resetEntries(game, cars_); resetEntries(game, peds_);
    }
    for (auto it = targets_.begin(); it != targets_.end();) {
        if (!resolve(game, {Kind::Vehicle, it->first}).valid()) it = targets_.erase(it);
        else ++it;
    }
}

void TrafficAvoidance::setTarget(uint32_t uid, const int32_t target[3]) {
    auto& stored = targets_[uid];
    std::copy(target, target+3, stored.begin());
}

void TrafficAvoidance::avoidanceVelocity(Entry& entry, const Entity& entity) {
    if (entry.velocityCached) return;
    entry.velocityCached = true;
    std::copy(entity.vel, entity.vel+3, entry.velocity);
    if (squareLength(entry.velocity) >= 603979776) return;
    int32_t pedestrianTarget[3]{};
    const int32_t* aim = nullptr;
    if (entity.car) {
        const auto target = targets_.find(entity.ref.uid);
        if (target != targets_.end()) aim = target->second.data();
    } else if (entity.person && entity.person->rollFrames < 0 &&
               entity.person->wander.phase != Pedestrians::Ped::Wander::Mooch) {
        const auto& person = *entity.person;
        const int32_t* target = person.go.detourFrames ? person.go.target :
            person.flee.on && person.flee.straight ? person.flee.to :
            person.wander.joining ? person.wander.joinTarget : person.target;
        pedestrianTarget[0]=target[0];pedestrianTarget[1]=target[1];pedestrianTarget[2]=entity.pos[2];
        aim=pedestrianTarget;
    }
    if (!aim) {
        for (int i = 0; i < 3; ++i) entry.velocity[i] = (int16_t)(6 * entity.fwd[i]);
    } else {
        for (int i = 0; i < 3; ++i) entry.velocity[i] = aim[i] - entity.pos[i];
        if (squareLength(entry.velocity) < 2305) std::fill(entry.velocity, entry.velocity+3, 0);
        else { Normalise3(entry.velocity); for (int& component : entry.velocity) component *= 6; }
    }
}

void TrafficAvoidance::dimensions(const Vehicle& car, const int32_t direction[3], int32_t out[3]) {
    if (!squareLength(direction)) { out[0]=car.hx; out[1]=car.hy; out[2]=car.hz; return; }
    int32_t dir[3] = {direction[0],direction[1],direction[2]};
    Normalise3(dir);
    int32_t forward[3] = {car.fwd[0],car.fwd[1],0};
    if (car.fwd[2]) {
        if (forward[0] || forward[1]) Normalise3(forward);
        else { forward[0]=4096; forward[1]=0; }
    }
    int32_t right[3] = {car.right[0],car.right[1],0};
    if (std::abs((int)car.right[2]) > 4074) { right[0]=0; right[1]=4096; }
    else if (car.right[2]) Normalise3(right);
    int32_t a[3], b[3];
    for (int i=0;i<3;++i) {
        const int32_t f=scaled(forward[i],car.hy), r=scaled(right[i],car.hx);
        a[i]=f-r; b[i]=f+r;
    }
    out[0] = (int32_t)(std::max(std::abs((int64_t)-dir[1]*a[0]+(int64_t)dir[0]*a[1]),
                               std::abs((int64_t)-dir[1]*b[0]+(int64_t)dir[0]*b[1])) >> 12);
    out[1] = (int32_t)(std::max(std::abs((int64_t)dir[0]*a[0]+(int64_t)dir[1]*a[1]+(int64_t)dir[2]*a[2]),
                               std::abs((int64_t)dir[0]*b[0]+(int64_t)dir[1]*b[1]+(int64_t)dir[2]*b[2])) >> 12);
    out[2] = 0;
}

// maths::DoesIntercept: the native fixed-point quadratic, including relative-speed/discriminant thresholds.
bool TrafficAvoidance::predict(const int32_t source[3], const int32_t velocity[3], int32_t radius,
                              const int32_t obstacle[3], const int32_t obstacleVelocity[3], int32_t obstacleRadius,
                              int32_t maxTime, Prediction& out) {
    int32_t a[3], b[3], difference[3], relative[3];
    for (int i=0;i<3;++i) {
        a[i]=velocity[i]>>5; b[i]=obstacleVelocity[i]>>5;
        relative[i]=a[i]-b[i]; difference[i]=source[i]-obstacle[i];
    }
    const int64_t aa=squareLength(relative);
    if (aa < 0x20C5) return false;
    const int64_t bb=2*((int64_t)relative[0]*difference[0]+(int64_t)relative[1]*difference[1]+(int64_t)relative[2]*difference[2]);
    const int64_t sum=(int64_t)radius+obstacleRadius;
    const int64_t radiiSquared=(sum*sum)&0x3FFFFFFFFFFFF000LL;
    const int64_t cc=squareLength(difference)-radiiSquared;
    const int64_t bSquare=(int64_t)((uint64_t)bb*(uint64_t)bb);
    const int64_t product=(int64_t)(4*(uint64_t)aa*(uint64_t)cc);
    const int64_t discriminant=(bSquare>>24)-(product>>24);
    if (discriminant < 1678) return false;
    const int32_t root=(int32_t)(uint32_t)std::sqrt((double)(uint64_t)discriminant);
    out.time=(int32_t)(-(bb+(int64_t)root*4096)/(int32_t)(aa>>11));
    if (out.time > maxTime) return false;
    out.duration += (int32_t)((((int64_t)root*4294967296LL)/aa)>>8);
    for (int i=0;i<3;++i) {
        out.source[i]=source[i]+scaled(out.time,a[i]);
        out.obstacle[i]=obstacle[i]+scaled(out.time,b[i]);
    }
    return true;
}

void TrafficAvoidance::compute(Game& game, Entry& entry, const Entity& source, const std::vector<Entry>& candidates) {
    if (!source.valid() || !source.alive() || source.attached() ||
        (source.car && !driverSeated(game,*source.car))) return;
    avoidanceVelocity(entry, source);
    int32_t direction[3]={entry.velocity[0],entry.velocity[1],0};
    if (direction[0] || direction[1]) Normalise3(direction);
    int32_t sizes[3];
    if (source.car) dimensions(*source.car,direction,sizes);
    const int32_t ownRadius=source.car ? sizes[0]+204 : Player::kRadius;
    const int32_t factor=source.car ? (source.car->simple() ? -205 : 819) : 205;
    const int32_t ownInflated=ownRadius+radiusMargin(ownRadius,factor);
    for (const Entry& candidate : candidates) {
        if (candidate.ref == entry.ref) continue;
        const Entity other=resolve(game,candidate.ref);
        if (!other.valid() || !other.alive() || other.attached()) continue;
        const int32_t difference[3]={source.pos[0]-other.pos[0],source.pos[1]-other.pos[1],source.pos[2]-other.pos[2]};
        if (squareLength(difference)>0x9C3FFFFFFLL ||
            (int64_t)source.fwd[0]*difference[0]+(int64_t)source.fwd[1]*difference[1]+(int64_t)source.fwd[2]*difference[2]>=0) continue;
        int32_t radius=Player::kRadius;
        if (other.car) { dimensions(*other.car,direction,sizes); radius=sizes[0]+204; }
        const int32_t margin=radiusMargin(radius,factor);
        Prediction prediction;
        prediction.duration=radius;
        if (!predict(source.pos,entry.velocity,ownInflated,other.pos,other.vel,radius+margin,entry.hit.time,prediction)) continue;
        static_cast<Prediction&>(entry.hit)=prediction;
        entry.hit.uid=candidate.ref.uid;
        entry.hit.pedestrian=other.car==nullptr;
        entry.hit.player=candidate.ref.kind==Kind::Player;
        entry.hit.sourceRadius=ownRadius;
        entry.hit.obstacleRadius=radius+(margin>>1);
        entry.obstacle=candidate.ref;
        entry.found=true;
    }
}

bool TrafficAvoidance::intercept(Game& game, const Vehicle& vehicle, Intercept& out, bool vehicles, bool pedestrians) {
    auto found=std::find_if(cars_.begin(),cars_.end(),[&](const Entry& entry){return entry.ref.uid==vehicle.uid;});
    if (found==cars_.end()) return false;
    return query(game,*found,out,vehicles,pedestrians);
}

bool TrafficAvoidance::interceptPed(Game& game, uint32_t uid, Intercept& out) {
    auto found=std::find_if(peds_.begin(),peds_.end(),[&](const Entry& entry) {
        return entry.ref.kind==Kind::Pedestrian && entry.ref.uid==uid;
    });
    return found!=peds_.end() && query(game,*found,out,true,false);
}

bool TrafficAvoidance::query(Game& game, Entry& entry, Intercept& out, bool vehicles, bool pedestrians) {
    if (!entry.computed) {
        entry.computed=true;
        entry.found=false;
        entry.hit.time=2621440;
        const Entity source=resolve(game,entry.ref);
        if (entry.force) {
            entry.force=false;
            if (vehicles) compute(game,entry,source,cars_);
            if (pedestrians) compute(game,entry,source,peds_);
        } else if (resolve(game,entry.obstacle).valid()) {
            // Between rebuilds, the native cache recomputes only its previous intercept entity.
            Entry candidate; candidate.ref=entry.obstacle;
            compute(game,entry,source,std::vector<Entry>{candidate});
        }
    }
    if (!entry.found || !resolve(game,entry.obstacle).valid()) return false;
    out=entry.hit;
    return true;
}

bool TrafficAvoidance::queue(Game& game, const Vehicle& vehicle, int32_t target[3]) {
    if (!squareLength(vehicle.vel)) return false;
    setTarget(vehicle.uid,target);
    Intercept hit;
    if (!intercept(game,vehicle,hit)) return false;
    if (distanceSquared(target,vehicle.pos)<distanceSquared(hit.source,vehicle.pos)) return false;
    const int32_t difference[3]={vehicle.pos[0]-hit.source[0],vehicle.pos[1]-hit.source[1],vehicle.pos[2]-hit.source[2]};
    const int32_t length=(int32_t)std::sqrt((double)squareLength(difference));
    if (!length) return false;
    const int32_t radius=hit.obstacleRadius+vehicle.boundRadius();
    if (length<radius) std::copy(vehicle.pos,vehicle.pos+3,target);
    else for (int i=0;i<3;++i) {
        const int32_t unit=(int32_t)(((int64_t)difference[i]*4294967296LL/length)>>20);
        target[i]=hit.source[i]-scaled(unit,radius);
    }
    return true;
}
