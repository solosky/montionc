# mc_transition — MotionC 的 Smooth UI Toolkit Transition 语义模块

## 动机

MotionC 现有动画引擎（`mc_animate`）是 **dt 驱动、push 式**的：调用方每次 update 传入帧增量，结果通过 `on_update` 回调推送。这个模型适合补间序列和弹簧模拟，但与立即式嵌入式 UI 的动画方式不匹配。两个真实消费者（VAMeter-Firmware 的 `app-eui`、nfc-kit 的 `fw/app-eui`）各自都需要原版 smooth_ui_toolkit 的 **Transition 语义**（`Transition`、`Transition2D`、`Transition3D`、`SmoothRGB`），并且都不得不在项目侧手工移植（`eui_transition2d.c` / `eui_transition3d.c`），因为其语义与 `mc_animate` 的差异不是一层 shim 能弥合的：

| 语义 | mc_animate（dt/push） | smooth_ui Transition（now_ms/pull） |
|------|----------------------|-------------------------------------|
| 时钟 | 调用方累计 `dt`；delay 是**破坏性的**（`delay -= dt`，消费一次即失效） | 起表时锚定 `time_offset`；`delay` 是**持久配置**，每次 `move_to` 都复用 |
| 起表时刻 | retarget 后的下一次 `update(dt)` | 延迟启动：`move_to` 只设端点 + `is_changed`；时钟在下一次 `update(now)` 才起表，锚定 `time_offset = now` |
| 结果交付 | 通过 `on_update(ctx, value)` 推送 | 拉取：视图在绘制时读 `x() / y()` |
| 2D 粒度 | `mc_vec2_t` 整结构操作 | 按轴：单轴独立 `jump`、按轴读取、按轴 `end_x` |
| 完成判定 | `update()` 返回值（必须调 update 才能轮询到） | 可查询状态：`is_finish`（以及 `is_paused`），任何时刻可读 |
| 改目标守卫 | `move_to` 总是重新起表 | `move_to` 到当前目标时是**空操作**（不打断进行中的时钟） |

eui 模拟器项目已经用逐常数动画对齐验证过这套移植（其回归测试钉住了缓动值、delay 和 `is_changed` 时序）。该移植就是本设计 provenance：把**语义**提升为 MotionC 的一等模块，实现在 MotionC 自己的数值底座上。

## 目标

1. 新增 `mc_transition` 模块，实现 smooth_ui_toolkit 的 Transition 语义：1D、2D、3D 以及平滑颜色过渡。
2. 完全复用 MotionC 现有设施——`mc_real_t`、`mc_easing_fn_t`、`mc_easing_*` 函数。不新增缓动代码，不新增数学代码。
3. 移植经过实战验证的回归测试（VAMeter 的 `test_transition2d/3d`），让动画对齐保证在迁移中存活。
4. 让消费者（基于 eui 的项目）能够删除各自的私有 transition 移植，直接依赖 MotionC。

## 非目标

- 不改动 `mc_animate` / `mc_spring` / `mc_sequence`——它们继续原样服务 dt/push 场景。两种模型并存（见"与现有模块的关系"）。
- 不做中央动画调度器、ticker 或帧管理器。过渡由调用方显式推进（`update(now_ms)`），通常在绘制 handler 里。
- 不引入显示格式知识（RGB565 转换留在 UI 框架层）。
- 不把更高层的 widget（如动画选择器）搬进 MotionC。
- 除 `MC_USE_FLOAT` 已提供的之外，不做 float 路径的专项工作。

## 与现有模块的关系

```
mc_easing（原样复用）          mc_color（RGB888 辅助函数原样复用）
        \                            /
         mc_transition （新增 — now_ms/pull 语义层）
                         |            \
   mc_animate（不变，dt/push）      （未来消费者：eui widgets）
```

`mc_transition` 是 `mc_animate` 的**兄弟**，不是替代、也不架在它之上：`mc_animate` 的破坏性 delay 和 push 回调模型无法表达上述锚定时钟语义。两个模块共用 `mc_easing`/`mc_real_t`。文档化的分工：

- `mc_animate`：框架驱动的动画——补间序列、弹簧物理、repeat/loop、完成回调。
- `mc_transition`：立即式 UI 元素动画——输入 handler 里声明式设目标，绘制时拉取位置，轨迹是墙钟时间的纯函数。

## 语义契约

模块保证以下各条，与原版 toolkit 一致：

1. **绝对时间求值。** `current` 是 `now_ms` 的纯函数：
   ```
   delta = now_ms - time_offset
   delta < delay            → current = start        （保持）
   delta - delay > duration → current = end, is_finish = true   （停止计算）
   否则                     → t = (delta - delay) / duration            [0..1]
                              current = start + (end - start) * path(t)
   ```
   没有任何每帧累计的状态。帧率只影响采样密度；掉一帧后下一次 update 会精确落在正确位置上。
2. **延迟启动（`is_changed`）。** `move_to` 设 `start = current`、`end = 目标`、清 `is_finish`，并置 `is_changed`——不动时钟。下一次 `update(now_ms)` 消费 `is_changed` 并锚定 `time_offset = now_ms`。因此输入 handler 可以随意改目标；运动从下一个被绘制的帧开始，且起表用的 `now` 与渲染该帧用的 `now` 相同。
3. **持久 delay。** `delay` 是按次起表锚定的配置，从不被消费。设置一次的 300ms delay 对之后每次 `move_to` 都生效。
4. **`jump` 与 `move` 之分。** `jump(start, end)` 瞬移（`current = end`、`is_finish = true`），只记录端点。`move_to` 按配置的 duration/delay/path 做动画。`move_to` 到当前已相同的目标是空操作，不打断运行中的时钟。
5. **拉取模型。** 读取接口（`x()`、`y()`、`end_x()`、`value()`、`is_finish()`）任何时刻可用，无需调用 update，无需回调。可选的 `update_callback(self, user_data)` 在每次 2D update 之后触发，用于派生状态。
6. **按轴独立。** 2D/3D 每轴存一个 1D transition。`jump_to` 以各轴当前值作为各自起点；`is_finish` 是所有轴的 AND；3D 额外支持按轴 delay（`set_each_delay`）。
7. **3D 有意与 2D 不同。** 原版 toolkit 的 3D transition 没有延迟启动标志：`move_to` 立即解除暂停，时钟锚定在 `time_offset = 0`（所以无 delay 的轴在 `update(50)` 时已到中途），完成判定用 `>=`——`is_finish` 在 `delay + duration` 整点即变 true。2D 保留 `is_changed` 延迟启动，且在 `delay + duration` 严格之后才完成（`>`）。两者都被原版 toolkit 的测试时序钉住，必须原样保留。
8. **Reset。** `reset` 回到 `start`、清 `time_offset`、置 `is_paused = true`、`is_finish = false`——"已上膛未起表"状态，之后由 `move_to` + `is_changed` 发射。
9. **颜色过渡。** `mc_color_trans_t` 在一个时钟下跑三个 1D transition（R、G、B，RGB888 值域），对外暴露 `jump_to(rgb888) / move_to(rgb888) / value()`。它自带 `is_changed` 成员——这是对私有移植的一次刻意统一（原私有版从 `is_paused && !is_finish` 哨兵推断"已上膛"）；可观察行为完全一致。
10. **默认值。** `init` 设 `duration = 1000`、`path = mc_ease_quad_out`、`is_paused = true`、`is_finish = true`——与私有移植一致。

## API 设计

单模块：`include/mc_transition.h` + `src/mc_transition.c`（约 300 行）。

### 值域

值是 **`int32_t` 像素域整数**；缓动跑在 MotionC 原生 `mc_real_t`（默认 Q16.16）上，内部用 64 位中间量折算回整数：

```c
current = start + (int32_t)(((int64_t)(end - start) * path(t)) / MC_FP_SCALE);
```

端点精确（`path(0) = 0`、`path(1) = MC_FP_SCALE`）。过冲缓动（back 系）可能超出 `[start, end]`；`int32_t` 余量充足。理由：所有消费者的 UI 坐标都是整数；值保持整数让视图代码免于转换噪音，缓动质量不受影响（下文已验证）。

### 结构与函数

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

/* 2D — is_changed（延迟启动标志）在这里，外加可选的每帧钩子 */
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

/* 3D — 共享 duration/path；支持按轴 delay。
   无 is_changed：时钟锚定 time_offset = 0，完成判定用 >=（见契约第 7 条）。 */
typedef struct { mc_transition_t x, y, z; } mc_transition3d_t;
/* init / set_duration / set_delay / set_each_delay(dx,dy,dz) / set_path /
   update / jump_to / move_to / x / y / z / is_finish                      */

/* 平滑颜色（SmoothRGB 等价物），RGB888 进出 */
typedef struct { mc_transition_t r, g, b; bool is_changed; } mc_color_trans_t;
void mc_color_trans_init(mc_color_trans_t *ct);
void mc_color_trans_set_duration(mc_color_trans_t *ct, uint32_t ms);
void mc_color_trans_jump_to(mc_color_trans_t *ct, uint32_t rgb888);
void mc_color_trans_move_to(mc_color_trans_t *ct, uint32_t rgb888);
uint32_t mc_color_trans_value(const mc_color_trans_t *ct);
void mc_color_trans_update(mc_color_trans_t *ct, uint32_t now_ms);
```

不依赖公开的 tick/HAL：`update` 显式传入 `now_ms`。消费者传自己的平台时钟（eui 项目的绘制 handler 本来就贯穿 `eui_get_tick_ms()`）。

## 数值等价性

私有移植用的是 permille 整数缓动（`int fn(int t)`，定义域 0..1000）；MotionC 缓动是 Q16.16（`mc_real_t fn(mc_real_t t)`）。已用全程差分对比验证过移植版 `easeOutBack`（`c1 = 1.70158`）：**最大偏差为行程的 4‰**，纯定点舍入——240px 屏上不足 1 像素。端点完全一致。因此移植测试中的中途采样断言改用 ±5‰（行程）容差；端点断言和时序/`is_changed` 断言保持精确。

## 测试计划

- 将 VAMeter 的 `test_transition2d.c` / `test_transition3d.c` 移植为
  `test/test_transition.c`（1D+2D+3D），并补充颜色过渡用例，保留全部时序断言（延迟启动、持久 delay、空操作 retarget、jump/move 之分、按轴独立、reset 状态），含 3D 差异用例（零锚时钟、`>=` 完成判定、按轴 delay）。
- 中途缓动值断言从精确 permille 值改为 ±5‰（行程）容差；端点断言保持精确。
- 新模块维持本仓库 100% 行覆盖率标准。
- CMake：无需接线——`file(GLOB_RECURSE src/*.c)` 自动收编新模块；测试按现有模式登记到 `test/CMakeLists.txt`。

## 集成与迁移计划

消费者按依赖顺序迁移；每一步独立保持绿灯。

1. **MotionC**：落地 `mc_transition` + 测试；push。
2. **eui**：把 `third_party/motionc` 子模块 bump 到新提交。构建系统零改动（CMake `add_subdirectory` 与 ESP-IDF `REQUIRES motionc` 均已存在）。eui 保留 `eui_anim`（dt/push，架在 `mc_animate` 上）服务框架级视图切换动画；两种动画风格并存，边界文档化（框架推、视图拉）。
3. **消费者项目（VAMeter `app-eui`、nfc-kit `fw/app-eui`）**：机械改名迁移，然后删除私有移植：

   | 私有移植 | 之后 |
   |---|---|
   | `eui_transition2d.{c,h}`、`eui_transition3d.{c,h}`、`eui_color_trans*` | 删除；`#include "mc_transition.h"` |
   | `eui_transition_t / 2d_t / 3d_t`、`eui_color_trans_t` | `mc_transition_t / 2d_t / 3d_t`、`mc_color_trans_t` |
   | `eui_ease_linear / out_quad / out_back`（permille） | `mc_ease_linear / quad_out / back_out`（Q16.16） |
   | `eui_rgb888_to_565` | 留在项目侧（显示格式知识） |
   | `update(now_ms)` 调用点与 `eui_get_tick_ms()` 贯穿 | 不变 |

   eui 构建已通过现有 target 链接（`eui PUBLIC mc`）把 MotionC include 路径传递给消费者。

## 风险

- 消费者 smoke 测试中的**中途像素断言**可能因 4‰ 缓动差异位移 <1px。端点精确；任何超差的中途断言放宽 ±1px 并加注释引用本 spec。
- **颜色过渡引入 `is_changed`** 不改变任何可观察行为（私有版哨兵 `is_paused && !is_finish` 等价）；在此注明，避免评审误判为语义变更。
- 本模块与原版 C++ toolkit 的**偏离风险**由移植的回归测试封顶；未来任何语义变更都必须显式更新这些测试。
