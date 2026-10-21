// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kryo.to
// See LICENSE in the repository root.
#include "propdynamics.h"
#include "game.h"
#include "explosions.h"
#include "fixedmath.h"
#include "particles.h"
#include "sound.h"
#include "cargens.h"
#include <algorithm>
#include <cmath>

namespace {
int32_t mulq(int64_t a, int64_t b) { return (int32_t)((a*b) >> 12); }
int32_t length(const int32_t p[3]) { return (int32_t)std::sqrt((double)p[0]*p[0]+(double)p[1]*p[1]+(double)p[2]*p[2]); }
void normalise(int32_t p[3]) {
    const int32_t len=length(p);
    if(len) for(int k=0;k<3;++k) p[k]=(int32_t)(((int64_t)p[k]*4096)/len);
}
}

void PropDynamics::smashParticles(Game& g, Loose& l, const int32_t point[3], const int32_t force[3], int effect,
                                 const int32_t* sourcePos, const int32_t* sourceVelocity, bool vehicleImpact) {
    const int32_t at[3] = {l.prop.x,l.prop.y,l.prop.z};
    const uint32_t ambient = g.world.timeCycle().ok() ? g.world.timeCycle().colour(13) : 0x808080u;
    const int32_t half[3] = {l.body.hx,l.body.hy,l.body.hz};
    const int32_t radius = length(half); // cSimpleMover::SetCollisionPrimitiveBox's bound radius
    auto rgb = [](unsigned r,unsigned g,unsigned b) { return r | (g << 8) | (b << 16); };
    auto wood = [&](int16_t vx,int16_t vy,int32_t strength,uint32_t a,uint32_t b,bool tint = true) {
        TheParticles().add<WoodEmitter>(at,vx,vy,strength,a,b,tint,ambient);
    };
    if (effect == 9) { // Smash3: anchored hydrant, up to 25 degrees of tilt and an indefinite fountain
        int32_t direction[3] = {force[0],force[1],force[2]};
        const int32_t strength = length(direction);
        if ((int64_t)strength*strength > 0x28000) {
            normalise(direction);
            int32_t axis[3] = {direction[1],-direction[0],0};
            if ((int64_t)axis[0]*axis[0]+(int64_t)axis[1]*axis[1] < 0x28001) {
                l.broken = false; // a nearly vertical force does not snap the hydrant
                return;
            }
            normalise(axis);
            l.body.rotate(axis,(int16_t)((int64_t)std::min(strength,50*4096)*0x11C7/(50*4096)));
            l.collidable = false;
            l.body.setToPhysics(false);
        }
        const int16_t velocity[3] = {(int16_t)(-direction[0]/4),(int16_t)(-direction[1]/4),0x1800};
        l.fountain.emplace(at,velocity);
        return;
    }
    if (effect == 21) { // Smash3: a petrol pump blows up (a type-7 cExplosionBigVehicle) and leaves a petrol fire
        // (the player-state and PDA-app guards and the takedown signal for a vehicle that hit it are not ported)
        int32_t blast[3]; l.body.worldCG(blast);
        for (int k = 0; k < 3; ++k) blast[k] += (int32_t)Rand32Critical(0x332)-0x199;
        g.addBigExplosion(blast, 7, 0x1000, force);
        g.fires.start(g, at, Fires::None, 0, 900, 0, true, false, nullptr, true);
        return;
    }
    if (effect == 17) { l.broken = false; return; } // Smash1 clears success; movable does not imply broken
    if (effect == 28) {
        int32_t blast[3]; l.body.worldCG(blast);
        for (int k = 0; k < 3; ++k) blast[k] += (int32_t)Rand32Critical(0x332)-0x199;
        g.addPropExplosion(blast);
        l.collidable = false; // Smash's +0x21 branch clears entity contact flags
        return;
    }
    if ((effect == 23 || effect == 30) && vehicleImpact) {
        // Smash2's hotdog cart struck by a vehicle: a small generic blast, without the gas-jet timer.
        int32_t blast[3]; l.body.worldCG(blast);
        g.addPropExplosion(blast,0x8CC);
        return;
    }
    if (effect == 26 || effect == 27) { // Smash2: slats span the prop's transformed right axis
        int32_t cg[3], right[3], a[3], b[3], launch[3];
        l.body.worldCG(cg);
        const int32_t local[3] = {radius,0,0}; l.body.worldPosition(local,right);
        int64_t speed2 = 0;
        for (int k=0;k<3;++k) {
            a[k] = cg[k]-(right[k]-l.body.pos[k]); b[k] = cg[k]+(right[k]-l.body.pos[k]);
            launch[k] = sourceVelocity ? sourceVelocity[k] : force[k] >> 10;
            speed2 += (int64_t)launch[k]*launch[k];
        }
        if (sourcePos && speed2 < 0x28000) for (int k=0;k<3;++k) launch[k] = cg[k]-sourcePos[k];
        const uint32_t colour = effect == 26 ? rgb(255,255,255) : rgb(253,116,75);
        // Cars and dynamic props both belong to the reference's slow-launch ClassId range.
        TheParticles().add<SlatsEmitter>(point,launch,a,b,colour,colour,effect == 26,true,ambient);
        return;
    }
    // Smash3: rubbish, scaled by the bound radius; effect 35's emitter stays with the bin
    if (effect == 13 || effect == 37) {
        TheParticles().add<GarbageEmitter>(at,(int16_t)((radius*0x1EA) >> 12),false);
        return;
    }
    if (effect == 35) {
        l.garbage = TheParticles().add<GarbageEmitter>(at,(int16_t)((radius*0x332) >> 12),true);
        return;
    }
    // Smash1: these bursts have zero inherited horizontal velocity. Their colours and the
    // newsstand's second paper burst are separate from the moving furniture branches in Smash4.
    switch (effect) {
    case 14: TheParticles().add<PaperEmitter>(at,7,0x1EB,rgb(110,152,195),rgb(190,190,190),true,ambient); return;
    case 22: TheParticles().add<PaperEmitter>(at,7,0x2E1,rgb(222,222,222),rgb(222,222,222),false,ambient); return;
    case 46: case 49: wood(0,0,radius,rgb(100,90,75),rgb(100,90,75)); return;
    case 20: wood(0,0,radius,rgb(240,100,60),rgb(255,255,255)); l.collidable=false; return;
    case 50: wood(0,0,radius,rgb(80,120,140),rgb(80,120,140)); l.collidable=false; return;
    case 47: wood(0,0,radius,rgb(130,130,130),rgb(130,130,130)); return;
    case 42:
        wood(0,0,radius,rgb(110,90,70),rgb(95,115,90));
        TheParticles().add<PaperEmitter>(at,4,0x23D,rgb(115,185,240),rgb(220,200,170),false,ambient);
        return;
    case 43:
        wood(0,0,radius,rgb(150,140,110),Rand16NonCritical(2) ? rgb(255,255,255) : rgb(230,35,130)); return;
    case 45: wood(0,0,radius,rgb(155,60,60),rgb(130,130,130)); return;
    case 44: wood(0,0,radius,rgb(155,60,60),rgb(85,85,140)); return;
    case 29: wood(0,0,radius,rgb(138,145,145),rgb(126,104,66)); return;
    case 48: wood(0,0,radius,rgb(190,75,150),rgb(190,75,150)); return;
    default: break;
    }
    // Smash normalises the incoming force before passing it to Smash4. The original divides
    // that Q12 vector by fourteen for the emitter's initial horizontal velocity.
    int32_t direction[3] = {force[0],force[1],force[2]};
    if ((int64_t)direction[0]*direction[0] + (int64_t)direction[1]*direction[1] +
        (int64_t)direction[2]*direction[2] >= 0x641) normalise(direction);
    const int16_t vx = (int16_t)(direction[0]/14), vy = (int16_t)(direction[1]/14);
    const int32_t large = radius*3/2;
    if (effect == 10 || effect == 41) {
        // Smash3's effect 41 emits wood, then falls through to the glass burst shared with effect 10.
        if (effect == 41) wood(vx,vy,radius/2,rgb(75,155,105),rgb(75,155,105));
        const int32_t local[3] = {-l.body.hx,0,l.body.hz};
        int32_t start[3], right[3], point[3];
        l.body.worldPosition(local,start);
        const int32_t offset[3] = {l.body.hx*2/7,0,0};
        l.body.worldPosition(offset,right);
        for (int i = 0; i < 6; ++i) {
            for (int k = 0; k < 3; ++k) point[k] = start[k]+i*(right[k]-l.body.pos[k]);
            TheParticles().add<GlassEmitter>(point);
        }
        return;
    }
    switch (effect) {
    case 11: case 12: case 24: case 36:
        wood(vx,vy,radius,rgb(92,75,45),rgb(92,75,45)); break;
    case 32: wood(vx,vy,large,rgb(138,145,145),rgb(126,104,66),false); break;
    case 33: wood(vx,vy,large,rgb(177,34,34),rgb(255,255,255),false); break;
    case 34: wood(vx,vy,large,rgb(72,118,64),rgb(72,118,64),false); break;
    case 51: wood(vx,vy,large,rgb(255,255,255),rgb(255,255,255),false); break;
    case 52: wood(vx,vy,large,rgb(140,140,110),rgb(140,140,110)); break;
    case 53: wood(vx,vy,large,rgb(35,60,35),rgb(35,60,35)); break;
    case 54: wood(vx,vy,large,rgb(100,80,55),rgb(100,80,55)); break;
    case 55: wood(vx,vy,large,rgb(160,150,130),rgb(160,150,130)); break;
    case 39:
        wood(vx,vy,radius,Rand16NonCritical(2) ? rgb(77,65,47) : rgb(55,66,32),rgb(204,197,185)); break;
    default: break;
    }
}

PropDynamics::Loose PropDynamics::makeBody(Game& g,int cx,int cy,int index) const {
    Loose l{}; l.cx=cx; l.cy=cy; l.index=index; l.prop=(*g.collision.props(cx,cy))[index];
    PropLibrary::Physics shape{};
    const auto* kind=g.props.kindForProp(l.prop.prop);
    if(g.props.physics(l.prop.prop,shape) && kind)
        l.body.init(l.prop,shape,l.prop.prop == 0x30 ? 0 : kind->mass,kind->planar);
    else { l.body.pos[0]=l.prop.x; l.body.pos[1]=l.prop.y; l.body.pos[2]=l.prop.z; }
    return l;
}

void PropDynamics::applyForce(Game& g,Loose& l,const int32_t p[3],const int32_t f[3],
                              const int32_t* sourcePos, const int32_t* sourceVelocity, bool vehicleImpact, int forceType) {
    const auto* kind=g.props.kindForProp(l.prop.prop);
    if(!kind) return;
    const int32_t magnitude=mulq(length(f),0x111);
    const bool uproot=kind->uprootForce >= 0 && magnitude >= (int32_t)std::lround(kind->uprootForce*4096);
    const bool smash=kind->smashForce >= 0 && magnitude >= (int32_t)std::lround(kind->smashForce*4096);
    if(l.uprooted) {
        l.body.setToPhysics(true); l.body.applyWorldForce(p,f,forceType); l.sleepCounter=15;
    } else if(uproot && l.body.mass_ > 0) {
        l.uprooted=true; l.body.setToPhysics(true);
        if(kind->smashEffect == 8) {
            int32_t reduced[3]; for(int k=0;k<3;++k) reduced[k]=mulq(f[k],0x4CC);
            l.body.applyWorldForce(p,reduced,forceType);
            int32_t cg[3]; l.body.worldCG(cg);
            const int32_t up[3]={0,0,0x50000}; l.body.applyWorldForce(cg,up);
            l.collidable=false;
        } else if(kind->smashEffect == 38) {
            int32_t sideways[3]={f[0],f[1],0}; normalise(sideways);
            for(int k=0;k<3;++k) sideways[k]*=75;
            int32_t at[3]; l.body.worldCG(at); at[2]+=l.body.hz*2;
            l.body.applyWorldForce(at,sideways);
            const int32_t centre[3]={0,0,l.body.hz*2/5}; l.body.setLocalCG(centre);
            l.collidable=false; // UpRoot clears entity contacts, retaining world collisions for this effect
        } else l.body.applyWorldForce(p,f,forceType);
    }
    if(smash) this->smash(g,l,p,f,sourcePos,sourceVelocity,vehicleImpact);
    if(l.broken || l.uprooted) {
        g.collision.setPropState(l.cx,l.cy,l.index,1);
        g.collision.setPropSolid(l.cx,l.cy,l.index,false);
    }
}

void PropDynamics::smash(Game& g, Loose& l, const int32_t p[3], const int32_t f[3],
                         const int32_t* sourcePos, const int32_t* sourceVelocity, bool vehicleImpact) {
    if (l.broken) return;
    const auto* kind = g.props.kindForProp(l.prop.prop); if (!kind) return;
    l.broken = true;
    const int32_t at[3] = {l.prop.x,l.prop.y,l.prop.z};
    TheSound().propSmash(at,kind->smashEffect);
    smashParticles(g,l,p,f,kind->smashEffect,sourcePos,sourceVelocity,vehicleImpact);
    int32_t axis[3] = {-f[1],f[0],0}; normalise(axis);
    if (kind->smashEffect == 7) { l.body.rotate(axis,0x16C); l.lampTimer=60; }
    else if (kind->smashEffect == 8) l.body.rotate(axis,0x3C72);
    else if (kind->smashEffect == 15 || kind->smashEffect == 25 || kind->smashEffect == 31) l.body.rotate(axis,0x1555);
    if (l.broken && (kind->uprootForce < 0 || !drawnModel(g,l))) l.collidable=false;
    if (l.broken || l.uprooted) {
        g.collision.setPropState(l.cx,l.cy,l.index,1);
        g.collision.setPropSolid(l.cx,l.cy,l.index,false);
    }
}

int PropDynamics::health(const Game& g, const Loose& l) const {
    const auto found = damaged_.find({l.cx*100+l.cy,l.index});
    if (found != damaged_.end()) return found->second.health;
    const auto* kind = g.props.kindForProp(l.prop.prop);
    return kind ? (int8_t)kind->health : 0; // cDynamicProp::Damage reads its health byte as signed
}

void PropDynamics::damage(Game& g, Loose& l, int amount, int damageKind, const int32_t direction[3]) {
    const auto* kind = g.props.kindForProp(l.prop.prop);
    if (!kind || !l.collidable || health(g,l) <= 0) return;
    const bool bullet = damageKind == 1 || damageKind == 2;
    const bool melee = damageKind == 0 || damageKind == 11 || damageKind == 12;
    if ((bullet && !kind->bulletDamage) || (melee && !kind->meleeDamage)) return;
    // Kind 4 destroys the remaining health outright; other kinds subtract their full damage, including kind 2.
    if (damageKind != 4 && amount <= 0) return;
    const int before = health(g,l);
    const int remaining = damageKind == 4 ? 0 : std::max(0,before-amount);
    auto& state = damaged_[{l.cx*100+l.cy,l.index}];
    state.health = (int8_t)remaining; std::copy(l.body.pos,l.body.pos+3,state.pos);
    if (remaining) return;
    int32_t force[3] = {direction[0],direction[1],direction[2]}; Normalise3(force);
    const int strength = damageKind == 4 ? 0 : bullet && damageKind == 2 ? amount/3 : amount;
    for (int k=0;k<3;++k) force[k] *= strength;
    const int32_t source[3] = {}; // generic explosions currently have no source-entity ownership
    smash(g,l,source,force,nullptr,nullptr,false);
}

template<class A, class B>
bool PropDynamics::contactForces(A& a,B& b,const int32_t sphereA[4],const int32_t sphereB[4],
                                 int32_t point[3],int32_t fa[3],int32_t fb[3]) {
    int32_t relative[3]; for(int k=0;k<3;++k) relative[k]=a.vel[k]-b.vel[k];
    const int32_t radius=sphereA[3]+sphereB[3];
    double start[3],move[3],dot=0,len2=0,start2=0;
    for(int k=0;k<3;++k) {
        start[k]=sphereA[k]-(double)sphereB[k]; move[k]=relative[k]*136.0/4096.0;
        dot+=start[k]*move[k]; len2+=move[k]*move[k]; start2+=start[k]*start[k];
    }
    double t=0;
    if(start2 >= (double)radius*radius) {
        if(!len2 || dot >= 0) return false;
        const double disc=dot*dot-len2*(start2-(double)radius*radius);
        if(disc < 0) return false;
        t=(-dot-std::sqrt(disc))/len2;
        if(t < 0 || t > 1) return false;
    }
    int32_t n[3];
    for(int k=0;k<3;++k) n[k]=(int32_t)std::lround(start[k]+t*move[k]);
    const int32_t distance=length(n);
    if(!distance) return false;
    normalise(n);
    for(int k=0;k<3;++k) point[k]=sphereA[k]+mulq((int32_t)std::lround(t*136),a.vel[k])-mulq(sphereA[3],n[k]);
    int32_t va[3],vb[3],cga[3],cgb[3];
    a.velocityAt(point,va); b.velocityAt(point,vb); a.worldCG(cga); b.worldCG(cgb);
    int32_t vr[3],ra[3],rb[3]; int64_t along=0;
    for(int k=0;k<3;++k) { vr[k]=va[k]-vb[k]; along+=(int64_t)vr[k]*n[k]; ra[k]=point[k]-cga[k]; rb[k]=point[k]-cgb[k]; }
    const int32_t vn=(int32_t)(along >> 12);
    if(vn >= 0) return false;
    const uint32_t u=(uint32_t)(-vn-4096),u3=std::min(u,0xE000u);
    const int64_t e=(int32_t)u >= 0 ? (((uint64_t)(u3*0xE90)*0x12492493ULL) >> 32)&0x1FFF000 : 0;
    const int32_t terms=a.impactTerm(n,ra)+b.impactTerm(n,rb);
    const int64_t den=(int64_t)a.invMass_+b.invMass_+std::max(terms,0);
    if(den <= 0) return false;
    const int64_t q=((e*0x100000-0x1FD700000000LL)/den) >> 20;
    int32_t impulse=mulq(q,vn);
    int32_t mf=a.mass_ > 0x7FF ? 0x4000 : a.mass_*8;
    if(b.mass_ < 0x800) mf=mulq(b.mass_*2,mf);
    impulse+=mulq(std::max(radius-distance,0),mf);

    for(int k=0;k<3;++k) {
        const int32_t vt=vr[k]-mulq(vn,n[k]);
        fa[k]=mulq(mulq(-a.mass_,0x733),vt)+mulq(impulse*30,n[k]);
        fb[k]=mulq(mulq(b.mass_,0x733),vt)-mulq(impulse*30,n[k]);
    }
    return true;
}

// ResolvePhysicalCollision's sphere contacts and CalcImpactCar's reciprocal mass/inertia impulse. A standing
// prop receives the impulse before deciding whether to uproot; vehicle speed alone cannot make it break.
bool PropDynamics::hit(Game& g,Vehicle& car,Loose& l) {
    if(!l.collidable || (l.body.hx == 0 && l.body.hy == 0 && l.body.hz == 0)) return false;
    const double dx=(double)car.pos[0]-l.body.pos[0],dy=(double)car.pos[1]-l.body.pos[1];
    const double reach=car.boundRadius()+l.body.hx+l.body.hy+l.body.hz+
                       std::abs(l.body.collOffset_[0])+std::abs(l.body.collOffset_[1])+
                       (car.speed()+l.body.speed())/25.0;
    if(dx*dx+dy*dy > reach*reach) return false;
    int32_t cars[16][4],props[6][4]; int nc,np;
    car.collisionSpheres(cars,nc); l.body.spheres(props,np);
    bool applied=false;
    for(int i=0;i<nc;++i) for(int j=0;j<np && l.collidable;++j) {
        int32_t point[3],fa[3],fb[3];
        if(!contactForces(car,l.body,cars[i],props[j],point,fa,fb)) continue;
        car.applyWorldForce(point,fa,8); applyForce(g,l,point,fb,car.pos,car.vel,true);
        car.recalcKinematics(); l.body.recalcKinematics();
        if(!applied) car.onCollision(fa,nullptr);
        applied=true;
    }
    return applied;
}

bool PropDynamics::hit(Game& g,Loose& a,Loose& b) {
    if(!a.collidable || !b.collidable || (!a.body.active() && !b.body.active())) return false;
    if((!a.body.hx && !a.body.hy && !a.body.hz) || (!b.body.hx && !b.body.hy && !b.body.hz)) return false;
    const double dx=(double)a.body.pos[0]-b.body.pos[0],dy=(double)a.body.pos[1]-b.body.pos[1];
    const double radius=a.body.hx+a.body.hy+a.body.hz+b.body.hx+b.body.hy+b.body.hz+
                        (a.body.speed()+b.body.speed())/25.0;
    if(dx*dx+dy*dy > radius*radius) return false;
    int32_t sa[6][4],sb[6][4]; int na,nb;
    a.body.spheres(sa,na); b.body.spheres(sb,nb);
    bool applied=false;
    for(int i=0;i<na && a.collidable;++i) for(int j=0;j<nb && b.collidable;++j) {
        int32_t point[3],fa[3],fb[3];
        if(!contactForces(a.body,b.body,sa[i],sb[j],point,fa,fb)) continue;
        applyForce(g,a,point,fa,b.body.pos,b.body.vel); applyForce(g,b,point,fb,a.body.pos,a.body.vel);
        a.body.recalcKinematics(); b.body.recalcKinematics(); applied=true;
    }
    return applied;
}

void PropDynamics::checkImpacts(Game& g) {
    for(Vehicle& car:g.cars) {
        for(Loose& l:loose_) hit(g,car,l);
        if(car.speed() < 41) continue;
        int cx,cy; Collision::cellOfPos(car.pos[0],car.pos[1],cx,cy);
        for(int x=cx-1;x<=cx+1;++x) for(int y=cy-1;y<=cy+1;++y) {
            const auto* list=g.collision.props(x,y); if(!list) continue;
            for(int i=0;i<(int)list->size();++i) {
                if((*list)[i].state != 0 || (*list)[i].kind == 1) continue; // garage doors have a separate infinite-mass controller
                const auto& p=(*list)[i];
                float radius,height; g.props.footprint(p.prop,radius,height);
                const double reach=car.boundRadius()+radius*4096+car.speed()/25.0;
                const double dx=(double)car.pos[0]-p.x,dy=(double)car.pos[1]-p.y;
                if(dx*dx+dy*dy > reach*reach) continue;
                Loose l=makeBody(g,x,y,i);
                hit(g,car,l);
                if(l.broken || l.uprooted) loose_.push_back(std::move(l));
            }
        }
    }
}

void PropDynamics::explosion(Game& g, const ExplosionBlast& blast) {
    if (!blast.forceRadius) return;
    auto affect = [&](Loose& l) {
        if (!l.collidable || !blast.includesCell(l.body.pos)) return;
        const int64_t squared = ExplosionBlast::distanceSquared(l.body.pos, blast.pos);
        if (squared > (int64_t)blast.forceRadius * blast.forceRadius) return;
        int32_t direction[3], point[3], force[3];
        for (int k = 0; k < 3; ++k) direction[k] = l.body.pos[k] - blast.pos[k];
        if (!direction[0] && !direction[1] && !direction[2]) {
            direction[0] = (int32_t)Rand32Critical(10 * 4096) - 5 * 4096;
            direction[1] = (int32_t)Rand32Critical(10 * 4096) - 5 * 4096;
        }
        direction[2] = 4096; Normalise3(direction);
        for (int k = 0; k < 3; ++k) point[k] = l.body.pos[k] - 3 * direction[k];
        direction[2] += 2048; Normalise3(direction);
        const int32_t magnitude = blast.force(squared);
        for (int k = 0; k < 3; ++k) force[k] = (int32_t)(((int64_t)direction[k] * magnitude) >> 12);
        applyForce(g, l, point, force, nullptr, nullptr, false, 16);
        if (squared <= (int64_t)blast.damageRadius * blast.damageRadius) {
            const int amount = (int)(((int64_t)ExplosionBlast::falloff(squared,blast.damageRadius) * blast.damageQ12) >> 24);
            damage(g,l,amount,blast.damageKind(),force);
        }
    };
    for (Loose& l : loose_) affect(l);
    for (int x = std::max(0, blast.minCellX); x <= std::min(139, blast.maxCellX); ++x)
        for (int y = std::max(0, blast.minCellY); y <= std::min(99, blast.maxCellY); ++y) {
            const auto* list = g.collision.props(x, y); if (!list) continue;
            for (int i = 0; i < (int)list->size(); ++i) {
                const auto& prop = (*list)[i];
                if (prop.state || prop.kind == 1 || !g.props.kindForProp(prop.prop)) continue;
                const int32_t at[3] = {prop.x, prop.y, prop.z};
                if (ExplosionBlast::distanceSquared(at, blast.pos) > (int64_t)blast.forceRadius * blast.forceRadius) continue;
                Loose l = makeBody(g, x, y, i); affect(l);
                if (l.broken || l.uprooted) loose_.push_back(std::move(l));
            }
        }
}

void PropDynamics::waterJet(Game& g, const Fountain& fountain) {
    for (Loose& l : loose_) {
        if (!l.collidable || (!l.body.hx && !l.body.hy && !l.body.hz)) continue;
        int32_t spheres[6][4]; int n = 0;
        l.body.spheres(spheres, n);
        bool hit = false;
        for (int i = 0; i < n && !hit; ++i) hit = fountain.touches(spheres[i], spheres[i][3]);
        if (!hit) continue;
        int32_t force[3], base[3];
        fountain.jet(force, 4);
        fountain.base(base);
        applyForce(g, l, base, force, nullptr, nullptr, false, 1);
        l.sleepCounter = 15;
    }
}

const Model* PropDynamics::drawnModel(Game& g,const Loose& l) {
    return l.broken ? g.props.smashedModel(l.prop.prop) : g.props.model(l.prop.prop);
}

void PropDynamics::update(Game& g) {
    for(Loose& l:loose_) { l.body.matrix(l.previous); l.hasPrevious=true; }
    // Resolve each moving pair once, then let flying furniture transfer forces to standing props.
    for(size_t i=0;i<loose_.size();++i)
        for(size_t j=i+1;j<loose_.size();++j) hit(g,loose_[i],loose_[j]);
    const size_t movingCount=loose_.size();
    for(size_t i=0;i<movingCount;++i) {
        if(!loose_[i].collidable || !loose_[i].body.active()) continue;
        int cx,cy; Collision::cellOfPos(loose_[i].body.pos[0],loose_[i].body.pos[1],cx,cy);
        for(int x=cx-1;x<=cx+1;++x) for(int y=cy-1;y<=cy+1;++y) {
            const auto* list=g.collision.props(x,y); if(!list) continue;
            for(int j=0;j<(int)list->size();++j) {
                const auto& p=(*list)[j]; if(p.state || p.kind == 1) continue;
                float radius,height; g.props.footprint(p.prop,radius,height);
                const auto& body=loose_[i].body;
                const double reach=body.hx+body.hy+body.hz+radius*4096+body.speed()/25.0;
                const double dx=(double)body.pos[0]-p.x,dy=(double)body.pos[1]-p.y;
                if(dx*dx+dy*dy > reach*reach) continue;
                Loose standing=makeBody(g,x,y,j);
                hit(g,loose_[i],standing);
                if(standing.broken || standing.uprooted) loose_.push_back(std::move(standing));
            }
        }
    }
    int32_t focus[3]; g.focus(focus);
    for(auto it=loose_.begin();it!=loose_.end();) {
        Loose& l=*it;
        const double dx=(double)focus[0]-l.body.pos[0],dy=(double)focus[1]-l.body.pos[1];
        if(dx*dx+dy*dy > 250.0*250*4096*4096) {
            g.collision.setPropState(l.cx,l.cy,l.index,0); g.collision.setPropSolid(l.cx,l.cy,l.index,true);
            if(l.garbage) l.garbage->release();
            damaged_.erase({l.cx*100+l.cy,l.index});
            it=loose_.erase(it); continue;
        }
        if(l.fountain) l.fountain->process(g);
        if(l.lampTimer) {
            float M[16]; l.body.matrix(M);
            if(l.lampTimer >= 53) {
                int32_t axis[3]={(int32_t)std::lround(-M[9]*4096),(int32_t)std::lround(M[8]*4096),0};
                normalise(axis); l.body.rotate(axis,0x16C);
                if(l.lampTimer == 53) { l.body.setToPhysics(true); l.collidable=false; }
            }
            --l.lampTimer;
            l.body.matrix(M);
            const double horizontalUp=M[8]*M[8]+M[9]*M[9];
            if(horizontalUp > 0xFAE000 / (4096.0*4096)) {
                l.lampTimer=0; TheSound().propSmash(l.body.pos,56,900);
            }
            if(!l.lampTimer) l.body.setToPhysics(false);
        }
        if(l.garbage) {   // the rubbish follows its bin while it rolls, and ends when it settles
            const int64_t s2=(int64_t)l.body.vel[0]*l.body.vel[0]+(int64_t)l.body.vel[1]*l.body.vel[1]+
                             (int64_t)l.body.vel[2]*l.body.vel[2];
            l.garbage->follow(l.body.pos,s2);
            if(!l.body.active()) { l.garbage->release(); l.garbage=nullptr; }
        }
        if(l.body.active()) {
            l.body.process(g.collision);
            if(l.sleepCounter) --l.sleepCounter;
            if(!l.lampTimer && !l.sleepCounter && l.body.contact() && std::abs(l.body.vel[0]) <= 0x3FF &&
               std::abs(l.body.vel[1]) <= 0x3FF && std::abs(l.body.vel[2]) < 0x2000) l.body.setToPhysics(false);
            l.body.damp();
            if(l.body.active()) {
                int32_t cg[3]; l.body.worldCG(cg);
                const int32_t gravity[3]={0,0,mulq(l.body.mass_,-0x1D000)};
                l.body.applyWorldForce(cg,gravity,1);
            }
        }
        const auto damaged = damaged_.find({l.cx*100+l.cy,l.index});
        if (damaged != damaged_.end()) std::copy(l.body.pos,l.body.pos+3,damaged->second.pos);
        ++it;
    }
    for (auto it=damaged_.begin();it!=damaged_.end();) {
        const int64_t dx=(int64_t)focus[0]-it->second.pos[0], dy=(int64_t)focus[1]-it->second.pos[1];
        if (dx*dx+dy*dy > (int64_t)(250*4096)*(250*4096)) it=damaged_.erase(it);
        else ++it;
    }
}

void PropDynamics::render(Game& g) const {
    const float t=g.renderAlpha;
    const bool blend=g.interpolate && t < 1.f;
    for(const Loose& l:loose_) if(const Model* model=drawnModel(g,l)) {
        float M[16]; l.body.matrix(M);
        if(blend && l.hasPrevious) {   // translation lerped, basis blended and re-orthonormalised (PC presentation)
            const float* P=l.previous;
            float f[3],u[3];
            for(int k=0;k<3;++k) { M[12+k]=P[12+k]+(M[12+k]-P[12+k])*t; f[k]=P[4+k]+(M[4+k]-P[4+k])*t; u[k]=P[8+k]+(M[8+k]-P[8+k])*t; }
            auto norm=[](float* v) { const float n=std::sqrt(v[0]*v[0]+v[1]*v[1]+v[2]*v[2]); if(n>1e-6f) for(int k=0;k<3;++k) v[k]/=n; };
            norm(f);
            const float d=f[0]*u[0]+f[1]*u[1]+f[2]*u[2];
            for(int k=0;k<3;++k) u[k]-=f[k]*d;
            norm(u);
            const float sx=std::sqrt(M[0]*M[0]+M[1]*M[1]+M[2]*M[2]),sy=std::sqrt(M[4]*M[4]+M[5]*M[5]+M[6]*M[6]),
                        sz=std::sqrt(M[8]*M[8]+M[9]*M[9]+M[10]*M[10]);
            const float r[3]={f[1]*u[2]-f[2]*u[1],f[2]*u[0]-f[0]*u[2],f[0]*u[1]-f[1]*u[0]};
            for(int k=0;k<3;++k) { M[k]=r[k]*sx; M[4+k]=f[k]*sy; M[8+k]=u[k]*sz; }
        }
        PropLibrary::drawModel(*model,M);
    }
    WorldCamera camera; g.viewCamera(camera);
    const uint32_t ambient = g.world.timeCycle().ok() ? g.world.timeCycle().colour(13) : 0x808080u;
    for(const Loose& l:loose_) if(l.fountain) l.fountain->render(camera,ambient);
}
