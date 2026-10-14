/* SPDX-License-Identifier: GPL-3.0-only */
#include "enhancements.h"
#include <stdio.h>
#include <string.h>
#include <ctype.h>

static int enabled = 1, ready;
static float model_scale;
static CtwCharacterModel player_model, cop_model, male[6], female[6];
static const char* bike_clip(const CtwCharacterPose* pose, int action) {
    static const char* const clips[4][7] = {
        {"BIKEv_Ride","BIKEv_Still","BIKEv_pushes","BIKEv_jumponL","BIKEv_jumponR","BIKEv_getoffLHS","BIKEv_getoffRHS"},
        {"BIKEh_Ride","BIKEh_Still","BIKEh_pushes","BIKEh_jumponL","BIKEh_jumponR","BIKEh_getoffLHS","BIKEh_getoffRHS"},
        {"BIKEd_Ride","BIKEd_Still","BIKEd_pushes","BIKEd_jumponL","BIKEd_jumponR","BIKEd_getoffLHS","BIKEd_getoffRHS"},
        {"BIKEs_Ride","BIKEs_Still","BIKEs_pushes","BIKEs_jumponL","BIKEs_jumponR","BIKEs_getoffLHS","BIKEs_getoffRHS"}
    };
    CtwVehicleState vehicle = {0};
    int style = 0;
    vehicle.size = sizeof vehicle;
    if (pose->size >= sizeof *pose && pose->attachment_vehicle &&
        CTW_MOD_HAS(api,get_vehicle_state) && CTW_MOD_HAS(api,vehicle_name) &&
        api->get_vehicle_state(pose->attachment_vehicle,&vehicle)) {
        const char* source = api->vehicle_name(vehicle.model_id);
        char name[32]; int i=0;
        if (source) {
            for (; source[i] && i < (int)sizeof name-1; ++i) name[i]=(char)tolower((unsigned char)source[i]);
            name[i]=0;
            char* extension=strstr(name,".vehicle");
            if (extension && !extension[8]) *extension=0;
            /* Donor animation choices are mod policy. Unmapped bikes keep the scooter bank. */
            if (!strcmp(name,"angel") || !strcmp(name,"angel2") || !strcmp(name,"chopper") || !strcmp(name,"gang_angel")) style=1;
            else if (!strcmp(name,"sanchez")) style=2;
            else if (!strcmp(name,"pcj600") || !strcmp(name,"nrg900")) style=3;
        }
    }
    return clips[style][action];
}
static CtwCharacterModel load(const char* model, const char* txd) {
    char dff[2048], ifp[2048], textures[2048];
    const char* dir = api->mod_dir(self);
    if (snprintf(dff, sizeof dff, "%slcs/models/%s.dff", dir, model) >= (int)sizeof dff ||
        snprintf(ifp, sizeof ifp, "%slcs/ped.ifp", dir) >= (int)sizeof ifp ||
        snprintf(textures, sizeof textures, "%slcs/textures/%s", dir, txd) >= (int)sizeof textures)
        return 0;
    return api->load_character_model(dff, ifp, textures);
}
static int render(const CtwCharacterPose* pose, void* user) {
    const char* animation;
    int loop = 1;
    float phase;
    CtwCharacterModel model;
    (void)user;
    if (!pose || pose->size < offsetof(CtwCharacterPose, one_shot_seconds) ||
        !ready || !enabled || !enh_third_person_active())
        return 0;
    phase = pose->upper_phase;
    model = pose->flags & CTW_CHARACTER_PLAYER ? player_model
            : pose->ped_type == 10             ? cop_model
            : pose->body_set                   ? male[pose->id % 6]
                                               : female[pose->id % 6];
    if (!model)
        return 0;
    animation = pose->body_set ? "IDLE_stance" : "woman_idlestance";
    if (pose->flags & CTW_CHARACTER_RIDING) {
        animation = bike_clip(pose,pose->upper_animation == 0xa6 ? 1 : pose->upper_animation == 0xa4 ? 2 : 0);
    } else if ((pose->flags & CTW_CHARACTER_KNOCKED_DOWN) && pose->size >= sizeof *pose) {
        if (pose->reaction_state == 5 || pose->reaction_state == 1) {
            animation = pose->flags & CTW_CHARACTER_FALL_FORWARD ? "getup_front" : "getup";
            return api->draw_character_model_at_time(model, pose, model_scale, animation,
                                                     pose->recovery_seconds, 0, 1);
        }
        animation = pose->upper_animation == 0x1a || pose->upper_animation == 0x2c || pose->upper_animation == 0x32
                    ? "KO_skid_front" : "KO_skid_back";
        return api->draw_character_model_at_time(model, pose, model_scale, animation,
                                                 pose->reaction_seconds, 0, 1);
    } else if ((pose->flags & CTW_CHARACTER_DEAD) || pose->upper_animation == 0x18 || pose->upper_animation == 0x1a) {
        animation = pose->upper_animation == 0x1a ? "KO_skid_front" : "KO_skid_back";
        // CTW's fallen sprite has one frame, so its phase cannot drive a fall.
        // The game clock keeps advancing after that sprite has finished.
        if (pose->size < offsetof(CtwCharacterPose, one_shot_seconds) + sizeof(float))
            return 0;
        return api->draw_character_model_at_time(model, pose, model_scale, animation,
                                                  pose->one_shot_seconds, 0, 1);
    } else if (pose->upper_animation == 0x3c) {
        animation = "commando_roll";
        loop = 0;
    } else if (pose->flags & CTW_CHARACTER_ATTACHED) {
        loop = 0;
        switch (pose->upper_animation) {
        case 6:
        case 8:
            animation = pose->flags & CTW_CHARACTER_FLIPPED ? "CAR_open_RHS" : "CAR_open_LHS";
            break;
        case 0xc:
            animation = pose->flags & CTW_CHARACTER_FLIPPED ? "CAR_getin_RHS" : "CAR_getin_LHS";
            break;
        case 0xe:
            animation = pose->flags & CTW_CHARACTER_FLIPPED ? "CAR_getout_RHS" : "CAR_getout_LHS";
            break;
        case 0x5a:
            animation = bike_clip(pose,pose->flags & CTW_CHARACTER_FLIPPED ? 4 : 3);
            break;
        case 0x54:
            animation = bike_clip(pose,pose->flags & CTW_CHARACTER_FLIPPED ? 6 : 5);
            break;
        default:
            return 0; /* unsupported poses retain CTW's original animation */
        }
    } else if (pose->speed_level) {
        phase = pose->legs_phase;
        if (pose->speed_level == 1)
            animation = pose->flags & CTW_CHARACTER_PLAYER ? "WALK_player"
                        : pose->body_set                   ? "WALK_civi"
                                                           : "WOMAN_walknorm";
        else if (pose->speed_level == 2)
            animation = pose->flags & CTW_CHARACTER_PLAYER ? "run_player"
                        : pose->body_set                   ? "run_civi"
                                                           : "woman_run";
        else
            animation = "sprint_civi";
    }
    if (pose->flags & CTW_CHARACTER_RIDING) {
        if (!CTW_MOD_HAS(api, draw_character_model_attached) ||
            pose->size < offsetof(CtwCharacterPose, reaction_state) ||
            !(pose->flags & CTW_CHARACTER_ATTACHMENT_FRAME)) return 0;
        /* The donor pelvis (bone 1) follows CTW's native upper rider anchor.
           Its whole skeleton follows bike lean/pitch/sway, at the shared
           donor unit conversion; no world-Z or Angel-only contact offset. */
        return api->draw_character_model_attached(model, pose, model_scale, animation, phase, loop, 1);
    }
    if ((pose->flags & CTW_CHARACTER_ATTACHED) && (pose->flags & CTW_CHARACTER_ATTACHMENT_FRAME) &&
        pose->size >= offsetof(CtwCharacterPose, reaction_state) && CTW_MOD_HAS(api,draw_character_model_attached))
        return api->draw_character_model_attached(model, pose, model_scale, animation, phase, loop, -1);
    return api->draw_character_model(model, pose, model_scale, animation, phase, loop);
}
static void setup(void) {
    static const char* const men[] = {"male01", "male02", "male03", "b_man1", "worker1", "p_man1"};
    static const char* const women[] = {"female01", "female02", "female03", "b_wom1", "shopper1", "p_wom1"};
    char source[2048];
    FILE* file;
    if (!CTW_MOD_HAS(api, on_character_render) || !CTW_MOD_HAS(api, load_character_model) ||
        !CTW_MOD_HAS(api, draw_character_model) || !CTW_MOD_HAS(api, get_character_bounds) ||
        !CTW_MOD_HAS(api, draw_character_model_at_time)) {
        api->menu_add_label(self, "3D characters need a newer game");
        return;
    }
    snprintf(source, sizeof source, "%slcs/source.txt", api->mod_dir(self));
    file = fopen(source, "rb");
    if (!file) {
        api->menu_add_label(self, "Import an LCS 2.4.379 APK in the Porter for 3D people");
        return;
    }
    fclose(file);
    player_model = load("player", "player");
    cop_model = load("cop", "cop");
    for (int i = 0; i < 6; ++i) {
        male[i] = load(men[i], men[i]);
        female[i] = load(women[i], women[i]);
    }
    ready = player_model && cop_model;
    if (ready) {
        CtwCharacterBounds bounds = {0};
        bounds.size = sizeof bounds;
        ready = api->get_character_bounds(player_model, "IDLE_stance", 0, &bounds);
        if (ready) {
            const float width = bounds.maximum[0] - bounds.minimum[0];
            /* Mod fitting policy: a 1.25-unit standing shoulder width, then
               one donor conversion for the entire roster. The native dynamic
               sphere's diameter is 2.5; it is not a shoulder-width measurement.
               Keep donor proportions and differences between people. */
            ready = width > 0.1f && width < 4.f;
            if (ready) model_scale = 1.25f / width;
        }
    }
    if (!ready) {
        api->menu_add_label(self, "LCS character import is incomplete; using sprites");
        return;
    }
    for (int i = 0; i < 6; ++i) {
        if (!male[i])
            male[i] = male[0];
        if (!female[i])
            female[i] = female[0];
    }
    api->menu_add_toggle(self, "3D people in third person (LCS)", &enabled);
    enh_saved_int("lcs.characters", &enabled);
    api->on_character_render(self, render, 0);
}
const CtwFeature lcs_characters = {"Imported LCS characters", setup, 0, 0, 0, 0};
