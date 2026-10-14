// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kryo.to
// See LICENSE in the repository root.
// Blast arithmetic and lifetimes use synthetic entities and arbitrary weapon damage, not extracted tables.
#include "game.h"
#include "explosions.h"
#include "particles.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>

static int failures;
static void check(bool ok, const char* message) {
    if (!ok) { std::fprintf(stderr, "FAIL: %s\n", message); ++failures; }
}
static const int32_t origin[3] = {-2475 * 4096, -1475 * 4096, 0};

struct GameTestAccess {
    static void process(Game& g) { g.processVehicleDeaths(); }
    static size_t count(const Game& g) { return g.explosions_.size(); }
    static void blast(Game& g, int type = 4) { g.explosions_.push_back({{origin[0],origin[1],origin[2]},3,type}); }
    static bool waiting(const Game& g, size_t index, int delay) {
        return index < g.explosions_.size() && !g.explosions_[index].fired && g.explosions_[index].delay == delay;
    }
    static void cameraStarted(const Game& g, const int32_t eye[3]) {
        check(g.deathFrames_ == 0 && !g.deathCameraSettled_ && std::equal(eye,eye+3,g.deathEye_),
              "fatal on-foot blasts capture the current death camera without waiting for another collision");
    }
};
struct PlayerTestAccess {
    static int reaction(const Player& p) { return p.reaction_; }
};
struct VehicleTestAccess {
    static void health(Vehicle& v, int amount) { v.health_ = (uint8_t)amount; }
    static void sleep(Vehicle& v) { v.setToPhysics(false); v.setToSimple(true); }
    static void dead(Vehicle& v) { v.health_ = 0; v.setDead(); v.justDied = false; }
    static bool fullPhysics(const Vehicle& v) { return v.physics_ && !v.simple_; }
};
struct PropTestAccess {
    static void explosive(Game& g) {
        PropLibrary::Def def{}; def.model=100; def.broken=0xFFFE; def.kind=0;
        def.shapes = {{1,{0,0,0,2048,2048,4096}}};
        g.props.defs_.push_back(def);
        g.props.kinds_.push_back({1,15,.1f,.1f,28,4096,false});
    }
    static void anchored(Game& g, int health = 90, int effect = 28) {
        explosive(g);
        auto& kind = g.props.kinds_[0];
        kind.health = (uint8_t)health; kind.uprootForce = -1; kind.smashForce = -1; kind.smashEffect = effect;
    }
    static void weaponGates(Game& g, bool bullet, bool melee) {
        g.props.kinds_[0].bulletDamage=bullet; g.props.kinds_[0].meleeDamage=melee;
    }
};
struct CollisionTestAccess {
    static void props(Game& g, int count) {
        auto cell = std::make_unique<Collision::Cell>(); cell->loaded = true;
        for (int i = 0; i < count; ++i) {
            Collision::Prop p{}; p.x=origin[0]+(i%8)*4096; p.y=origin[1]+(i/8)*4096;
            cell->props.push_back(p);
            Collision::Cell::PropShapes shapes{}; shapes.box0=(uint32_t)cell->boxes.size();
            shapes.cyl0=(uint32_t)cell->cyls.size(); shapes.mesh0=(uint32_t)cell->meshes.size();
            g.props.shapes(p,cell->boxes,cell->cyls,cell->meshes);
            shapes.boxes=(uint32_t)cell->boxes.size()-shapes.box0;
            shapes.cyls=(uint32_t)cell->cyls.size()-shapes.cyl0;
            shapes.meshes=(uint32_t)cell->meshes.size()-shapes.mesh0;
            cell->propShapes.push_back(std::move(shapes));
        }
        g.collision.cells_[2020] = std::move(cell);
    }
};
struct PropDynamicsTestAccess {
    static bool moving(const Game& g) {
        return !g.propDynamics.loose_.empty() && g.propDynamics.loose_[0].body.active();
    }
    static int health(Game& g) { return g.propDynamics.health(g,g.propDynamics.makeBody(g,20,20,0)); }
    static void damage(Game& g, int amount, int kind) {
        auto prop=g.propDynamics.makeBody(g,20,20,0);
        const int32_t direction[3]={4096,0,0}; g.propDynamics.damage(g,prop,amount,kind,direction);
    }
    static bool anchoredBroken(const Game& g) {
        return g.propDynamics.loose_.size()==1 && g.propDynamics.loose_[0].broken &&
               !g.propDynamics.loose_[0].uprooted && !g.propDynamics.loose_[0].body.active();
    }
};

static Vehicle makeCar(int units = 0) {
    VehicleInfo info{};
    auto word = [&](int offset, int value) { info.raw[offset]=(uint8_t)value; info.raw[offset+1]=(uint8_t)(value>>8); };
    word(0x28,4096); word(0x2A,8192); word(0x2C,4096);
    word(0x50,4096); word(0x94,1);
    int32_t at[3] = {origin[0]+units*4096,origin[1],origin[2]};
    Vehicle car; car.init(info,0,at,0,0); return car;
}
static void move(Player& p, int units) {
    std::copy(origin,origin+3,p.pos); p.pos[0] += units * 4096;
}

static void arithmetic() {
    const ExplosionBlast vehicle(origin,4,4096,80), prop(origin,0,4096,80), small(origin,0,2048,81);
    // cExplosionGeneric::Process: AffectEntities(7 x scale, 14 x scale, 320, a5, a6, 0): a5 (vehicles and props) is the
    // grenade's damage halved, a6 (pedestrians) the whole of it.
    check(vehicle.damageRadius == 7*4096 && vehicle.forceRadius == 14*4096 &&
          vehicle.damageQ12 == 40*4096 && vehicle.pedDamageQ12 == 80*4096,
          "generic blasts use separate 7/14-unit radii; things take half the grenade damage, people all of it");
    check(small.damageRadius == 7*2048 && small.forceRadius == 14*2048 && small.damageQ12 == 40*2048 &&
          small.pedDamageQ12 == 81*2048,
          "scale affects radius and damage, and half damage truncates to whole units before scaling");
    check(ExplosionBlast::falloff((int64_t)(7*4096/2)*(7*4096/2),7*4096) == 3079 &&
          ExplosionBlast::falloff((int64_t)(7*4096)*(7*4096),7*4096) == 29,
          "quadratic falloff preserves the reference reciprocal quantisation, including the nonzero radius edge");
    check(vehicle.force(0) == 320*4096 && small.force(0) == 320*4096,
          "generic blast strength does not scale with its radius");
    int32_t point[3]; std::copy(origin,origin+3,point); int damage = -1;
    check(vehicle.pedHit(point,false,damage) && damage == 80, "ped damage uses the whole of the main damage");
    check(vehicle.pedHit(point,true,damage) && damage == 40, "players take half the ped damage for vehicle blasts");
    check(prop.pedHit(point,true,damage) && damage == 20, "kind-7 generic prop blasts halve player damage again");
    point[0] += 8*4096;
    check(vehicle.pedHit(point,true,damage) && damage == 0, "players receive fringe knockdowns beyond the damage radius");
    point[0] = origin[0]+9*4096;
    check(!vehicle.pedHit(point,true,damage), "player knockdown reach uses the squared 0x5C2 factor");
    point[0] = origin[0]+13*4096;
    check(vehicle.pedHit(point,false,damage) && !damage, "NPC blast reach extends farther than player reach");
    point[2] = 20*4096;
    check(!vehicle.pedHit(point,false,damage), "blast range includes height rather than just ground distance");
    check(vehicle.vehicleDamage(0,4096,false) == 50 && vehicle.vehicleDamage(0,4096,true) == 50 &&
          prop.vehicleDamage(0,4096,true) == 20, "vehicle-blast multiplier and occupied generic-prop protection remain distinct");
    check(vehicle.vehicleDamage(8*4096,3*4096,false) == 24 && vehicle.vehicleDamage(11*4096,3*4096,false) == 0,
          "vehicles just beyond the damage radius use their bounding radius, but distant vehicles are untouched");
}

static void pedestrians() {
    const int32_t direction[3] = {4096,0,10*4096};
    Player p; move(p,0);
    check(p.explosionHit(20,direction) && p.health == 107 && PlayerTestAccess::reaction(p) == 7 &&
          p.vel[0] == 40*4096 && p.vel[2] == 20*4096 && !p.onGround(),
          "survivors get a strength-20 high knockdown independent of blast height");
    p.vel[0] = 123; p.vel[2] = 456;
    check(!p.explosionHit(20,direction) && p.health == 87 && p.vel[0] == 123 && p.vel[2] == 456,
          "an existing response does not block health damage or receive a second launch impulse");
    Player dying; dying.health=10;
    check(dying.explosionHit(20,direction) && dying.dead && PlayerTestAccess::reaction(dying) == 2 && !dying.vel[2],
          "fatal health damage installs SetDead's default animation before blast knockdown");
    Player attached; attached.attached=true;
    check(!attached.explosionHit(100,direction) && attached.health == 127 && !attached.reacting(),
          "attached bodies cannot receive on-foot blast damage");
    Game g; move(g.player,100); g.peds.peds.resize(4);
    for (auto& ped:g.peds.peds) move(ped.body,0);
    g.peds.peds[1].vehicle=77; g.peds.peds[1].body.hidden=true;
    g.peds.peds[2].body.explosionHit(1,direction); g.peds.peds[2].deadFrames=0;
    g.peds.peds[3].body.health=5;
    const ExplosionBlast blast(origin,4,4096,80); g.peds.explosion(g,blast);
    check(g.peds.peds[0].body.health == 47 && g.peds.peds[0].deadFrames == 0 &&
          g.peds.peds[1].body.health == 127 && !g.peds.peds[1].body.reacting(),
          "NPC wrapper registers a living knockdown and excludes seated occupants");
    check(g.peds.peds[2].body.health == 46 && g.peds.peds[3].body.dead,
          "already knocked-down NPCs still take damage, and lethal blasts register death");
    g.peds.explosion(g,blast);
    check(g.peds.peds[3].body.health == 0, "dead pedestrians cannot be repeatedly damaged");
}

static void vehicles() {
    const ExplosionBlast blast(origin,4,4096,80);
    Vehicle car=makeCar(8); VehicleTestAccess::sleep(car);   // beyond the 7-unit damage radius, within its bound radius
    car.explosionHit(blast,false);
    check(car.health() == 234 && VehicleTestAccess::fullPhysics(car) && car.vel[0] > 0 && car.vel[2] > 0,
          "a parked car takes blast damage, wakes full physics and receives outward/upward velocity");
    car.act({},false,nullptr);
    check(VehicleTestAccess::fullPhysics(car), "the next vehicle Act does not settle away a parked-car blast");
    car.releaseEffects();
    Vehicle far=makeCar(90); VehicleTestAccess::sleep(far); far.explosionHit(blast,false);
    check(far.health() == 255 && !far.vel[0] && !far.vel[2], "out-of-range vehicles are unchanged");
    Vehicle lethal=makeCar(); VehicleTestAccess::health(lethal,20); lethal.explosionHit(blast,false);
    check(lethal.dead() && lethal.justDied && lethal.health() == 0, "lethal blast damage schedules an unoccupied vehicle's destruction once");
    lethal.justDied=false; lethal.explosionHit(blast,false);
    check(!lethal.justDied && lethal.vel[2] > 0, "wrecks can receive blast forces without another death event");
    lethal.releaseEffects();
    Vehicle occupied=makeCar(); VehicleTestAccess::health(occupied,20); occupied.explosionHit(blast,true);
    check(!occupied.dead() && occupied.health() == 30 && !occupied.justDied,
          "a player-occupied car catches fire instead of being killed immediately by blast damage");
    occupied.releaseEffects();
}

static void queues() {
    Game g; move(g.player,0); GameTestAccess::blast(g);
    int32_t eye[3]; g.camera.position(eye);
    GameTestAccess::process(g); GameTestAccess::process(g);
    check(g.player.health == 127 && !g.player.reacting(), "delayed blasts do not damage during their first two Process calls");
    GameTestAccess::process(g);
    check(g.player.health == 87 && g.player.reacting(), "the blast applies its damage on the third Process call");
    for (int i=0;i<10;++i) GameTestAccess::process(g);
    check(g.player.health == 87 && GameTestAccess::count(g) == 1, "the retained final state never reapplies damage");
    GameTestAccess::process(g); check(!GameTestAccess::count(g), "final state expires on its eleventh retained Process call");
    Game fatal; move(fatal.player,0); fatal.player.health=5; GameTestAccess::blast(fatal);
    fatal.camera.position(eye);
    for (int i=0;i<3;++i) GameTestAccess::process(fatal);
    check(fatal.player.dead && fatal.player.health == 0, "on-foot lethal blasts kill the player");
    GameTestAccess::cameraStarted(fatal,eye);
    Game chain; move(chain.player,100); chain.cars.push_back(makeCar(5)); VehicleTestAccess::health(chain.cars[0],20);
    GameTestAccess::blast(chain);
    for (int i=0;i<3;++i) GameTestAccess::process(chain);
    check(chain.cars[0].justDied && GameTestAccess::count(chain) == 1, "blast damage marks a nearby car for a chain reaction");
    GameTestAccess::process(chain);
    check(!chain.cars[0].justDied && GameTestAccess::count(chain) == 2 && GameTestAccess::waiting(chain,1,2),
          "the death hook queues one delayed secondary blast without recursively firing it");
    for (int i=0;i<2;++i) GameTestAccess::process(chain);
    check(GameTestAccess::count(chain) == 2, "the secondary car blast fires without duplicate death explosions");
    chain.cars[0].releaseEffects();
    Game props; move(props.player,100); PropTestAccess::explosive(props); CollisionTestAccess::props(props,32);
    GameTestAccess::blast(props);
    for (int i=0;i<3;++i) GameTestAccess::process(props);
    check(props.propDynamics.looseCount() == 32 && PropDynamicsTestAccess::moving(props) &&
          GameTestAccess::count(props) == 33 && GameTestAccess::waiting(props,32,3),
          "blast forces smash explosive props and queue growth preserves all deferred newborn explosions");
    for (int i=0;i<2;++i) GameTestAccess::process(props);
    check(GameTestAccess::waiting(props,32,1), "new prop blasts receive their own full three-call delay");
    GameTestAccess::process(props);
    check(GameTestAccess::count(props) == 33 && props.propDynamics.looseCount() == 32,
          "secondary prop blasts do not repeatedly smash or duplicate their broken owners");
}

static void propHealth() {
    Game g; move(g.player,0); PropTestAccess::anchored(g,60); CollisionTestAccess::props(g,1);
    int32_t at[3]={origin[0]-4096,origin[1],origin[2]};   // 1 unit away: 39 damage a blast
    const ExplosionBlast blast(at,4,4096,80);
    const int32_t lineA[3]={origin[0]-4096,origin[1],2048}, lineB[3]={origin[0]+4096,origin[1],2048};
    check(g.collision.lineHitsBoxes(lineA,lineB,false),"the synthetic standing prop initially owns a collision shape");
    g.propDynamics.explosion(g,blast);
    check(PropDynamicsTestAccess::health(g)==21 && g.propDynamics.looseCount()==0 &&
          (*g.collision.props(20,20))[0].state==0 && !GameTestAccess::count(g),
          "partial blast damage persists on standing props without replacing collisions or starting smash effects");
    check(g.collision.lineHitsBoxes(lineA,lineB,false),"partial health damage retains the standing collision shape");
    g.propDynamics.explosion(g,blast);
    check(PropDynamicsTestAccess::health(g)==0 && PropDynamicsTestAccess::anchoredBroken(g) &&
          (*g.collision.props(20,20))[0].state==1 && GameTestAccess::count(g)==1,
          "health can smash an anchored prop that never meets either physical-force threshold");
    check(!g.collision.lineHitsBoxes(lineA,lineB,false),"lethal health destruction releases static collision ownership");
    g.propDynamics.explosion(g,blast);
    check(GameTestAccess::count(g)==1,"health destruction does not repeat a prop's explosion");

    Game reset; move(reset.player,0); PropTestAccess::anchored(reset,60); CollisionTestAccess::props(reset,1);
    reset.propDynamics.explosion(reset,blast); reset.propDynamics.update(reset);
    check(PropDynamicsTestAccess::health(reset)==21,"nearby updates preserve partial standing-prop health");
    move(reset.player,300); reset.propDynamics.update(reset);
    check(PropDynamicsTestAccess::health(reset)==60,"partial health resets with the port's distant-prop cleanup");

    Game gates; PropTestAccess::anchored(gates); CollisionTestAccess::props(gates,1);
    PropDynamicsTestAccess::damage(gates,30,1); PropDynamicsTestAccess::damage(gates,30,11);
    check(PropDynamicsTestAccess::health(gates)==90,"bullet and melee health gates reject their disabled kinds");
    PropTestAccess::weaponGates(gates,true,false); PropDynamicsTestAccess::damage(gates,30,2);
    check(PropDynamicsTestAccess::health(gates)==60,"kind-2 damage subtracts full health, not its reduced force strength");
    PropTestAccess::weaponGates(gates,false,true); PropDynamicsTestAccess::damage(gates,10,12);
    check(PropDynamicsTestAccess::health(gates)==50,"the kind-12 melee health gate is separate from bullet damage");
    PropTestAccess::weaponGates(gates,false,false); PropDynamicsTestAccess::damage(gates,10,7);
    check(PropDynamicsTestAccess::health(gates)==40,"generic blast health is not blocked by weapon-specific gates");
    PropDynamicsTestAccess::damage(gates,0,6);
    check(PropDynamicsTestAccess::health(gates)==40,"zero blast damage does not destroy a living prop");
    PropDynamicsTestAccess::damage(gates,0,4);
    check(PropDynamicsTestAccess::health(gates)==0 && GameTestAccess::count(gates)==1,
          "kind-4 damage consumes all remaining health even with zero raw amount");

    Game rejected; PropTestAccess::anchored(rejected,30,17); CollisionTestAccess::props(rejected,1);
    PropDynamicsTestAccess::damage(rejected,30,6); PropDynamicsTestAccess::damage(rejected,30,6);
    check(PropDynamicsTestAccess::health(rejected)==0 && (*rejected.collision.props(20,20))[0].state==0 &&
          !GameTestAccess::count(rejected),"a rejected smash keeps contacts but does not restore depleted health");
    for (int initial : {0,128,255}) {
        Game disabled; PropTestAccess::anchored(disabled,initial); CollisionTestAccess::props(disabled,1);
        PropDynamicsTestAccess::damage(disabled,100,6);
        check(PropDynamicsTestAccess::health(disabled)==(int8_t)initial && !GameTestAccess::count(disabled),
              "native signed nonpositive health does not enter health destruction");
    }
    Game impact; move(impact.player,0); PropTestAccess::explosive(impact); CollisionTestAccess::props(impact,1);
    impact.propDynamics.explosion(impact,ExplosionBlast(origin,4,4096,80));
    check(GameTestAccess::count(impact)==1,"force and health during the same blast cannot smash an explosive prop twice");
}

static void duplicateSuppression() {
    Game ordinary; ordinary.addPropExplosion(origin); ordinary.addPropExplosion(origin);
    check(GameTestAccess::count(ordinary)==2,"native Reset leaves radius suppression off by default");
    Game enabled; move(enabled.player,100); enabled.enableExplosionRadiusCheck(true);
    enabled.addPropExplosion(origin); enabled.addPropExplosion(origin,2048);
    check(GameTestAccess::count(enabled)==1,"explicit radius checking suppresses duplicate generic prop blasts regardless of scale");
    for (int i=0;i<3;++i) GameTestAccess::process(enabled);
    enabled.addPropExplosion(origin);
    check(GameTestAccess::count(enabled)==1,"a fired final-state explosion still suppresses nearby additions");
    for (int i=0;i<11;++i) GameTestAccess::process(enabled);
    enabled.addPropExplosion(origin);
    check(GameTestAccess::count(enabled)==1 && GameTestAccess::waiting(enabled,0,3),
          "expired explosions stop suppressing and a replacement gets its complete delay");
    enabled.enableExplosionRadiusCheck(false); enabled.addPropExplosion(origin);
    check(GameTestAccess::count(enabled)==2,"the script switch can disable checking without clearing existing explosions");

    auto nextShake = [](bool addDuplicate, int32_t offset[3]) {
        RandInit(0); Game g; move(g.player,0); g.enableExplosionRadiusCheck(true);
        g.camera.reset(g.player.pos,0); g.addPropExplosion(origin);
        const int32_t still[3]={};
        for (int i=0;i<3;++i) g.camera.update(g.player.pos,0,still,nullptr);
        if (addDuplicate) g.addPropExplosion(origin);
        g.camera.update(g.player.pos,0,still,nullptr); g.camera.shakeOffset(offset);
    };
    int32_t baseline[3], suppressed[3]; nextShake(false,baseline); nextShake(true,suppressed);
    check(std::equal(baseline,baseline+3,suppressed),"a suppressed addition does not restart camera shake or alter its random stream");

    // floor(sqrt(0x45FFFFFF)) is 34269 raw Q12 units. Height participates in the cutoff.
    for (int axis : {0,2}) for (int offset : {34269,34270}) {
        Game boundary; boundary.enableExplosionRadiusCheck(true); boundary.addPropExplosion(origin);
        int32_t at[3]; std::copy(origin,origin+3,at); at[axis]+=offset; boundary.addPropExplosion(at);
        check(GameTestAccess::count(boundary)==(offset==34269?1u:2u),
              "duplicate suppression retains the raw squared-distance boundary in both horizontal and vertical axes");
    }
    Game cells; cells.enableExplosionRadiusCheck(true);
    int32_t left[3]={origin[0]+24*4096,origin[1],0}; // one unit before this cell's edge
    int32_t right[3]={left[0]+2*4096,left[1],0};
    cells.addPropExplosion(left); cells.addPropExplosion(right);
    check(GameTestAccess::count(cells)==2,"nearby explosions outside the incoming half-unit cell rectangle are not suppressed");
    right[0]=left[0]+4096+1024; cells.addPropExplosion(right);
    check(GameTestAccess::count(cells)==2,"a half-unit query spanning a cell edge finds existing blasts in both cells");

    Game chain; move(chain.player,100); chain.enableExplosionRadiusCheck(true);
    chain.addPropExplosion(origin); chain.cars.push_back(makeCar(5)); VehicleTestAccess::dead(chain.cars[0]);
    chain.cars[0].uid=77; chain.cars[0].seatUser[0]=123;
    chain.peds.peds.resize(1); auto& driver=chain.peds.peds[0];
    driver.uid=123; driver.vehicle=77; driver.seat=0; driver.body.hidden=driver.body.attached=true;
    chain.cars[0].justDied=true; GameTestAccess::process(chain);
    check(GameTestAccess::count(chain)==1 && !chain.cars[0].justDied && chain.cars[0].dead(),
          "generic prop and vehicle blasts share suppression without cancelling the vehicle's death hook");
    check(driver.body.dead && !driver.body.hidden && !driver.body.attached && !driver.vehicle &&
          chain.cars[0].seatUser[0]==-1,"a suppressed vehicle blast still releases and kills its seated driver");
    chain.cars[0].releaseEffects();
}

int main() {
    namespace fs=std::filesystem;
    const fs::path dir=fs::temp_directory_path()/"ctw-explosion-test"; fs::create_directories(dir);
    WeaponTables table{}; table.info[20].damage=80;
    for (int& life : table.traceLife) life=3;
    {
        std::ofstream f(dir/"weapon_tables.bin",std::ios::binary);
        f.write("CTWWPN01",8); f.write((const char*)table.info,sizeof table.info);
        f.write((const char*)table.defaultAmmo,sizeof table.defaultAmmo); f.write((const char*)table.traceLife,sizeof table.traceLife);
    }
    check(Weapons_Init(dir.string()),"synthetic blast weapon table loads");
    RandInit(0); arithmetic(); pedestrians(); vehicles(); queues(); propHealth(); duplicateSuppression();
    fs::remove_all(dir);
    return failures ? EXIT_FAILURE : EXIT_SUCCESS;
}
