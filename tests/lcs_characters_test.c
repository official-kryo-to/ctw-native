/* SPDX-License-Identifier: GPL-3.0-only */
#include "../ctw-modkit/examples/enhancements/lcs_characters.c"
#include <math.h>

const CtwApi* api;
CtwMod* self;
static int active = 1, failures, calls;
static float drawn_scale, drawn_time;
static const char* drawn_clip;
static int drawn_loop, drawn_ground;
static int drawn_bone;
static const char* vehicle_model="angel.vehicle";
static int vehicle_state(CtwVehicle vehicle,CtwVehicleState* state) {
    if (vehicle != 42) return 0;
    state->model_id=3; return 1;
}
static const char* vehicle_name(int id) { return id==3 ? vehicle_model : NULL; }
int enh_third_person_active(void) { return active; }
void enh_saved_int(const char* key, int* value) { (void)key; (void)value; }
static void check(int ok, const char* why) {
    if (!ok) { fprintf(stderr, "FAIL: %s\n", why); ++failures; }
}
static int draw(CtwCharacterModel model, const CtwCharacterPose* pose, float scale,
                const char* clip, float time, int loop, int ground) {
    (void)pose; ++calls; drawn_scale=scale; drawn_time=time;
    drawn_clip=clip; drawn_loop=loop; drawn_ground=ground;
    return model != 0;
}
static int phased(CtwCharacterModel model, const CtwCharacterPose* pose, float scale,
                  const char* clip, float phase, int loop) {
    return draw(model,pose,scale,clip,phase,loop,0);
}
static int attached(CtwCharacterModel model, const CtwCharacterPose* pose, float scale,
                    const char* clip, float phase, int loop, int bone) {
    drawn_bone=bone;
    return draw(model,pose,scale,clip,phase,loop,0);
}
int main(void) {
    CtwApi host = {0};
    CtwCharacterPose pose = {0};
    host.size=sizeof host; host.version=9;
    host.draw_character_model=phased; host.draw_character_model_at_time=draw;
    host.draw_character_model_attached=attached;
    host.get_vehicle_state=vehicle_state; host.vehicle_name=vehicle_name;
    api=&host; ready=enabled=1; player_model=cop_model=1; model_scale=1.77f;
    pose.size=sizeof pose; pose.flags=CTW_CHARACTER_PLAYER; pose.body_set=1;
    check(render(&pose,0) && !strcmp(drawn_clip,"IDLE_stance") && drawn_scale==model_scale,
          "standing people use the calibrated donor conversion");
    pose.flags |= CTW_CHARACTER_DEAD; pose.upper_animation=0x18;
    pose.upper_phase=0; pose.one_shot_seconds=.4f;
    check(render(&pose,0) && !strcmp(drawn_clip,"KO_skid_back") && drawn_time==.4f &&
          !drawn_loop && drawn_ground, "one-frame fallen sprites play a timed grounded fall");
    pose.one_shot_seconds=2;
    check(render(&pose,0) && drawn_time==2, "fall time continues while the sprite phase stays zero");
    pose.flags=CTW_CHARACTER_KNOCKED_DOWN|CTW_CHARACTER_PLAYER;
    pose.reaction_state=14;pose.upper_animation=0x32;pose.reaction_seconds=.3f;
    check(render(&pose,0) && !strcmp(drawn_clip,"KO_skid_front") && drawn_time==.3f,
          "impact direction selects the front donor fall");
    pose.reaction_state=2;pose.upper_animation=0x1a;pose.one_shot_seconds=0;pose.reaction_seconds=.9f;
    check(render(&pose,0) && drawn_time==.9f,"entering CTW's fallen state does not restart the imported fall");
    pose.reaction_state=5;pose.upper_animation=0x1e;pose.recovery_seconds=.2f;pose.flags|=CTW_CHARACTER_FALL_FORWARD;
    check(render(&pose,0) && !strcmp(drawn_clip,"getup_front") && drawn_time==.2f,
          "living knockdowns use the recovery clip instead of being marked dead");
    pose.reaction_state=1;pose.upper_animation=0x12;pose.one_shot_seconds=0;pose.recovery_seconds=.4f;
    check(render(&pose,0) && !strcmp(drawn_clip,"getup_front") && drawn_time==.4f,
          "the final recovery sprite does not replay getup from the ground");
    pose.flags=CTW_CHARACTER_DEAD;pose.upper_animation=0x18;
    pose.size=offsetof(CtwCharacterPose,one_shot_seconds);
    check(!render(&pose,0), "a legacy pose cannot read the new clock tail");
    pose.size=sizeof pose; pose.flags=CTW_CHARACTER_PLAYER|CTW_CHARACTER_ATTACHED;
    pose.upper_animation=0x77;
    check(!render(&pose,0), "unsupported attached animations retain CTW sprites");
    pose.flags|=CTW_CHARACTER_ATTACHMENT_FRAME; pose.upper_animation=0xc;
    check(render(&pose,0) && !strcmp(drawn_clip,"CAR_getin_LHS") && drawn_bone==-1,
          "car entry uses the vehicle frame with a floor anchor and preserves clip height");
    pose.flags=CTW_CHARACTER_PLAYER|CTW_CHARACTER_RIDING|CTW_CHARACTER_ATTACHMENT_FRAME; pose.upper_animation=0xa6;
    check(render(&pose,0) && !strcmp(drawn_clip,"BIKEv_Still") && drawn_scale==model_scale &&
          !drawn_ground && drawn_bone==1, "riders anchor their animated pelvis in the native frame");
    pose.attachment_vehicle=42;
    check(render(&pose,0) && !strcmp(drawn_clip,"BIKEh_Still"),"Angel uses the donor chopper pose instead of a scooter pose");
    vehicle_model="Sanchez.vehicle"; pose.upper_animation=0xa4;
    check(render(&pose,0) && !strcmp(drawn_clip,"BIKEd_pushes"),"dirt-bike reverse uses its matching bank");
    vehicle_model="NRG900.vehicle";pose.flags=CTW_CHARACTER_PLAYER|CTW_CHARACTER_ATTACHED|CTW_CHARACTER_ATTACHMENT_FRAME|CTW_CHARACTER_FLIPPED;
    pose.upper_animation=0x5a;
    check(render(&pose,0) && !strcmp(drawn_clip,"BIKEs_jumponR"),"sport-bike mounting retains the selected side");
    vehicle_model="Faggio";pose.upper_animation=0x54;
    check(render(&pose,0) && !strcmp(drawn_clip,"BIKEv_getoffRHS"),"scooter dismount uses the scooter bank");
    pose.flags=CTW_CHARACTER_PLAYER|CTW_CHARACTER_RIDING|CTW_CHARACTER_ATTACHMENT_FRAME;
    pose.size=offsetof(CtwCharacterPose,one_shot_seconds);
    check(!render(&pose,0), "legacy riders retain CTW sprites without a valid attachment frame");
    check(!render(NULL,0), "invalid render poses fail safely");
    active=0; calls=0;
    check(!render(&pose,0) && calls==0, "the original camera keeps the original sprites");
    return failures ? 1 : 0;
}
