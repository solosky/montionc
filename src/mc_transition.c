#include "mc_transition.h"
#include <string.h>

/* Shared interpolation for elapsed e (ms beyond delay, clamped caller-side).
 * Fold Q16.16 easing output to integer pixels with 64-bit intermediates. */
static void tr_interpolate(mc_transition_t *t, uint32_t e)
{
    mc_real_t tt = MC_REAL_FROM_INT(e) / t->duration;   /* [0,1] */
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
