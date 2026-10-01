// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kryo.to
// See LICENSE in the repository root.
#include "propdynamics.h"
#include "game.h"
#include "sound.h"
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

PropDynamics::Loose PropDynamics::makeBody(Game& g,int cx,int cy,int index) const {
    Loose l{}; l.cx=cx; l.cy=cy; l.index=index; l.prop=(*g.collision.props(cx,cy))[index];
    PropLibrary::Physics shape{};
    const auto* kind=g.props.kind(l.prop.kind);
    if(g.props.physics(l.prop.prop,shape) && kind)
        l.body.init(l.prop,shape,l.prop.prop == 0x30 ? 0 : kind->mass,kind->planar);
    else { l.body.pos[0]=l.prop.x; l.body.pos[1]=l.prop.y; l.body.pos[2]=l.prop.z; }
    return l;
}

void PropDynamics::applyForce(Game& g,Loose& l,const int32_t p[3],const int32_t f[3]) {
    const auto* kind=g.props.kind(l.prop.kind);
    if(!kind) return;
    const int32_t magnitude=mulq(length(f),0x111);
    const bool uproot=kind->uprootForce >= 0 && magnitude >= (int32_t)std::lround(kind->uprootForce*4096);
    const bool smash=kind->smashForce >= 0 && magnitude >= (int32_t)std::lround(kind->smashForce*4096);
    if(l.uprooted) {
        l.body.setToPhysics(true); l.body.applyWorldForce(p,f); l.sleepCounter=15;
    } else if(uproot && l.body.mass_ > 0) {
        l.uprooted=true; l.body.setToPhysics(true);
        if(kind->smashEffect == 8) {
            int32_t reduced[3]; for(int k=0;k<3;++k) reduced[k]=mulq(f[k],0x4CC);
            l.body.applyWorldForce(p,reduced);
            int32_t cg[3]; l.body.worldCG(cg);
            const int32_t up[3]={0,0,0x50000}; l.body.applyWorldForce(cg,up);
            l.collidable=false;
        } else if(kind->smashEffect == 38) {
            int32_t sideways[3]={f[0],f[1],0}; normalise(sideways);
            for(int k=0;k<3;++k) sideways[k]*=75;
            int32_t at[3]; l.body.worldCG(at); at[2]+=l.body.hz*2;
            l.body.applyWorldForce(at,sideways);
            const int32_t centre[3]={0,0,l.body.hz*2/5}; l.body.setLocalCG(centre);
        } else l.body.applyWorldForce(p,f);
    }
    if(smash && !l.broken) {
        l.broken=true;
        const int32_t at[3]={l.prop.x,l.prop.y,l.prop.z};
        TheSound().propSmash(at,kind->smashEffect);
        int32_t axis[3]={-f[1],f[0],0}; normalise(axis);
        if(kind->smashEffect == 7) { l.body.rotate(axis,0x16C); l.lampTimer=60; }
        else if(kind->smashEffect == 8) l.body.rotate(axis,0x3C72);
        else if(kind->smashEffect == 15 || kind->smashEffect == 25 || kind->smashEffect == 31) l.body.rotate(axis,0x1555);
        if(kind->uprootForce == -1.f || !drawnModel(g,l)) l.collidable=false;
    }
    if(l.broken || l.uprooted) {
        g.collision.setPropState(l.cx,l.cy,l.index,1);
        g.collision.setPropSolid(l.cx,l.cy,l.index,false);
    }
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
        car.applyWorldForce(point,fa,8); applyForce(g,l,point,fb);
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
        applyForce(g,a,point,fa); applyForce(g,b,point,fb);
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
                if((*list)[i].state != 0) continue;
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

const Model* PropDynamics::drawnModel(Game& g,const Loose& l) {
    return l.broken ? g.props.smashedModel(l.prop.prop) : g.props.model(l.prop.prop);
}

void PropDynamics::update(Game& g) {
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
                const auto& p=(*list)[j]; if(p.state) continue;
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
            it=loose_.erase(it); continue;
        }
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
        ++it;
    }
}

void PropDynamics::render(Game& g) const {
    for(const Loose& l:loose_) if(const Model* model=drawnModel(g,l)) {
        float M[16]; l.body.matrix(M); PropLibrary::drawModel(*model,M);
    }
}
