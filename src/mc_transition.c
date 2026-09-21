#include "mc_transition.h"
#include <string.h>

/* Shared interpolation for elapsed e (ms beyond delay, clamped caller-side).
 * Fold Q16.16 easing output to integer pixels with 64-bit intermediates. */
static void tr_interpolate(mc_transition_t *t, uint32_t e)
{
    /* 64-bit intermediate: e * MC_FP_SCALE overflows int32 for e >= 32768 */
    mc_real_t tt = (mc_real_t)(((int64_t)e * MC_FP_SCALE) / t->duration);
    mc_real_t eased = t->path(tt);
    int64_t span = (int64_t)t->end_value - (int64_t)t->start_value;
    t->current_value = t->start_value + (int32_t)((span * eased) / MC_FP_SCALE);
}

void mc_transition_init(mc_transition_t *t)
{
    memset(t, 0, sizeof(*t));
    t->duration = 1000;
    t->path = mc_ease_quad_out;
    t->is_paused = true;
    t->is_finish = true;
}

void mc_transition_reset(mc_transition_t *t)
{
    t->is_paused = true;
    t->is_finish = false;
    t->current_value = t->start_value;
    t->time_offset = 0;
}

void mc_transition_jump(mc_transition_t *t, int start, int end)
{
    t->start_value = start;
    t->end_value = end;
    t->is_paused = true;
    t->is_finish = true;
    t->current_value = end;
}

void mc_transition_update(mc_transition_t *t, uint32_t now_ms)
{
    if (t->is_paused || t->is_finish) return;
    uint32_t delta_time = now_ms - t->time_offset;
    if (delta_time < t->delay) {
        t->current_value = t->start_value;
        return;
    }
    uint32_t e = delta_time - t->delay;
    /* duration==0 (or e==0 with duration==0) must not divide; land on end */
    if (t->duration == 0 || e > t->duration) {
        t->current_value = t->end_value;
        t->is_finish = true;
        return;
    }
    tr_interpolate(t, e);
}

void mc_transition_set_duration(mc_transition_t *t, uint32_t ms) { t->duration = ms; }
void mc_transition_set_delay(mc_transition_t *t, uint32_t ms) { t->delay = ms; }
void mc_transition_set_path(mc_transition_t *t, mc_easing_fn_t path) { t->path = path; }

// ---------------------------------------------------------------------------
// 2D transition
// ---------------------------------------------------------------------------

void mc_transition2d_init(mc_transition2d_t *tr)
{
    mc_transition_init(&tr->x);
    mc_transition_init(&tr->y);
    tr->is_changed = false;
    tr->update_callback = NULL;
    tr->user_data = NULL;
}

void mc_transition2d_set_duration(mc_transition2d_t *tr, uint32_t ms)
{
    tr->x.duration = ms;
    tr->y.duration = ms;
}

void mc_transition2d_set_delay(mc_transition2d_t *tr, uint32_t ms)
{
    tr->x.delay = ms;
    tr->y.delay = ms;
}

void mc_transition2d_set_path(mc_transition2d_t *tr, mc_easing_fn_t path)
{
    tr->x.path = path;
    tr->y.path = path;
}

void mc_transition2d_set_update_callback(mc_transition2d_t *tr,
        void (*cb)(mc_transition2d_t *tr, void *user_data), void *user_data)
{
    tr->update_callback = cb;
    tr->user_data = user_data;
}

void mc_transition2d_update(mc_transition2d_t *tr, uint32_t now_ms)
{
    if (tr->is_changed) {
        tr->is_changed = false;
        tr->x.time_offset = now_ms;
        tr->y.time_offset = now_ms;
        tr->x.is_paused = false;
        tr->y.is_paused = false;
    }
    mc_transition_update(&tr->x, now_ms);
    mc_transition_update(&tr->y, now_ms);
    if (tr->update_callback) tr->update_callback(tr, tr->user_data);
}

void mc_transition2d_jump_to(mc_transition2d_t *tr, int x, int y)
{
    mc_transition_jump(&tr->x, tr->x.current_value, x);
    mc_transition_jump(&tr->y, tr->y.current_value, y);
}

void mc_transition2d_move_to(mc_transition2d_t *tr, int x, int y)
{
    if (x == tr->x.end_value && y == tr->y.end_value) return;
    mc_transition_jump(&tr->x, tr->x.current_value, x);
    mc_transition_jump(&tr->y, tr->y.current_value, y);
    mc_transition_reset(&tr->x);
    mc_transition_reset(&tr->y);
    tr->is_changed = true;
}

int  mc_transition2d_x(const mc_transition2d_t *tr) { return tr->x.current_value; }
int  mc_transition2d_y(const mc_transition2d_t *tr) { return tr->y.current_value; }
int  mc_transition2d_end_x(const mc_transition2d_t *tr) { return tr->x.end_value; }
int  mc_transition2d_end_y(const mc_transition2d_t *tr) { return tr->y.end_value; }
bool mc_transition2d_is_finish(const mc_transition2d_t *tr)
{
    return tr->x.is_finish && tr->y.is_finish;
}

// ---------------------------------------------------------------------------
// 3D transition — deliberately differs from 2D (spec contract #7):
// no is_changed; move_to unpauses with time_offset = 0; completion uses >=.
// ---------------------------------------------------------------------------

static void tr3d_axis_update(mc_transition_t *a, uint32_t now_ms)
{
    if (a->is_paused || a->is_finish) return;
    uint32_t delta_time = now_ms - a->time_offset;
    if (delta_time < a->delay) {
        a->current_value = a->start_value;
        return;
    }
    uint32_t e = delta_time - a->delay;
    if (a->duration == 0 || e >= a->duration) {   /* >= : exact boundary finishes */
        a->current_value = a->end_value;
        a->is_finish = true;
        return;
    }
    tr_interpolate(a, e);
}

void mc_transition3d_init(mc_transition3d_t *t)
{
    mc_transition_init(&t->x);
    mc_transition_init(&t->y);
    mc_transition_init(&t->z);
}

void mc_transition3d_set_duration(mc_transition3d_t *t, uint32_t ms)
{
    t->x.duration = t->y.duration = t->z.duration = ms;
}

void mc_transition3d_set_delay(mc_transition3d_t *t, uint32_t ms)
{
    t->x.delay = t->y.delay = t->z.delay = ms;
}

void mc_transition3d_set_each_delay(mc_transition3d_t *t, uint32_t dx, uint32_t dy,
                                    uint32_t dz)
{
    t->x.delay = dx;
    t->y.delay = dy;
    t->z.delay = dz;
}

void mc_transition3d_set_path(mc_transition3d_t *t, mc_easing_fn_t path)
{
    t->x.path = t->y.path = t->z.path = path;
}

void mc_transition3d_update(mc_transition3d_t *t, uint32_t now_ms)
{
    tr3d_axis_update(&t->x, now_ms);
    tr3d_axis_update(&t->y, now_ms);
    tr3d_axis_update(&t->z, now_ms);
}

void mc_transition3d_jump_to(mc_transition3d_t *t, int x, int y, int z)
{
    mc_transition_jump(&t->x, t->x.current_value, x);
    mc_transition_jump(&t->y, t->y.current_value, y);
    mc_transition_jump(&t->z, t->z.current_value, z);
}

void mc_transition3d_move_to(mc_transition3d_t *t, int x, int y, int z)
{
    if (x == t->x.end_value && y == t->y.end_value && z == t->z.end_value) return;
    mc_transition_jump(&t->x, t->x.current_value, x);
    mc_transition_jump(&t->y, t->y.current_value, y);
    mc_transition_jump(&t->z, t->z.current_value, z);
    mc_transition_reset(&t->x);   /* is_finish=false, time_offset=0, paused */
    mc_transition_reset(&t->y);
    mc_transition_reset(&t->z);
    t->x.is_paused = false;       /* start immediately (contract #7) */
    t->y.is_paused = false;
    t->z.is_paused = false;
}

int  mc_transition3d_x(const mc_transition3d_t *t) { return t->x.current_value; }
int  mc_transition3d_y(const mc_transition3d_t *t) { return t->y.current_value; }
int  mc_transition3d_z(const mc_transition3d_t *t) { return t->z.current_value; }
bool mc_transition3d_is_finish(const mc_transition3d_t *t)
{
    return t->x.is_finish && t->y.is_finish && t->z.is_finish;
}
