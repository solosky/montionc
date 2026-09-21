# mc_transition — Smooth UI Toolkit Transition Semantics for MotionC

## Motivation

MotionC's animation engine (`mc_animate`) is **dt-driven and push-style**: the caller
supplies a frame delta each update, and results are delivered through `on_update`
callbacks. That model fits tween sequences and spring simulations well, but it does
not match how immediate-mode embedded UIs animate. Two real consumers
(VAMeter-Firmware `app-eui`, nfc-kit `fw/app-eui`) independently needed the
**Transition semantics of the original smooth_ui_toolkit** (`Transition`,
`Transition2D`, `Transition3D`, `SmoothRGB`) and each had to hand-port them
project-side (`eui_transition2d.c` / `eui_transition3d.c`), because the semantics
differ from `mc_animate` in ways that are not shimmable:

| Semantic | mc_animate (dt/push) | smooth_ui Transition (now_ms/pull) |
|----------|----------------------|-------------------------------------|
| Clock | Caller accumulates `dt`; delay is **destructive** (`delay -= dt`, consumed once) | Anchor `time_offset` at start; `delay` is a **persistent config** reused by every `move_to` |
| Start moment | Next `update(dt)` after retarget | Deferred: `move_to` only sets endpoints + `is_changed`; the clock starts at the **next `update(now)`**, anchoring `time_offset = now` |
| Result delivery | Push via `on_update(ctx, value)` | Pull: view reads `x() / y()` during draw |
| 2D granularity | Whole-struct ops on `mc_vec2_t` | Per-axis: independent `jump`, per-axis readers, per-axis `end_x` |
| Completion | `update()` return value (must poll by calling) | Queryable state: `is_finish` (plus `is_paused`) readable at any time |
| Retarget guard | `move_to` always restarts | `move_to` to the current target is a **no-op** (does not reset a running clock) |

The eui simulator project validated the port against the original C++ toolkit with
per-constant animation alignment (its regression tests pin easing values, delays,
and `is_changed` timing). That port is the provenance of this design: promote the
**semantics** into MotionC as a first-class module, implemented on MotionC's own
numeric base.

## Goals

1. A new `mc_transition` module implementing the smooth_ui_toolkit Transition
   semantics: 1D, 2D, 3D, and smooth color transition.
2. Full reuse of existing MotionC facilities — `mc_real_t`, `mc_easing_fn_t`,
   `mc_easing_*` functions. No new easing code, no new math.
3. Port the field-proven regression tests (VAMeter `test_transition2d/3d`) so the
   animation-alignment guarantees survive the migration.
4. Enable consumers (eui-based projects) to delete their private transition ports
   and depend on MotionC directly.

## Non-Goals

- No changes to `mc_animate` / `mc_spring` / `mc_sequence` — they keep serving
  dt/push use cases unchanged. The two models coexist (see "Relation to existing
  modules").
- No central animation scheduler, ticker, or frame manager. Transitions are
  advanced explicitly by the caller (`update(now_ms)`), typically from a draw
  handler.
- No display-format knowledge (RGB565 conversion stays in the UI framework layer).
- No relocation of higher-level widgets (e.g. the animated selector) into MotionC.
- No float-path-specific work beyond what `MC_USE_FLOAT` already provides.

## Relation to Existing Modules

```
mc_easing (reused as-is)      mc_color (reused as-is for RGB888 helpers)
        \                            /
         mc_transition  (NEW — now_ms/pull semantics layer)
                         |            \
   mc_animate (unchanged dt/push)    (future consumers: eui widgets)
```

`mc_transition` is a **sibling** of `mc_animate`, not a replacement and not built
on top of it: the destructive-delay and push-callback model of `mc_animate`
cannot express the anchored-clock semantics above. Both modules share
`mc_easing`/`mc_real_t`. Documented division of labor:

- `mc_animate`: framework-driven animations — tween sequences, spring physics,
  repeat/loop, completion callbacks.
- `mc_transition`: immediate-mode UI element animation — declarative targets set
  from input handlers, positions pulled during draw, trajectory a pure function
  of wall-clock time.

## Semantics Contract

The module guarantees the following, mirroring the original toolkit:

1. **Absolute-time evaluation.** `current` is a pure function of `now_ms`:
   ```
   delta = now_ms - time_offset
   delta < delay            → current = start        (hold)
   delta - delay > duration → current = end, is_finish = true   (stop evaluating)
   otherwise                → t = (delta - delay) / duration            [0..1]
                              current = start + (end - start) * path(t)
   ```
   No state accumulates per frame. Frame rate affects sampling density only; a
   dropped frame lands exactly on the correct position next update.
2. **Deferred start (`is_changed`).** `move_to` sets `start = current`, `end =
   target`, clears `is_finish`, and raises `is_changed` — without touching the
   clock. The next `update(now_ms)` consumes `is_changed` and anchors
   `time_offset = now_ms`. Input handlers may therefore retarget freely; motion
   begins on the following drawn frame with the same `now` used to render it.
3. **Persistent delay.** `delay` is configuration anchored per start, never
   consumed. A 300 ms delay set once applies to every subsequent `move_to`.
4. **`jump` vs `move`.** `jump(start, end)` teleports (`current = end`,
   `is_finish = true`) and only records endpoints. `move_to` animates with the
   configured duration/delay/path. `move_to` to the already-current target is a
   no-op that does not disturb a running clock.
5. **Pull model.** Readers (`x()`, `y()`, `end_x()`, `value()`, `is_finish()`)
   work at any time without invoking update and without callbacks. An optional
   `update_callback(self, user_data)` fires after each 2D update for derived
   state.
6. **Per-axis independence.** 2D/3D store one 1D transition per axis. `jump_to`
   uses each axis's current value as its own start; `is_finish` is the AND of all
   axes; 3D additionally supports per-axis delays (`set_each_delay`).
7. **3D diverges from 2D deliberately.** The original toolkit's 3D transition
   has no deferral flag: `move_to` unpauses immediately with the clock anchored
   at `time_offset = 0` (so `update(50)` on a delay-free axis is already
   mid-flight), and completion uses `>=` — `is_finish` becomes true exactly at
   `delay + duration`. 2D keeps the `is_changed` deferral and finishes strictly
   after `delay + duration` (`>`). Both behaviors are pinned by the original
   toolkit's test timing and must be preserved as-is.
8. **Reset.** `reset` returns to `start`, clears `time_offset`, sets
   `is_paused = true`, `is_finish = false` — the "armed but not started" state
   that `move_to` + `is_changed` later launches.
9. **Color transition.** `mc_color_trans_t` runs three 1D transitions (R, G, B,
   RGB888 domains) under one clock, exposing `jump_to(rgb888) / move_to(rgb888) /
   value()`. It carries its own `is_changed` member — a deliberate unification
   over the private port (which inferred "armed" from `is_paused && !is_finish`);
   observable behavior is identical.
10. **Defaults.** `init` sets `duration = 1000`, `path = mc_ease_quad_out`,
   `is_paused = true`, `is_finish = true` — matching the private ports.

## API Design

Single module: `include/mc_transition.h` + `src/mc_transition.c` (~300 lines).

### Value domain

Values are **`int32_t` pixel-domain integers**; easing runs in MotionC's native
`mc_real_t` (Q16.16 by default) and is folded to integers internally with 64-bit
intermediates:

```c
current = start + (int32_t)(((int64_t)(end - start) * path(t)) / MC_FP_SCALE);
```

Endpoints are exact (`path(0) = 0`, `path(1) = MC_FP_SCALE`). Overshoot easings
(back) may exceed `[start, end]`; `int32_t` has ample headroom. Rationale: UI
coordinates are integers in every consumer; keeping values integer keeps view
code free of conversion noise, while easing quality is unaffected (verified
below).

### Structures and functions

```c
typedef int32_t mc_transition_value_t;

/* 1D */
typedef struct {
    mc_transition_value_t start_value, end_value, current_value;
    uint32_t duration, delay;
    uint32_t time_offset;
    mc_easing_fn_t path;
    bool is_paused, is_finish;
} mc_transition_t;

void mc_transition_init(mc_transition_t *t);
void mc_transition_reset(mc_transition_t *t);
void mc_transition_jump(mc_transition_t *t, int start, int end);
void mc_transition_update(mc_transition_t *t, uint32_t now_ms);

/* 2D — is_changed lives here (the deferral flag), plus optional per-frame hook */
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

/* 3D — shared duration/path; per-axis delay supported. No is_changed: the
   clock anchors at time_offset = 0 and completes with >= (see contract #7). */
typedef struct { mc_transition_t x, y, z; } mc_transition3d_t;
/* init / set_duration / set_delay / set_each_delay(dx,dy,dz) / set_path /
   update / jump_to / move_to / x / y / z / is_finish                      */

/* Smooth color (SmoothRGB equivalent), RGB888 in and out */
typedef struct { mc_transition_t r, g, b; bool is_changed; } mc_color_trans_t;
void mc_color_trans_init(mc_color_trans_t *ct);
void mc_color_trans_set_duration(mc_color_trans_t *ct, uint32_t ms);
void mc_color_trans_jump_to(mc_color_trans_t *ct, uint32_t rgb888);
void mc_color_trans_move_to(mc_color_trans_t *ct, uint32_t rgb888);
uint32_t mc_color_trans_value(const mc_color_trans_t *ct);
void mc_color_trans_update(mc_color_trans_t *ct, uint32_t now_ms);
```

No public tick/HAL dependency: `update` takes `now_ms` explicitly. Consumers pass
their platform clock (eui projects already thread `eui_get_tick_ms()` through
their draw handlers).

## Numeric Equivalence

The private ports used permille-integer easing (`int fn(int t)`, domain
0..1000); MotionC easing is Q16.16 (`mc_real_t fn(mc_real_t t)`). Verified by a
full-range differential check against the ported `easeOutBack`
(`c1 = 1.70158`): **maximum deviation 4‰ of travel**, pure fixed-point rounding —
under 1 px at 240 px. Endpoints match exactly. Interior-sample assertions in
ported tests therefore use a ±5‰ (of travel) tolerance; endpoint and
timing/`is_changed` assertions remain exact.

## Testing Plan

- Port VAMeter's `test_transition2d.c` / `test_transition3d.c` as
  `test/test_transition.c` (1D+2D+3D) and add color-transition cases, preserving
  every timing assertion (deferred start, persistent delay, no-op retarget,
  jump/move distinction, per-axis independence, reset state) including the 3D
  divergence cases (zero-anchored clock, `>=` completion, per-axis delays).
- Interior easing-value assertions switch from exact permille values to
  ±5‰-of-travel tolerance; endpoint assertions stay exact.
- Maintain the repository's 100% line-coverage standard for the new module.
- CMake: nothing to wire — `file(GLOB_RECURSE src/*.c)` picks the module up;
  tests register in `test/CMakeLists.txt` per existing pattern.

## Integration & Migration Plan

Consumers migrate in dependency order; each step is independently green.

1. **MotionC**: land `mc_transition` + tests; push.
2. **eui**: bump its `third_party/motionc` submodule to the new commit. Zero
   build-system changes (CMake `add_subdirectory` and ESP-IDF `REQUIRES motionc`
   already exist). eui keeps `eui_anim` (dt/push, built on `mc_animate`) for
   framework-level view transitions; the two animation styles coexist with a
   documented boundary (framework pushes, views pull).
3. **Consumer projects (VAMeter `app-eui`, nfc-kit `fw/app-eui`)**: mechanical
   rename migration, then delete the private ports:

   | Private port | After |
   |---|---|
   | `eui_transition2d.{c,h}`, `eui_transition3d.{c,h}`, `eui_color_trans*` | deleted; `#include "mc_transition.h"` |
   | `eui_transition_t / 2d_t / 3d_t`, `eui_color_trans_t` | `mc_transition_t / 2d_t / 3d_t`, `mc_color_trans_t` |
   | `eui_ease_linear / out_quad / out_back` (permille) | `mc_ease_linear / quad_out / back_out` (Q16.16) |
   | `eui_rgb888_to_565` | stays project-side (display-format knowledge) |
   | `update(now_ms)` call sites and `eui_get_tick_ms()` threading | unchanged |

   The eui build already propagates MotionC include paths to consumers via its
   existing target linkage.

## Risks

- **Mid-animation pixel assertions** in consumer smoke tests may shift by <1 px
  due to the 4‰ easing difference. Endpoints are exact; any failing interior
  assertion is relaxed by ±1 px with a comment referencing this spec.
- **`is_changed` unification for color** changes no observable behavior (the
  private sentinel `is_paused && !is_finish` is equivalent); noted so reviewers
  do not mistake it for a semantic change.
- **Divergence risk** between this module and the original C++ toolkit is capped
  by the ported regression tests; any future semantic change must update them
  deliberately.
