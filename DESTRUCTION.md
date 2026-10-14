# Destruction and traffic audit

Checked against the locally supplied Android 4.4.243 arm64 build on 2026-10-07.
This is a progress record, not a claim that destruction is fully identical to the original.
No reference binaries, extracted tables or screenshots belong in this repository.

## Verified coverage

The local asset audit inspected all 299 prop definitions and 217 physics primitives. Of those definitions,
120 have an uproot or smash response. All 120 reacted in direct contact probes using the original data at
one or more tested speeds (6, 15, 30 and 60 world units per second). Every texture referenced by their
intact and broken models resolved: seven distinct textures, no missing references. This does not establish
that every particle, material, map placement or third-person view is correct.

A second probe ran the actual vehicle integration and prop-contact path on a synthetic flat scene, using
each reactive prop's original collision shapes and vehicle handling. At 30 units per second, three lateral
approaches per definition produced 360 trials. The old path reacted in 298; the corrected path reacted in
all 360. These results cover that controlled scene, not every map location, speed or impact angle.

The bug was duplicate collision ownership: a movable or breakable prop was also included in the vehicle's
static world sweep. Its static shape could stop the vehicle before the dynamic prop contact fired. Vehicle
and loose-body sweeps now leave those props to the dynamic resolver. Static scenery, garage gates and
pedestrian/camera queries retain their existing collision shapes. Synthetic regression tests reproduce the
previously blocked impact and check that filtering does not remove ordinary scenery.

## Implemented in this pass

- Hydrants use the `Smash3` effect-9 horizontal-force gate and capped tilt about their fixed placement.
  The fountain stays at the original base. Water-stream spray follows the type-1
  `cParticleEmitterWaterSplash` birth, direction, damping, gravity and fade rules. Positional event 0x6C
  loops while the stream is updated and is released when its owner disappears.
- Smash effect 17 can respond without incorrectly selecting a broken model. A rejected smash no longer
  disables the prop's contacts.
- Vehicle doors/hatches use `DamageDoors`, `FlapDoors`, `UpdateDoorMatrix`, `CreateDebris` and
  `cDebris::Process` as references. Loose hinges react to acceleration, angular velocity and gravity;
  the hatch direction comes from its model's initial placement. Detached geometry moves independently of
  the wreck, retaining its child nodes. Launch, drag, gravity, quaternion motion and the below-world cutoff
  follow the reference. The original fragment processor has no ground bounce. Repair restores panels.
  Burnt vehicles turn off the engine, indicators and model lighting.
- Road traffic creates driver pedestrians using the existing population appearance selection. Occupants
  track a stable vehicle ID, reserve their seat and follow the vehicle transform. Taking a driver's seat
  waits for the NPC exit animation; destruction releases and kills occupants through pedestrian damage.
  Vehicle cleanup removes its occupants without deleting unrelated pedestrians. A generated car without
  a driver is not kept as active traffic.

The synthetic tests cover contact filtering, hydrant placement/rejection, water emitter and sound lifetime,
door acceleration/hinge direction, independent fragment transforms, repair, driver ownership across vehicle
vector changes, animated exit, fatal release and cleanup. Local rendered probes also checked a broken
hydrant, moving panels and a driver's exit. Audio voice lifecycle was tested with synthetic PCM; the new
water loop has not been assessed by listening to it in a full gameplay session.

## Generic blast continuation

The next pass implements the ordinary type-0/type-4 damage and force path from
`cExplosionGeneric::Process`, `cBaseExplosion::AffectEntities`, `cPlayer::Damage`, `cPed::Damage` and
`cAnimation::ApplyPhysicalEffect`, checked against the same local reference build:

- Damage radius is 7 times scale; force radius is 14 times scale (7 and 14 units at scale 1;
  `cExplosionGeneric::Process` builds them as `scale * 28672 >> 12` and `scale * 57344 >> 12`). Damage comes from
  the grenade weapon table: pedestrians take all of it, vehicles and props half of it (`AffectEntities` takes the
  vehicle/prop damage as its fifth argument and the pedestrian damage as its sixth). Force strength is 320 and does
  not scale. Quadratic falloff retains the native quantised reciprocal, including its nonzero value at the nominal
  radius edge.
- On-foot pedestrians receive health damage and effect-2 high knockdowns. A living body with an existing
  animation still takes damage but is not launched again. Fatal health damage installs the default death
  response first. The player's smaller knockdown reach and type-0/type-4 damage reductions are separate.
  Seated occupants do not receive direct blast damage. Fatal on-foot blasts initialise the death camera.
- Vehicles receive upward-biased, off-centre type-16 force, leave simple physics and wake from rest.
  Vehicle bounds extend the damage gate; the original 1.25 multiplier and player-occupied type-0 reduction
  remain distinct. Lethal damage to a player-occupied vehicle starts burning instead of killing it instantly.
  Wrecks still receive forces without emitting another destruction event.
- Prop forces use the native direction, application-point offset and falloff, then the existing
  uproot/smash thresholds. Explosive smash effect 28 can queue another delayed blast. The separate
  prop-health path is recorded below.
- Queue processing no longer holds an explosion-vector iterator across blast callbacks. Newly queued
  prop blasts wait until the next tick before their first Process call; retained final states do not repeat
  damage. A vehicle killed by a blast enters the existing death hook on the next tick.

Synthetic tests cover scaled damage, reciprocal rounding, 3D distance, player reach/reductions, survivor and
fatal task priority, repeated hits on living knockdowns, occupant protection, parked-car wakeup, wreck forces,
vehicle chain reactions, retained-state expiry and 32 explosive props appending during one blast callback.
A local actual-city probe also observed a secondary car destroyed, a close pedestrian killed and a farther
pedestrian knocked down alive. Game and showcase screenshots rendered successfully. These probes establish
those cases, not world-wide or complete native parity.

The native world iterator's radius-plus-six cell rectangle is used for selection, but its 128-entity weak
pointer snapshot, class/list ordering, full immunity flags and source-entity reaction tasks are still absent.
Because the port processes explosions after vehicle integration, it consumes their vehicle impulse
immediately so the next Act cannot discard it through settling. That scheduling adaptation, and next-tick
vehicle chain birth, must remain labelled until the native scheduler is ported.

The preceding visual correction also follows `cEffectManager::Render`: additive fire/flash/cloud first,
then normal alpha smoke. Pixel tests cover both blend functions and pass order. The reference explosion
flash is yellow; the problem was ordinary-alpha blending making that light effect darken its background.

## Prop health and optional duplicate checks

The following continuation checks `cDynamicProp::Damage`, `SetPropDef`, `Smash`,
`Explosion::AddExplosion`, `EnableExplosionRaiusCheck` and `Reset` against the same reference:

- Generic type-0/type-4 blasts now subtract distance-based health from props after applying their physical
  force. Health is read as a signed byte. Partial damage persists per placement without swapping the model,
  removing static shapes or creating a second physical body. Lethal health uses the existing smash path,
  even when the object's physical-force thresholds both forbid a response. A smash already triggered by
  that blast's force does not run a second time through health damage.
- Prop kind bytes 3 and 4 supply the independent bullet/melee health gates. The health helper preserves
  full kind-2 damage despite its reduced smash-force strength, and kind-4's consume-all-health rule.
  Only blast kinds 6 and 7 are connected to live prop damage in this pass; firing, melee, fire contact and
  their extra physical-force paths must still be integrated. Full collision/immunity/source gates and the
  damage-overshoot argument used by specialised smash branches remain incomplete.
- Generic stationary blasts now share the optional native duplicate check. It is off after Reset and
  exposed as an internal game switch, not silently enabled for ordinary chains. When enabled, additions
  search the half-unit cell rectangle and compare raw three-dimensional squared distance to `0x45FFFFFF`.
  Pending and retained final-state blasts both suppress additions. Rejected additions do not shake the
  camera, and suppressing a vehicle blast does not cancel its occupant/death hook.

Synthetic regressions cover partial-health collision retention, lethal collision release, anchored health
destruction, repeated damage, signed/nonpositive health, the separate health gates, rejected smashes and
force/health double-smash prevention. Duplicate tests cover the default-off switch, scale-independent
rejection, exact horizontal/vertical distance boundaries, cell-edge selection, final-state expiry and
vehicle death processing. All 13 native and 20 Python tests pass. A local probe using an actual anchored
prop definition observed health 15 -> 6 -> 0 with placement retained until the lethal hit. The actual-city
vehicle/pedestrian chain probe still passes; game and showcase renders were also checked.

The port currently resets prop damage at its existing 250-unit cleanup distance; this is not the original
sector/entity lifetime. Optional suppression covers the implemented generic stationary types, not the
unported specialised-class exclusions, moving input or native explosion pool capacity. There is no mission
script VM yet to drive the switch. Native scheduling and ownership remain separate unfinished work.

## Still incomplete

- The remaining specialised smash branches, including spark and petrol-pump effects, and hotdog-cart gas
  jets for damage sources other than vehicle contact. An object reacting in a test does not prove its entire
  visual and sound sequence has been ported.
- The remaining explosion damage/force gates, prop weapon/fire damage integration and health ownership,
  native entity snapshot/order/limits, source reactions, specialised duplicate-check exclusions/pool limits,
  and full vehicle death-cause/chain timing parity.
  The current death hook applies the reference five panel probability
  rolls, but has not reproduced all native conditions that enter the explosive-destruction branch.
- The original shared vehicle-fragment pool and its reuse/eviction ordering. Fragments currently belong to
  their vehicle, so removing that vehicle also removes its fragments. Body damage beyond the supported
  panel nodes, auxiliary hatch geometry visibility and debris trails need further work.
- Hydrant stream collision, extinguishing and other water responses. No new claim is made about waterfront
  mesh clipping, buoyancy or world-wide placement accuracy.
- Native virtual-driver allocation and its damage gates. Drivers are currently actual pedestrians from
  traffic creation, which changes population accounting. Ordinary seat occupancy must not be mistaken for
  the original virtual-driver flag.
- Complete carjacking task/reaction selection and traffic resuming its route after a collision. A surviving
  driver currently flees after exiting. Seated 3D donor characters inside cars are not implemented; CTW
  sprites remain hidden inside closed vehicles.

Continue by comparing the remaining branches and their call sites against the reference, adding a
reproducing test where practical, and checking the result in-game. Keep provisional adaptations labelled
instead of marking their entire subsystem complete.
