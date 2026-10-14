/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kryo.to
 * See LICENSE in the repository root. */
/* Exercise the actual camera feature against a synthetic host, including the older SDK prefix. */
#include "../ctw-modkit/examples/enhancements/third_person.c"
#include <stdio.h>
#include <string.h>

const CtwApi* api;
CtwMod* self;
void enh_saved_int(const char* key, int* value) { (void)key; (void)value; }
void enh_saved_float(const char* key, float* value) { (void)key; (void)value; }
static float shown[3], applied[3] = {0.3f,-0.4f,0.5f};
static int shake_calls, camera_on, dead;
static int failures;
static void check(int ok, const char* why) { if (!ok) { fprintf(stderr,"FAIL: %s\n",why); ++failures; } }
static int player_state(CtwPlayerState* p) { p->flags = dead ? CTW_PLAYER_DEAD : 0; return 1; }
static void camera(float* eye, float* y, float* p) { memset(eye,0,3*sizeof(float)); *y=0; *p=-60; }
static void free_camera(int on, const float* eye, float y, float p) {
    (void)y; (void)p; camera_on=on; if (eye) memcpy(shown,eye,sizeof shown);
}
static void control(int on, float y) { (void)on; (void)y; }
static void int_noop(int v) { (void)v; }
static void float_noop(float v) { (void)v; }
static int zero(void) { return 0; }
static int key_state(int key) { (void)key; return 0; }
static float zero_float(void) { return 0; }
static float render_distance(void) { return 120; }
static void mouse(float* x, float* y) { *x=*y=0; }
static int line(const float* a, const float* b, float* f) { (void)a; (void)b; *f=1; return 0; }
static int ground(const float* p, CtwGround* g) { (void)p; (void)g; return 0; }
static void shake(float* out) { ++shake_calls; memcpy(out,applied,sizeof applied); }

int main(void) {
    CtwApi host={0}; host.version=6; host.size=sizeof host;
    host.get_player_state=player_state; host.get_camera=camera; host.set_free_camera=free_camera;
    host.set_control_yaw=control; host.set_camera_fov=float_noop; host.set_mouse_captured=int_noop;
    host.get_render_distance=render_distance; host.set_render_distance=float_noop;
    host.menu_is_open=zero; host.key_down=key_state; host.mouse_delta=mouse; host.mouse_wheel=zero_float;
    host.world_line=line; host.get_ground=ground; host.get_camera_shake=shake;
    api=&host; available=enabled=1;
    host.version=5; host.size=offsetof(CtwApi,get_camera_shake);
    third_person.frame(1.f/60); float baseline[3]; memcpy(baseline,shown,sizeof baseline);
    check(camera_on && !shake_calls,"third person still works with the v5 prefix");
    third_person.stop(); host.version=6; host.size=sizeof host;
    third_person.frame(1.f/60);
    for (int i=0;i<3;++i) check(fabsf(shown[i]-baseline[i]-applied[i])<0.00001f,"third person adds the host shake once");
    third_person.frame(0);
    for (int i=0;i<3;++i) check(fabsf(shown[i]-baseline[i]-applied[i])<0.00001f,"repeated draws do not accumulate shake");
    check(shake_calls==2,"one shake read per drawn frame");
    dead=1; third_person.frame(1.f/60);
    check(!camera_on && shake_calls==2,"death releases the custom camera without reading shake");
    return failures ? 1 : 0;
}
