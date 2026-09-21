#ifndef MC_TRANSITION_H
#define MC_TRANSITION_H

#include <stdint.h>
#include <stdbool.h>
#include "mc_types.h"
#include "mc_easing.h"

/* Smooth UI Toolkit Transition semantics: absolute-time (update(now_ms)),
 * pull-style. See docs/superpowers/specs/2026-09-21-mc-transition-design.md
 * for the normative 10-point contract. Sibling of mc_animate (dt/push):
 * use mc_transition for immediate-mode UI element animation, mc_animate for
 * tween sequences and springs. */

/* 1D. Values are int32_t pixel-domain; easing runs in mc_real_t. */
typedef struct {
    int32_t start_value, end_value, current_value;
    uint32_t duration, delay;      /* ms; both are persistent config */
    uint32_t time_offset;          /* clock anchor, set at deferred start */
    mc_easing_fn_t path;
    bool is_paused, is_finish;
} mc_transition_t;

void mc_transition_init(mc_transition_t *t);   /* dur=1000, quad_out, paused+finish */
void mc_transition_reset(mc_transition_t *t);  /* back to start, armed not started */
void mc_transition_jump(mc_transition_t *t, int start, int end); /* teleport */
void mc_transition_update(mc_transition_t *t, uint32_t now_ms);
void mc_transition_set_duration(mc_transition_t *t, uint32_t ms);
void mc_transition_set_delay(mc_transition_t *t, uint32_t ms);
void mc_transition_set_path(mc_transition_t *t, mc_easing_fn_t path);

/* 2D. is_changed = deferred-start flag, lives here (1D is driven directly). */
typedef struct mc_transition2d {
    mc_transition_t x, y;
    bool is_changed;
    void (*update_callback)(struct mc_transition2d *tr, void *user_data);
    void *user_data;
} mc_transition2d_t;

void mc_transition2d_init(mc_transition2d_t *tr);
void mc_transition2d_set_duration(mc_transition2d_t *tr, uint32_t ms);
void mc_transition2d_set_delay(mc_transition2d_t *tr, uint32_t ms);
void mc_transition2d_set_path(mc_transition2d_t *tr, mc_easing_fn_t path);
void mc_transition2d_set_update_callback(mc_transition2d_t *tr,
        void (*cb)(mc_transition2d_t *tr, void *user_data), void *user_data);
void mc_transition2d_update(mc_transition2d_t *tr, uint32_t now_ms);
void mc_transition2d_jump_to(mc_transition2d_t *tr, int x, int y);
void mc_transition2d_move_to(mc_transition2d_t *tr, int x, int y);
int  mc_transition2d_x(const mc_transition2d_t *tr);
int  mc_transition2d_y(const mc_transition2d_t *tr);
int  mc_transition2d_end_x(const mc_transition2d_t *tr);
int  mc_transition2d_end_y(const mc_transition2d_t *tr);
bool mc_transition2d_is_finish(const mc_transition2d_t *tr);

/* 3D. Deliberately differs from 2D (contract #7): no is_changed — move_to
 * unpauses immediately with the clock anchored at time_offset = 0, and the
 * completion test is >= (finish exactly at delay + duration). */
typedef struct {
    mc_transition_t x, y, z;
} mc_transition3d_t;

void mc_transition3d_init(mc_transition3d_t *t);
void mc_transition3d_set_duration(mc_transition3d_t *t, uint32_t ms);
void mc_transition3d_set_delay(mc_transition3d_t *t, uint32_t ms);
void mc_transition3d_set_each_delay(mc_transition3d_t *t, uint32_t dx, uint32_t dy,
                                    uint32_t dz);
void mc_transition3d_set_path(mc_transition3d_t *t, mc_easing_fn_t path);
void mc_transition3d_update(mc_transition3d_t *t, uint32_t now_ms);
void mc_transition3d_jump_to(mc_transition3d_t *t, int x, int y, int z);
void mc_transition3d_move_to(mc_transition3d_t *t, int x, int y, int z);
int  mc_transition3d_x(const mc_transition3d_t *t);
int  mc_transition3d_y(const mc_transition3d_t *t);
int  mc_transition3d_z(const mc_transition3d_t *t);
bool mc_transition3d_is_finish(const mc_transition3d_t *t);

/* Smooth color (SmoothRGB equivalent). Three 1D channels under one clock,
 * RGB888 in and out. Carries is_changed like 2D (contract #9). */
typedef struct {
    mc_transition_t r, g, b;
    bool is_changed;
} mc_color_trans_t;

void mc_color_trans_init(mc_color_trans_t *ct);
void mc_color_trans_set_duration(mc_color_trans_t *ct, uint32_t ms);
void mc_color_trans_jump_to(mc_color_trans_t *ct, uint32_t rgb888);
void mc_color_trans_move_to(mc_color_trans_t *ct, uint32_t rgb888);
uint32_t mc_color_trans_value(const mc_color_trans_t *ct);
void mc_color_trans_update(mc_color_trans_t *ct, uint32_t now_ms);

#endif
