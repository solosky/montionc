#include <stdio.h>
#include "mc_transition.h"

static int tests_run = 0, tests_passed = 0;
#define TEST(n) do { tests_run++; printf("  %s ... ", n); } while(0)
#define PASS() do { tests_passed++; printf("PASS\n"); } while(0)
#define CHECK(c) do { if (!(c)) { printf("FAIL at %d\n", __LINE__); return 1; } } while(0)
/* interior easing samples: tol is in output units (permille-of-travel × span/1000) */
#define CHECK_NEAR(v, expect, tol) do { \
    int64_t d_ = (int64_t)(v) - (int64_t)(expect); \
    if (d_ < 0) d_ = -d_; \
    if (d_ > (tol)) { printf("FAIL at %d: %d vs %d\n", __LINE__, (int)(v), (int)(expect)); return 1; } \
} while(0)

/* 1D has no move_to (2D/3D/color own the move semantics); direct 1D users arm
 * the clock manually. This helper mirrors what 2D move_to + is_changed does. */
static void arm(mc_transition_t *t, uint32_t anchor)
{
    mc_transition_reset(t);        /* current=start, finish=false, offset=0 */
    t->is_paused = false;
    t->time_offset = anchor;
}

static int test_1d_init_defaults(void) {
    TEST("1D init defaults: dur=1000 quad_out paused+finish");
    mc_transition_t t;
    mc_transition_init(&t);
    CHECK(t.duration == 1000);
    CHECK(t.path == mc_ease_quad_out);
    CHECK(t.is_paused == true);
    CHECK(t.is_finish == true);
    CHECK(t.current_value == 0);
    PASS(); return 0;
}

static int test_1d_jump(void) {
    TEST("1D jump teleports and finishes");
    mc_transition_t t;
    mc_transition_init(&t);
    mc_transition_jump(&t, 10, 20);
    CHECK(t.start_value == 10 && t.end_value == 20);
    CHECK(t.current_value == 20);
    CHECK(t.is_finish == true);
    PASS(); return 0;
}

static int test_1d_linear_timeline(void) {
    TEST("1D linear 10->110 dur=100: exact endpoints");
    mc_transition_t t;
    mc_transition_init(&t);
    mc_transition_set_duration(&t, 100);
    mc_transition_set_path(&t, mc_ease_linear);
    mc_transition_jump(&t, 10, 110);
    arm(&t, 1000);
    mc_transition_update(&t, 1000);       /* t=0 */
    CHECK(t.current_value == 10);
    mc_transition_update(&t, 1050);
    CHECK(t.current_value == 60);         /* exact: linear midpoint */
    mc_transition_update(&t, 1101);       /* 1D uses strict >: 101 > 100 */
    CHECK(t.current_value == 110);
    CHECK(t.is_finish == true);
    PASS(); return 0;
}

static int test_1d_back_overshoot(void) {
    TEST("1D back_out interior sample within ±5 permille of travel");
    mc_transition_t t;
    mc_transition_init(&t);
    mc_transition_set_duration(&t, 1000);
    mc_transition_set_path(&t, mc_ease_back_out);
    mc_transition_jump(&t, 0, 1000);
    arm(&t, 0);
    mc_transition_update(&t, 500);        /* t=0.5 → back_out ≈ 1.088 */
    /* Q16.16 mc_ease_back_out vs permille reference: max dev 4 permille */
    CHECK_NEAR(t.current_value, 1088, 5);
    mc_transition_update(&t, 1000);
    CHECK(t.current_value == 1000);       /* endpoint exact */
    PASS(); return 0;
}

static int test_1d_delay_persistent(void) {
    TEST("1D delay is config, not consumed; holds start value");
    mc_transition_t t;
    mc_transition_init(&t);
    mc_transition_set_duration(&t, 100);
    mc_transition_set_delay(&t, 200);
    mc_transition_jump(&t, 0, 100);
    /* first run: anchored at 1000, holds until 1200 */
    arm(&t, 1000);
    mc_transition_update(&t, 1100);
    CHECK(t.current_value == 0);
    /* second run reuses the same delay config (not consumed) */
    mc_transition_jump(&t, 100, 200);
    arm(&t, 1000);
    mc_transition_update(&t, 1100);
    CHECK(t.current_value == 100);        /* still inside second delay window */
    mc_transition_update(&t, 1301);
    CHECK(t.current_value == 200);
    PASS(); return 0;
}

static int test_1d_reset(void) {
    TEST("1D reset returns to start, armed-not-started");
    mc_transition_t t;
    mc_transition_init(&t);
    mc_transition_set_duration(&t, 100);
    mc_transition_set_path(&t, mc_ease_linear);
    mc_transition_jump(&t, 0, 50);
    mc_transition_reset(&t);
    CHECK(t.current_value == t.start_value);
    CHECK(t.time_offset == 0);
    CHECK(t.is_paused == true);
    CHECK(t.is_finish == false);
    mc_transition_update(&t, 5000);       /* paused: update must not move it */
    CHECK(t.current_value == t.start_value);
    t.is_paused = false;                  /* armed at offset 0 */
    mc_transition_update(&t, 50);
    CHECK(t.current_value == 25);         /* linear midpoint of 0..50 */
    PASS(); return 0;
}

static int test_1d_zero_duration(void) {
    TEST("1D duration=0 lands on end without div-by-zero");
    mc_transition_t t;
    mc_transition_init(&t);
    mc_transition_set_duration(&t, 0);
    mc_transition_jump(&t, 0, 42);
    arm(&t, 0);
    mc_transition_update(&t, 1);
    CHECK(t.current_value == 42 && t.is_finish == true);
    arm(&t, 0);
    mc_transition_update(&t, 0);          /* e == 0 exactly: also guarded */
    CHECK(t.current_value == 42 && t.is_finish == true);
    PASS(); return 0;
}

static int test_1d_long_duration_no_overflow(void) {
    TEST("1D long duration (e > 65536) scales time without overflow");
    mc_transition_t t;
    mc_transition_init(&t);
    mc_transition_set_duration(&t, 100000);
    mc_transition_set_path(&t, mc_ease_linear);
    mc_transition_jump(&t, 0, 100000);
    arm(&t, 0);
    mc_transition_update(&t, 70000);
    CHECK_NEAR(t.current_value, 70000, 2);   /* ≈69999; ~4463 with the overflow */
    PASS(); return 0;
}

static int test_1d_pure_function_of_time(void) {
    TEST("1D update is a pure function of now_ms (no per-frame accumulation)");
    mc_transition_t t;
    mc_transition_init(&t);
    mc_transition_set_duration(&t, 100);
    mc_transition_set_path(&t, mc_ease_linear);
    mc_transition_jump(&t, 0, 100);
    arm(&t, 1000);
    mc_transition_update(&t, 1050);
    int32_t once = t.current_value;
    mc_transition_update(&t, 1050);   /* same timestamp: same value */
    CHECK(t.current_value == once);
    mc_transition_update(&t, 1040);   /* earlier timestamp: recomputes, never accumulates */
    CHECK(t.current_value < once);
    PASS(); return 0;
}

static int g_cb_calls = 0;
static void on_update_cb(mc_transition2d_t *tr, void *ud) { (void)tr; (void)ud; g_cb_calls++; }

static int test_2d_jump_and_finish(void) {
    TEST("2D jump = no transition, is_finish = AND of axes");
    mc_transition2d_t tr;
    mc_transition2d_init(&tr);
    mc_transition2d_jump_to(&tr, 10, 20);
    CHECK(mc_transition2d_x(&tr) == 10 && mc_transition2d_y(&tr) == 20);
    CHECK(mc_transition2d_is_finish(&tr) == true);
    PASS(); return 0;
}

static int test_2d_deferred_start_timeline(void) {
    TEST("2D move+update dur=100: anchored at first update, strict > completion");
    mc_transition2d_t tr;
    mc_transition2d_init(&tr);
    mc_transition2d_set_duration(&tr, 100);
    mc_transition2d_set_path(&tr, mc_ease_quad_out);
    mc_transition2d_move_to(&tr, 110, 20);       /* only endpoints + is_changed */
    CHECK(mc_transition2d_x(&tr) == 0);          /* clock not started yet */
    CHECK(mc_transition2d_is_finish(&tr) == false);
    mc_transition2d_update(&tr, 1000);           /* anchors here */
    CHECK(mc_transition2d_x(&tr) == 0);          /* t=0, quad_out(0)=0 */
    mc_transition2d_update(&tr, 1101);           /* 101 > 100 → done */
    CHECK(mc_transition2d_x(&tr) == 110 && mc_transition2d_y(&tr) == 20);
    CHECK(mc_transition2d_is_finish(&tr) == true);
    PASS(); return 0;
}

static int test_2d_delay_holds_start(void) {
    TEST("2D delay holds start value; config persists across moves");
    mc_transition2d_t tr;
    mc_transition2d_init(&tr);
    mc_transition2d_set_duration(&tr, 100);
    mc_transition2d_set_delay(&tr, 200);
    mc_transition2d_jump_to(&tr, 0, 0);
    mc_transition2d_move_to(&tr, 110, 20);
    mc_transition2d_update(&tr, 1000);           /* anchor */
    mc_transition2d_update(&tr, 1101);           /* inside 200ms delay */
    CHECK(mc_transition2d_x(&tr) == 0);
    mc_transition2d_update(&tr, 1301);           /* past delay+duration */
    CHECK(mc_transition2d_x(&tr) == 110);
    /* second move reuses delay config */
    mc_transition2d_move_to(&tr, 210, 20);
    mc_transition2d_update(&tr, 1301);           /* re-anchored: inside delay again */
    CHECK(mc_transition2d_x(&tr) == 110);
    PASS(); return 0;
}

static int test_2d_noop_retarget(void) {
    TEST("2D move to current target is a no-op (no clock restart)");
    mc_transition2d_t tr;
    mc_transition2d_init(&tr);
    mc_transition2d_set_duration(&tr, 100);
    mc_transition2d_move_to(&tr, 210, 20);
    mc_transition2d_update(&tr, 1000);           /* anchor */
    mc_transition2d_move_to(&tr, 210, 20);       /* same target: must not re-anchor */
    mc_transition2d_update(&tr, 1101);
    CHECK(mc_transition2d_x(&tr) == 210);        /* finished on the original clock */
    PASS(); return 0;
}

static int test_2d_update_callback(void) {
    TEST("2D update_callback fires once per update");
    mc_transition2d_t tr;
    mc_transition2d_init(&tr);
    mc_transition2d_set_duration(&tr, 100);
    mc_transition2d_set_update_callback(&tr, on_update_cb, NULL);
    mc_transition2d_move_to(&tr, 10, 0);
    mc_transition2d_update(&tr, 100);
    mc_transition2d_update(&tr, 200);
    CHECK(g_cb_calls == 2);
    PASS(); return 0;
}

static int test_2d_per_axis_jump(void) {
    TEST("2D jump_to uses each axis current as its own start");
    mc_transition2d_t tr;
    mc_transition2d_init(&tr);
    mc_transition2d_jump_to(&tr, 5, 7);          /* current 0,0 → start 0,0 */
    CHECK(mc_transition2d_x(&tr) == 5 && mc_transition2d_y(&tr) == 7);
    CHECK(mc_transition2d_end_x(&tr) == 5 && mc_transition2d_end_y(&tr) == 7);
    mc_transition2d_jump_to(&tr, 9, 11);         /* current 5,7 → start 5,7 */
    CHECK(tr.x.start_value == 5 && tr.y.start_value == 7);
    CHECK(mc_transition2d_x(&tr) == 9 && mc_transition2d_y(&tr) == 11);
    PASS(); return 0;
}

int main(void) {
    printf("test_transition:\n");
    if (test_1d_init_defaults()) return 1;
    if (test_1d_jump()) return 1;
    if (test_1d_linear_timeline()) return 1;
    if (test_1d_back_overshoot()) return 1;
    if (test_1d_delay_persistent()) return 1;
    if (test_1d_reset()) return 1;
    if (test_1d_zero_duration()) return 1;
    if (test_1d_long_duration_no_overflow()) return 1;
    if (test_1d_pure_function_of_time()) return 1;
    if (test_2d_jump_and_finish()) return 1;
    if (test_2d_deferred_start_timeline()) return 1;
    if (test_2d_delay_holds_start()) return 1;
    if (test_2d_noop_retarget()) return 1;
    if (test_2d_update_callback()) return 1;
    if (test_2d_per_axis_jump()) return 1;
    printf("%d/%d passed\n", tests_passed, tests_run);
    return (tests_passed == tests_run) ? 0 : 1;
}
