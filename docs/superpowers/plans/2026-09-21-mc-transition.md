# mc_transition Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add the smooth_ui_toolkit Transition semantics module (`mc_transition`: 1D/2D/3D + smooth color) to MotionC, then wire consumers: bump eui's motionc submodule and migrate VAMeter's app-eui off its private port.

**Architecture:** New sibling module next to `mc_animate` — absolute-time (`update(now_ms)`), pull-style, built on `mc_easing`/`mc_real_t` with `int32_t` pixel-domain values. Implementation is one header + one .c; tests follow the repo's plain-assert style. Consumer migration is a mechanical rename (see Task 7 sed table).

**Tech Stack:** C99, CMake, no new dependencies, no math.h.

**Spec:** `docs/superpowers/specs/2026-09-21-mc-transition-design.md` (read together with this plan; the semantic contract there is normative — especially contract #7, the deliberate 2D/3D divergence).

## Global Constraints

- C99 only; `mc_transition.c` must not include `math.h` or any new dependency.
- All public symbols use the `mc_` prefix; code style follows `src/mc_animate.c` (small functions, `//`-free comments, `memset` init).
- Semantics must match the spec's 10-point contract exactly: deferred start via `is_changed` (2D/color), persistent `delay`, `jump`/`move` distinction with same-target no-op, per-axis independence, 3D zero-anchored clock with `>=` completion, `reset` state, defaults `duration=1000, path=mc_ease_quad_out, paused+finish`.
- Do NOT modify `mc_animate`, `mc_easing`, `mc_color`, `mc_spring`, `mc_sequence`.
- Values are `int32_t` pixels; easing in `mc_real_t`; interpolation uses 64-bit intermediates: `current = start + (int32_t)((int64_t)(end-start) * path(t) / MC_FP_SCALE)`.
- Test tolerances (spec §Numeric Equivalence): interior easing samples ±5‰ of travel; endpoints, timing, `is_changed` assertions exact.
- Maintain 100% line coverage for the new module (repo standard).
- Repos are pushed in dependency order: montionc → eui → VAMeter. Do not start a task before the previous repo's task is committed.

---

### Task 1: montionc — module skeleton + 1D transition (TDD)

**Files:**
- Create: `include/mc_transition.h`
- Create: `src/mc_transition.c`
- Create: `test/test_transition.c`
- Modify: `test/CMakeLists.txt` (append one line)

**Interfaces:**
- Consumes: `mc_real_t`, `MC_FP_SCALE`, `MC_REAL_FROM_INT` (`mc_types.h`), `mc_easing_fn_t`, `mc_ease_quad_out`, `mc_ease_linear`, `mc_ease_back_out` (`mc_easing.h`).
- Produces (used by Tasks 2-4): `mc_transition_t` struct; `mc_transition_init/reset/jump/update`; `MC_TRANSITION_H` guard. Task 2 relies on 1D `update` consuming `is_paused/is_finish` so 2D can flip them externally.

- [ ] **Step 1: Write the failing test**

Create `test/test_transition.c`:

```c
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

int main(void) {
    printf("test_transition:\n");
    if (test_1d_init_defaults()) return 1;
    if (test_1d_jump()) return 1;
    if (test_1d_linear_timeline()) return 1;
    if (test_1d_back_overshoot()) return 1;
    if (test_1d_delay_persistent()) return 1;
    if (test_1d_reset()) return 1;
    if (test_1d_zero_duration()) return 1;
    printf("%d/%d passed\n", tests_passed, tests_run);
    return (tests_passed == tests_run) ? 0 : 1;
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cd /Users/solosky/Projects/mycode/montionc && cmake -B build -DMC_BUILD_EXAMPLE=OFF && cmake --build build --target test_transition -j && ctest --test-dir build -R test_transition --output-on-failure`
Expected: compile FAIL — `mc_transition.h: No such file or directory`.

- [ ] **Step 3: Write the header (complete — all four sub-APIs, so Tasks 2-4 only add .c code)**

Create `include/mc_transition.h`:

```c
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
```

- [ ] **Step 4: Write the 1D implementation**

Create `src/mc_transition.c`:

```c
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
```

- [ ] **Step 5: Register the test**

In `test/CMakeLists.txt`, after the `add_mc_test(test_allocator)` line, append:

```cmake
add_mc_test(test_transition)
```

- [ ] **Step 6: Run test to verify it passes**

Run: `cmake --build build --target test_transition -j && ctest --test-dir build -R test_transition --output-on-failure`
Expected: PASS, `7/7 passed`.

- [ ] **Step 7: Commit**

```bash
cd /Users/solosky/Projects/mycode/montionc
git add include/mc_transition.h src/mc_transition.c test/test_transition.c test/CMakeLists.txt
git commit -m "feat(transition): add mc_transition 1D — absolute-time pull-style transition"
```

---

### Task 2: montionc — 2D transition + deferred start + callback

**Files:**
- Modify: `src/mc_transition.c` (append)
- Modify: `test/test_transition.c` (append tests + main calls)

**Interfaces:**
- Consumes: Task 1's `mc_transition_t` API and the `tr_interpolate` helper.
- Produces: `mc_transition2d_*` functions (used by Task 4's pattern and by consumers in Tasks 6-7). Note for consumers: `move_to` only sets endpoints + `is_changed`; the clock anchors at the next `update`.

- [ ] **Step 1: Append failing 2D tests** to `test/test_transition.c` (before `main`):

```c
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
```

In `main`, after the last `if (test_1d_zero_duration()) ...` line, add:

```c
    if (test_2d_jump_and_finish()) return 1;
    if (test_2d_deferred_start_timeline()) return 1;
    if (test_2d_delay_holds_start()) return 1;
    if (test_2d_noop_retarget()) return 1;
    if (test_2d_update_callback()) return 1;
    if (test_2d_per_axis_jump()) return 1;
```

- [ ] **Step 2: Run to verify failure**

Run: `cmake --build build --target test_transition -j && ./build/test/test_transition`
Expected: link FAIL — `undefined reference to mc_transition2d_init`.

- [ ] **Step 3: Append 2D implementation** to `src/mc_transition.c`:

```c
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
```

- [ ] **Step 4: Run to verify pass**

Run: `cmake --build build --target test_transition -j && ctest --test-dir build -R test_transition --output-on-failure`
Expected: PASS, `13/13 passed`.

- [ ] **Step 5: Commit**

```bash
git add src/mc_transition.c test/test_transition.c
git commit -m "feat(transition): add 2D with deferred start (is_changed) and update callback"
```

---

### Task 3: montionc — 3D transition (zero-anchored clock, `>=` completion)

**Files:**
- Modify: `src/mc_transition.c` (append)
- Modify: `test/test_transition.c` (append tests + main calls)

**Interfaces:**
- Consumes: `mc_transition_t` struct fields directly (1D), `tr_interpolate`.
- Produces: `mc_transition3d_*` API. Semantics are pinned by the ported test below (from VAMeter `test_transition3d.c` — exact timing assertions, linear path so all samples are exact).

- [ ] **Step 1: Append failing 3D tests** before `main`:

```c
static int test_3d_per_axis_delay_zero_anchor(void) {
    TEST("3D per-axis delay, clock anchored at 0 (no deferral)");
    mc_transition3d_t t;
    mc_transition3d_init(&t);
    mc_transition3d_set_duration(&t, 100);
    mc_transition3d_set_path(&t, mc_ease_linear);
    mc_transition3d_set_each_delay(&t, 0, 50, 100);
    mc_transition3d_jump_to(&t, 0, 0, 0);
    mc_transition3d_move_to(&t, 100, 100, 100);  /* unpauses NOW, offset stays 0 */
    mc_transition3d_update(&t, 50);
    CHECK(mc_transition3d_x(&t) == 50);          /* no delay: already halfway */
    CHECK(mc_transition3d_y(&t) == 0);           /* delay 50: just starting */
    CHECK(mc_transition3d_z(&t) == 0);           /* delay 100: not started */
    mc_transition3d_update(&t, 200);
    mc_transition3d_move_to(&t, 100, 100, 100);  /* same target: no-op, no restart */
    CHECK(mc_transition3d_is_finish(&t) == true);
    CHECK(mc_transition3d_x(&t) == 100);
    PASS(); return 0;
}

static int test_3d_completion_at_exact_boundary(void) {
    TEST("3D >= completion: finish exactly at delay+duration (delay 100, dur 100)");
    mc_transition3d_t t;
    mc_transition3d_init(&t);
    mc_transition3d_set_duration(&t, 100);
    mc_transition3d_set_path(&t, mc_ease_linear);
    mc_transition3d_set_delay(&t, 100);
    mc_transition3d_jump_to(&t, 0, 0, 0);
    mc_transition3d_move_to(&t, 10, 10, 10);
    mc_transition3d_update(&t, 199);
    CHECK(mc_transition3d_is_finish(&t) == false);
    mc_transition3d_update(&t, 200);             /* 100+100 exactly */
    CHECK(mc_transition3d_is_finish(&t) == true);
    CHECK(mc_transition3d_x(&t) == 10);
    PASS(); return 0;
}
```

In `main`, after the 2D block, add:

```c
    if (test_3d_per_axis_delay_zero_anchor()) return 1;
    if (test_3d_completion_at_exact_boundary()) return 1;
```

- [ ] **Step 2: Run to verify failure**

Run: `cmake --build build --target test_transition -j && ./build/test/test_transition`
Expected: link FAIL — `undefined reference to mc_transition3d_init`.

- [ ] **Step 3: Append 3D implementation** to `src/mc_transition.c`:

```c
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
```

- [ ] **Step 4: Run to verify pass**

Run: `cmake --build build --target test_transition -j && ctest --test-dir build -R test_transition --output-on-failure`
Expected: PASS, `15/15 passed`.

- [ ] **Step 5: Commit**

```bash
git add src/mc_transition.c test/test_transition.c
git commit -m "feat(transition): add 3D with zero-anchored clock and >= completion"
```

---

### Task 4: montionc — smooth color transition

**Files:**
- Modify: `src/mc_transition.c` (append)
- Modify: `test/test_transition.c` (append tests + main calls)

**Interfaces:**
- Consumes: 1D API.
- Produces: `mc_color_trans_*` API — completes the module surface declared in Task 1's header. Ported from VAMeter launcher/pm color-transition timings (e.g. `0xCBDEEE → 0x536484`, 350 ms).

- [ ] **Step 1: Append failing color tests** before `main`:

```c
static int test_color_jump_move_value(void) {
    TEST("color jump_to/move_to/value with VAMeter palette timing");
    mc_color_trans_t ct;
    mc_color_trans_init(&ct);
    mc_color_trans_set_duration(&ct, 350);
    mc_color_trans_jump_to(&ct, 0xCBDEEE);
    CHECK(mc_color_trans_value(&ct) == 0xCBDEEE);
    mc_color_trans_move_to(&ct, 0x536484);       /* deferred start */
    mc_color_trans_update(&ct, 100);             /* anchor */
    mc_color_trans_update(&ct, 100 + 175);       /* midpoint: per-channel check */
    CHECK(mc_color_trans_value(&ct) != 0x536484);
    mc_color_trans_update(&ct, 100 + 351);       /* strict > (2D rules) */
    CHECK(mc_color_trans_value(&ct) == 0x536484);
    PASS(); return 0;
}

static int test_color_same_value_noop(void) {
    TEST("color move_to same value is a no-op");
    mc_color_trans_t ct;
    mc_color_trans_init(&ct);
    mc_color_trans_set_duration(&ct, 100);
    mc_color_trans_jump_to(&ct, 0x112233);
    mc_color_trans_move_to(&ct, 0x112233);       /* value() == target: no re-anchor */
    mc_color_trans_update(&ct, 1000);
    mc_color_trans_update(&ct, 2000);
    CHECK(mc_color_trans_value(&ct) == 0x112233);
    PASS(); return 0;
}
```

In `main`, after the 3D block, add:

```c
    if (test_color_jump_move_value()) return 1;
    if (test_color_same_value_noop()) return 1;
```

- [ ] **Step 2: Run to verify failure**

Run: `cmake --build build --target test_transition -j && ./build/test/test_transition`
Expected: link FAIL — `undefined reference to mc_color_trans_init`.

- [ ] **Step 3: Append color implementation** to `src/mc_transition.c`:

```c
// ---------------------------------------------------------------------------
// Smooth color transition — three 1D channels, one clock (contract #9).
// ---------------------------------------------------------------------------

void mc_color_trans_init(mc_color_trans_t *ct)
{
    mc_transition_init(&ct->r);
    mc_transition_init(&ct->g);
    mc_transition_init(&ct->b);
    ct->is_changed = false;
}

void mc_color_trans_set_duration(mc_color_trans_t *ct, uint32_t ms)
{
    ct->r.duration = ct->g.duration = ct->b.duration = ms;
}

void mc_color_trans_jump_to(mc_color_trans_t *ct, uint32_t rgb888)
{
    mc_transition_jump(&ct->r, (rgb888 >> 16) & 0xFF, (rgb888 >> 16) & 0xFF);
    mc_transition_jump(&ct->g, (rgb888 >> 8) & 0xFF, (rgb888 >> 8) & 0xFF);
    mc_transition_jump(&ct->b, rgb888 & 0xFF, rgb888 & 0xFF);
}

void mc_color_trans_move_to(mc_color_trans_t *ct, uint32_t rgb888)
{
    if (rgb888 == mc_color_trans_value(ct)) return;
    mc_transition_jump(&ct->r, ct->r.current_value, (rgb888 >> 16) & 0xFF);
    mc_transition_jump(&ct->g, ct->g.current_value, (rgb888 >> 8) & 0xFF);
    mc_transition_jump(&ct->b, ct->b.current_value, rgb888 & 0xFF);
    mc_transition_reset(&ct->r);
    mc_transition_reset(&ct->g);
    mc_transition_reset(&ct->b);
    ct->is_changed = true;
}

uint32_t mc_color_trans_value(const mc_color_trans_t *ct)
{
    return ((uint32_t)ct->r.current_value << 16) |
           ((uint32_t)ct->g.current_value << 8) | (uint32_t)ct->b.current_value;
}

void mc_color_trans_update(mc_color_trans_t *ct, uint32_t now_ms)
{
    if (ct->is_changed) {
        ct->is_changed = false;
        ct->r.time_offset = now_ms;
        ct->g.time_offset = now_ms;
        ct->b.time_offset = now_ms;
        ct->r.is_paused = false;
        ct->g.is_paused = false;
        ct->b.is_paused = false;
    }
    mc_transition_update(&ct->r, now_ms);
    mc_transition_update(&ct->g, now_ms);
    mc_transition_update(&ct->b, now_ms);
}
```

- [ ] **Step 4: Run to verify pass**

Run: `cmake --build build --target test_transition -j && ctest --test-dir build -R test_transition --output-on-failure`
Expected: PASS, `17/17 passed`.

- [ ] **Step 5: Commit**

```bash
git add src/mc_transition.c test/test_transition.c
git commit -m "feat(transition): add smooth color transition (SmoothRGB equivalent)"
```

---

### Task 5: montionc — coverage check, README, push

**Files:**
- Modify: `README.md` (module table + division-of-labor note)

**Interfaces:**
- Produces: pushed `main` containing the complete module — the SHA Task 6 pins.

- [ ] **Step 1: Build with coverage and verify 100% line coverage of the new module**

```bash
cd /Users/solosky/Projects/mycode/montionc
cmake -B build-cov -DCMAKE_C_FLAGS="--coverage" -DCMAKE_EXE_LINKER_FLAGS="--coverage" -DMC_BUILD_EXAMPLE=OFF
cmake --build build-cov -j
ctest --test-dir build-cov --output-on-failure
gcov -b build-cov/CMakeFiles/mc.dir/src/mc_transition.c.o
```
Expected: `mc_transition.c` lines: 100.0% covered. If any line is missed, add a test exercising it (typically the no-op early-returns in `move_to` or the `update_callback == NULL` branch) and rerun. Note: if the `.o` path differs with your generator, locate it with `find build-cov -name 'mc_transition.c.o'`.

- [ ] **Step 2: Run the full test suite**

Run: `cmake --build build -j && ctest --test-dir build --output-on-failure`
Expected: all suites pass, including the new `test_transition`.

- [ ] **Step 3: Update README**

In the module table (after the `mc_animate.h` row), add:

```markdown
| `mc_transition.h` | `mc_transition_t`, `mc_transition2d_t/3d_t`, `mc_color_trans_t` | 平滑 UI Transition 语义（绝对时间 now_ms 驱动、拉取式、延迟启动）|
```

After the `## 模块` table section, add:

```markdown
## mc_transition vs mc_animate

- `mc_animate`：dt 驱动、push 式（on_update 回调），服务补间序列、弹簧物理、repeat/loop。
- `mc_transition`：绝对时间驱动（`update(now_ms)`）、拉取式（随时读 x()/y()），
  服务立即式 UI 元素动画。位置是墙钟时间的纯函数，掉帧不影响轨迹。
  语义契约见 `docs/superpowers/specs/2026-09-21-mc-transition-design.md`。
```

- [ ] **Step 4: Commit and push**

```bash
git add README.md
git commit -m "docs: add mc_transition module to README; document mc_animate division of labor"
git push origin main
```
Record the new `main` HEAD SHA — Task 6 pins it. Expected: push succeeds to `git@github.com:solosky/montionc.git`.

---

### Task 6: eui — bump third_party/motionc submodule

**Files:**
- Modify: `third_party/motionc` (submodule pointer) in repo `/Users/solosky/Projects/mycode/eui`

**Interfaces:**
- Consumes: Task 5's pushed montionc SHA.
- Produces: an eui commit on `fix/raylib-first-present` whose tree contains `mc_transition.h`; VAMeter (Task 7) pins this eui commit. VAMeter currently tracks the `fix/raylib-first-present` line (it carries the validated raylib first-present fix, commit 16c38c0), so the bump lands on that branch; merging it to `main` remains a separate, user-owned decision.

- [ ] **Step 1: Fetch and checkout the new motionc in eui**

```bash
cd /Users/solosky/Projects/mycode/eui
git checkout fix/raylib-first-present
git -C third_party/motionc fetch origin
git -C third_party/motionc checkout <SHA_FROM_TASK_5>
git status --short   # expect: " M third_party/motionc"
```

- [ ] **Step 2: Verify mc_transition.h is present and eui still builds**

Run: `ls third_party/motionc/include/mc_transition.h && cmake -B /tmp/eui-bump-check -DEUI_BUILD_TESTS=OFF -DEUI_BUILD_CROSS_EXAMPLES=OFF && cmake --build /tmp/eui-bump-check -j`
Expected: header exists; eui builds clean with the new motionc.

- [ ] **Step 3: Commit and push**

```bash
git add third_party/motionc
git commit -m "chore: bump motionc submodule (mc_transition module)"
git push origin fix/raylib-first-present
```
Record the new eui commit SHA — Task 7 pins it.

---

### Task 7: VAMeter — bump eui, mechanical migration, delete private port

**Files (all under `/Users/solosky/Projects/mycode/VAMeter-Firmware`, branch `eui`):**
- Modify: `dependencies/eui` (submodule pointer → Task 6 SHA)
- Rename via sed (12 app files + 6 test files + `ui/eui_selector.{c,h}`): exact table below
- Create: `ui/ui_draw.c` gains `ui_rgb888_to_565` (moved); `ui/ui_draw.h` gains declaration
- Delete: `ui/eui_transition2d.c`, `ui/eui_transition2d.h`, `ui/eui_transition3d.c`, `ui/eui_transition3d.h`
- Modify: `app-eui/CMakeLists.txt` (remove 2 source lines + 2 test targets)

**Interfaces:**
- Consumes: `mc_transition.h` via eui's `PUBLIC` include dir (`eui/src/CMakeLists.txt:37-41` exports `third_party/motionc/include`); `app_eui` links `eui PUBLIC`, so no include-path changes anywhere.
- Produces: app-eui sources referencing only `mc_transition_*` / `mc_ease_*` / `ui_rgb888_to_565`. Regression coverage: montionc's `test_transition` (Task 1-4) replaces the deleted `test_transition2d/3d`; VAMeter ctest count goes 19 → 17 + 1 GL-skip.

**Rename table (apply in this order, whole-word where noted):**

| Order | sed expression | Rationale |
|---|---|---|
| 1 | `s/eui_transition3d_/mc_transition3d_/g` | functions before generic prefix |
| 2 | `s/eui_transition2d_/mc_transition2d_/g` | |
| 3 | `s/eui_color_trans_/mc_color_trans_/g` | |
| 4 | `s/eui_transition_t/mc_transition_t/g` | 1D struct typedef |
| 5 | `s/eui_transition3d_t/mc_transition3d_t/g` | |
| 6 | `s/eui_transition2d_t/mc_transition2d_t/g` | |
| 7 | `s/eui_color_trans_t/mc_color_trans_t/g` | |
| 8 | `s/eui_transition_/mc_transition_/g` | 1D functions (init/reset/jump/update/set_*); safe because rules 1-6 already consumed the 2D/3D/type tokens. No `\b` — BSD sed (macOS) does not support it. |
| 9 | `s/eui_ease_linear/mc_ease_linear/g` | |
| 10 | `s/eui_ease_out_quad/mc_ease_quad_out/g` | name changes too |
| 11 | `s/eui_ease_out_back/mc_ease_back_out/g` | |
| 12 | `s/eui_rgb888_to_565/ui_rgb888_to_565/g` | function stays project-side, moves to ui_draw |

Include lines: `#include "eui_transition2d.h"` and `#include "eui_transition3d.h"` → `#include "mc_transition.h"` (files including both need the include once — de-dup if sed produces two identical lines).

> **勘误（Task 7 实作补充，2026-09-21）**：上表在执行时有五处未覆盖，实施时已按下述方式补齐（均为机械改名，未改语义）。nfc-kit 后续迁移请一并套用：
>
> 1. **brief 的 shell 循环在 zsh 下不工作**：zsh 不对未加引号的 `$FILES` 做分词，`FILES=$(grep -rl …); for f in $FILES` 只会把整串当作单个词迭代一次、什么也不改。改用 `xargs`（`grep -rl … | xargs sed -i '' …`）或等价写法。
> 2. **部分 include 行带 `ui/` 前缀**：本代码库并非所有 include 都写裸路径——使用方目录（`apps/*.c` 与 `test/smoke_*.c`）写的是 `#include "ui/eui_transition2d.h"` / `"ui/eui_transition3d.h"`（带 `ui/` 前缀），只有 `ui/` 目录内的文件（`eui_selector.h`、`eui_transition2d.c`、`eui_transition3d.{c,h}`）与 `test/test_transition2d.c`、`test/test_transition3d.c` 用裸路径。表中两条裸路径模式只覆盖后者，前者须另配带 `ui/` 前缀的模式，两种都要保留。
> 3. **`eui_easing_fn_t` 未列入表内**：该 typedef 仅定义于被删除的 `ui/eui_transition2d.h`，而规则 8 只改 `eui_transition_`，故它幸存到改名之后并破坏构建；须改为 `mc_easing_fn_t`，使用点是 `ui/eui_selector.{c,h}`（即 `app-eui/ui/eui_selector.c:170` 与 `app-eui/ui/eui_selector.h:65`）。
> 4. **`test_selector` 还有第三处对已删源文件的 CMake 引用**：除表中提到的 2 个测试目标外，`test_selector` 的 `add_executable` 源列表里也列有 `ui/eui_transition2d.c`，须一并删除，否则 CMake 报 `Cannot find source file`。
> 5. **`test/smoke_menu.c` 需显式补 `#include "ui/ui_draw.h"`**：该文件原本没有任何 transition include，`ui_rgb888_to_565` 是经 `apps/menu_view.h → ui/eui_selector.h → ui/eui_transition2d.h` 传递而来的；这条传递路径现为 `mc_transition.h`，不再声明该函数。

- [ ] **Step 1: Bump the eui submodule**

```bash
cd /Users/solosky/Projects/mycode/VAMeter-Firmware
git checkout eui
git -C dependencies/eui fetch origin
git -C dependencies/eui checkout <EUI_SHA_FROM_TASK_6>
git add dependencies/eui
```

- [ ] **Step 2: Apply the rename table** to every file in `app-eui/` (`apps/*.c`, `apps/*.h`, `ui/*.c`, `ui/*.h`, `test/*.c`):

```bash
cd /Users/solosky/Projects/mycode/VAMeter-Firmware/app-eui
FILES=$(grep -rl 'eui_transition\|eui_color_trans\|eui_ease_out\|eui_ease_linear\|eui_rgb888_to_565' apps ui test)
for f in $FILES; do
  sed -i '' -e 's/eui_transition3d_/mc_transition3d_/g' \
            -e 's/eui_transition2d_/mc_transition2d_/g' \
            -e 's/eui_color_trans_/mc_color_trans_/g' \
            -e 's/eui_transition_t/mc_transition_t/g' \
            -e 's/eui_transition3d_t/mc_transition3d_t/g' \
            -e 's/eui_transition2d_t/mc_transition2d_t/g' \
            -e 's/eui_color_trans_t/mc_color_trans_t/g' \
            -e 's/eui_transition_/mc_transition_/g' \
            -e 's/eui_ease_linear/mc_ease_linear/g' \
            -e 's/eui_ease_out_quad/mc_ease_quad_out/g' \
            -e 's/eui_ease_out_back/mc_ease_back_out/g' \
            -e 's/eui_rgb888_to_565/ui_rgb888_to_565/g' \
            -e 's/#include "eui_transition2d.h"/#include "mc_transition.h"/' \
            -e 's/#include "eui_transition3d.h"/#include "mc_transition.h"/' "$f"
done
# de-dup identical adjacent mc_transition.h includes (files that had both 2d+3d)
for f in $FILES; do awk 'NR>0 && $0=="#include \"mc_transition.h\"" && prev==$0 {next} {print; prev=$0}' "$f" > "$f.tmp" && mv "$f.tmp" "$f"; done
grep -rn 'eui_transition\|eui_color_trans\|eui_ease_out\|eui_rgb888_to_565' apps ui test && echo "RESIDUE FOUND" || echo "rename clean"
```
Expected: `rename clean`. Known residue exception: comments referencing the original C++ source (e.g. `view_single_data.cpp`) are untouched — verify no `eui_transition`/`eui_color_trans` identifiers remain outside comments.

- [ ] **Step 3: Move `rgb888_to_565` into ui_draw**

Delete the function from the (soon-removed) transition header/impl by creating it in `ui/ui_draw.c`:

```c
uint16_t ui_rgb888_to_565(uint32_t rgb888)
{
    uint8_t r = (rgb888 >> 16) & 0xFF, g = (rgb888 >> 8) & 0xFF, b = rgb888 & 0xFF;
    return (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
}
```

Append to `ui/ui_draw.h` (before `#endif`):

```c
/**
 * @brief Convert RGB888 to RGB565 (canvas native format).
 */
uint16_t ui_rgb888_to_565(uint32_t rgb888);
```

- [ ] **Step 4: Delete the private port and its CMake entries**

```bash
cd /Users/solosky/Projects/mycode/VAMeter-Firmware/app-eui
git rm ui/eui_transition2d.c ui/eui_transition2d.h ui/eui_transition3d.c ui/eui_transition3d.h
```

In `app-eui/CMakeLists.txt`: remove the two library lines `ui/eui_transition2d.c` and `ui/eui_transition3d.c` from `add_library(app_eui ...)`, and delete the two test blocks for `test_transition2d` and `test_transition3d` (`add_executable` + `target_include_directories` + `target_link_libraries` + `add_test` for each).

- [ ] **Step 5: Delete the old transition test files**

Their assertions now live in montionc's `test_transition` (Tasks 1-4); the CMake entries for them were removed in Step 4:

```bash
git rm test/test_transition2d.c test/test_transition3d.c
```

- [ ] **Step 6: Build and run the full suite**

```bash
cd /Users/solosky/Projects/mycode/VAMeter-Firmware
cmake --build build -j 2>&1 | tail -5
ctest --test-dir build --output-on-failure
```
Expected: clean build; ctest reports 17 total — 16 passed + 1 skipped (`sim_smoke_exit`, GL-less shell). If a smoke test fails on an **interior pixel/position assertion** only, relax by ±1 px with a comment `/* spec 2026-09-21-mc-transition: Q16.16 easing ±4 permille */` — endpoint and timing failures are NOT relaxable, they indicate a migration mistake; investigate instead.

- [ ] **Step 7: Commit**

```bash
git add -A
git commit -m "refactor(app-eui): migrate to motionc mc_transition, drop private transition port

- types/functions renamed per spec migration table (eui_transition* -> mc_transition*)
- easing: permille eui_ease_* -> Q16.16 mc_ease_* (max dev 4 permille, endpoints exact)
- eui_rgb888_to_565 -> ui_rgb888_to_565, relocated to ui_draw
- test_transition2d/3d assertions live in montionc test_transition now
- dependencies/eui bumped for third_party/motionc (mc_transition)"
```

---

## Follow-ups (explicitly out of scope)

- nfc-kit `fw/app-eui` has the same private port; migrate it with Task 7's table after this plan lands.
- eui `eui_widget_selector` promotion and the `fix/raylib-first-present` → `main` merge remain separate decisions.
