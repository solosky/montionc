# MotionC

标准 C99 (ANSI C) 动画引擎，专为无 FPU 或低成本嵌入式 MCU 设计。

## 特点

| 特性 | 说明 |
|------|------|
| **Q16.16 固定点** | 默认 `int32_t` 固定点运算，零浮点性能开销 |
| **MC_USE_FLOAT** | 定义此宏即切换为原生 `float`，适配带 FPU 的 MCU |
| **零 math.h 依赖** | 缓动函数使用整数多项式 + 预计算 LUT，弹簧使用欧拉积分 |
| **外部分配器** | `mc_allocator_set()` 可替换 `malloc/free` 为内存池 |
| **自包含** | 无任何外部依赖，仅需 C99 编译器 |
| **模块化** | 按需包含对应头文件，链接时未使用的代码自动消除 |

## 模块

| 头文件 | 类型/函数 | 说明 |
|--------|-----------|------|
| `mc_types.h` | `mc_real_t`, `MC_FP_C`, `MC_REAL_TO_INT` | Q16.16 固定点 / float 类型及转换宏 |
| `mc_math.h` | `mc_vec2_t`, `mc_vec4_t`, `mc_rect_t`, `mc_clamp`, `mc_lerp`, `mc_fp_mul/div/sqrt` | 向量、矩形、数学工具 |
| `mc_easing.h` | `mc_easing_fn_t`, 30 个缓动函数, `mc_ease_cubic_bezier` | 缓动函数库（多项式 + LUT） |
| `mc_spring.h` | `mc_spring_params_t`, `mc_spring_step` | 弹簧物理模拟（欧拉积分） |
| `mc_animate.h` | `mc_animate_t`, `mc_vec2_animate_t`, `mc_vec4_animate_t` | 标量/二维/四维动画（缓动 + 弹簧双模式） |
| `mc_transition.h` | `mc_transition_t`, `mc_transition2d_t/3d_t`, `mc_color_trans_t` | 平滑 UI Transition 语义（绝对时间 now_ms 驱动、拉取式、延迟启动） |
| `mc_sequence.h` | `mc_sequence_t`, `mc_animate_step_t` | 动画序列，链式步骤 |
| `mc_wave.h` | `mc_wave_t`, `mc_wave_ring_t`, `mc_wave_config_t` | 双通道行波水波样本发生器（双正弦环形缓冲，B 通道 2 倍速；smooth_ui_toolkit WaterWaveGenerator 语义，零 math.h：1/4 周期 LUT 正弦） |
| `mc_color.h` | `mc_rgb_t`, `mc_rgba_t`, hex/HSV 转换, 颜色混合 | RGB888 颜色工具 |
| `mc_hal.h` | `mc_hal_set_tick_callback`, `mc_get_tick`, `mc_delay` | tick/delay 抽象层 |
| `mc_ringbuf.h` | `mc_ringbuf_t` | 环形缓冲区 |
| `mc_signal.h` | `mc_signal_t`, `mc_slot_fn` | 观察者模式 |
| `mc_pool.h` | `mc_pool_t` | 对象池 |
| `mc_allocator.h` | `mc_allocator_t`, `mc_allocator_set` | 外部分配器接口 |
| `mc_errors.h` | `MC_OK`, `MC_ERR_NOMEM`, `MC_ERR_FULL` | 错误码 |
| `mc_config.h` | `MC_USE_FLOAT` | 编译时配置开关 |

## mc_transition vs mc_animate

- `mc_animate`：dt 驱动、push 式（on_update 回调），服务补间序列、弹簧物理、repeat/loop。
- `mc_transition`：绝对时间驱动（`update(now_ms)`）、拉取式（随时读 x()/y()），
  服务立即式 UI 元素动画。位置是墙钟时间的纯函数，掉帧不影响轨迹。
  语义契约见 `docs/superpowers/specs/2026-09-21-mc-transition-design.md`。

## 类型系统

### 实数类型

```c
// 默认：Q16.16 固定点，1 表示为 65536
typedef int32_t mc_real_t;
#define MC_FP_SCALE  65536
#define MC_FP_C(v)   ((mc_real_t)((v) * MC_FP_SCALE))

// 启用 MC_USE_FLOAT 后：
typedef float mc_real_t;
#define MC_FP_C(v)   (v)
```

### 转换宏

| 宏 | 说明 |
|----|------|
| `MC_FP_C(v)` | 编译时常量转换，如 `MC_FP_C(1.5)` |
| `MC_REAL_FROM_INT(n)` | 整数 → mc_real_t |
| `MC_REAL_FROM_FLOAT(f)` | float → mc_real_t |
| `MC_REAL_TO_FLOAT(r)` | mc_real_t → float |
| `MC_REAL_TO_INT(r)` | mc_real_t → int（截断） |

## 动画 API

### 标量动画

```c
#include "mc.h"

// ---- 缓动模式 ----
mc_animate_t anim;
mc_animate_init_easing(&anim, MC_FP_C(0), MC_FP_C(10), MC_FP_C(1)); // 0→10, 1s
anim.config.easing.easing = mc_ease_cubic_out;
anim.repeat = 255; // 无限循环

while (1) {
    bool done = mc_animate_update(&anim, MC_FP_C(1.0f / 60));
    int value = MC_REAL_TO_INT(anim.current);
    // use value...
}

// ---- 弹簧模式 ----
mc_animate_init_spring(&anim, MC_FP_C(100), MC_FP_C(600));
anim.config.spring.stiffness = MC_FP_C(120);
anim.config.spring.damping = MC_FP_C(10);

while (1) {
    mc_animate_update(&anim, MC_FP_C(1.0f / 60));
    int value = MC_REAL_TO_INT(anim.current);
}
```

回调支持：

```c
anim.on_update = my_update_callback;    // 每帧回调，传入当前值
anim.on_complete = my_complete_callback; // 动画完成时回调
anim.ctx = &my_context;                  // 用户上下文
```

### 二维向量动画

```c
mc_vec2_animate_t va;
mc_vec2_animate_init_spring(&va,
    mc_vec2(MC_FP_C(100), MC_FP_C(200)),
    mc_vec2(MC_FP_C(500), MC_FP_C(200)));

// 动态改变目标
mc_vec2_animate_move_to(&va, mc_vec2(MC_FP_C(300), MC_FP_C(400)));

while (1) {
    mc_vec2_animate_update(&va, MC_FP_C(1.0f / 60));
    int x = MC_REAL_TO_INT(va.current.x);
    int y = MC_REAL_TO_INT(va.current.y);
}
```

### 四维向量动画（如 RGBA 颜色过渡）

```c
mc_vec4_animate_t cv;
mc_vec4_animate_init_easing(&cv,
    mc_vec4(MC_FP_C(255), MC_FP_C(0), MC_FP_C(0), MC_FP_C(255)),
    mc_vec4(MC_FP_C(0), MC_FP_C(255), MC_FP_C(0), MC_FP_C(128)),
    MC_FP_C(1.5));
cv.config.easing.easing = mc_ease_sine_in_out;

while (1) {
    mc_vec4_animate_update(&cv, MC_FP_C(1.0f / 60));
    uint8_t r = MC_REAL_TO_INT(cv.current.x);
    uint8_t g = MC_REAL_TO_INT(cv.current.y);
    uint8_t b = MC_REAL_TO_INT(cv.current.z);
    uint8_t a = MC_REAL_TO_INT(cv.current.w);
}
```

### 动画序列

```c
mc_sequence_t seq;
mc_sequence_init(&seq, 10);

mc_sequence_push(&seq, (mc_animate_step_t){ MC_FP_C(100), MC_FP_C(0.5), mc_ease_quad_out });
mc_sequence_push(&seq, (mc_animate_step_t){ MC_FP_C(300), MC_FP_C(0.8), mc_ease_bounce_out });
mc_sequence_push(&seq, (mc_animate_step_t){ MC_FP_C(500), MC_FP_C(0.6), mc_ease_elastic_out });

seq.on_step = my_step_callback;
seq.on_complete = my_complete_callback;

mc_sequence_play(&seq);

while (1) {
    bool done = mc_sequence_update(&seq, MC_FP_C(1.0f / 60));
}
```

## 缓动函数

### 多项式（始终可用，无需 math.h）

| 函数 | 说明 |
|------|------|
| `mc_ease_linear` | 线性 |
| `mc_ease_quad_in/out/in_out` | 二次方 |
| `mc_ease_cubic_in/out/in_out` | 三次方 |
| `mc_ease_quart_in/out/in_out` | 四次方 |
| `mc_ease_quint_in/out/in_out` | 五次方 |

### LUT 预计算（固定点模式，零 math.h 依赖）

| 函数 | 说明 |
|------|------|
| `mc_ease_sine_in/out/in_out` | 正弦 |
| `mc_ease_expo_in/out/in_out` | 指数 |
| `mc_ease_circ_in/out/in_out` | 圆形 |
| `mc_ease_back_in/out/in_out` | 回退 |
| `mc_ease_elastic_in/out/in_out` | 弹性 |
| `mc_ease_bounce_in/out/in_out` | 弹跳 |

### 通用

| 函数 | 说明 |
|------|------|
| `mc_ease_cubic_bezier(t, x1, y1, x2, y2)` | 三次贝塞尔曲线，参数同 CSS |

## 弹簧物理

```c
mc_real_t x = MC_FP_C(0);
mc_spring_state_t state = { 0 };
mc_spring_params_t params = {
    .stiffness = MC_FP_C(100),
    .damping = MC_FP_C(10),
    .mass = MC_FP_C(1)
};

// 每帧调用
mc_spring_step(&x, &state, &params, MC_FP_C(10), MC_FP_C(1.0f / 60));
```

## 水波发生器（mc_wave）

双通道行波样本发生器（smooth_ui_toolkit WaterWaveGenerator 语义）：A/B 两条正弦波填入环形样本缓冲，每次 update A 推进一列、B 推进两列（B 滚动速度两倍）。零 math.h 依赖（1/4 周期 LUT 正弦）；样本存储由调用方提供。

```c
#include "mc_wave.h"

static int16_t wave_a[240], wave_b[240];   /* 调用方存储，容量 = length */

mc_wave_t wave;
mc_wave_config_t cfg;
mc_wave_config_default(&cfg);              /* a 10/0, b 13/-10, period 60, shift 20 */
// cfg.a_scale = MC_FP_C(8);               /* 可按需覆盖 */

mc_wave_init(&wave, wave_a, wave_b, 240, &cfg);

// 渲染前推进（如每 10ms 一次）
mc_wave_update(&wave);

// 逐列取样本画竖线：count 为有效样本数（环满时 = length-1，
// SUT RingBuffer off-by-one 语义），at(i) 从最老样本数起
for (uint32_t i = 0; i < mc_wave_count(&wave.a); i++) {
    int16_t ya = mc_wave_at(&wave.a, i);
    draw_vline(i, ya + y_offset);
}
```

通道公式：`A(x) = sin(x/period)*a_scale + a_offset`，`B(x) = sin((x-b_shift)/period)*b_scale + b_offset`；`mc_wave_sample_a/b()` 可对任意 x 求值（测试/外推用）。

## 颜色工具

```c
mc_rgb_t c = mc_rgb_from_hex(0xFF8800);
uint32_t hex = mc_rgb_to_hex(c);

mc_rgb_t hsv = mc_rgb_from_hsv(MC_FP_C(180), MC_FP_C(1), MC_FP_C(1));

mc_rgb_t blend = mc_rgb_blend_opacity(bg, fg, MC_FP_C(0.5));
mc_rgb_t diff = mc_rgb_blend_difference(bg, fg);
```

## 数学工具

```c
mc_real_t v = mc_clamp(val, MC_FP_C(0), MC_FP_C(100));
mc_real_t v = mc_lerp(a, b, MC_FP_C(0.5));        // 线性插值
mc_real_t v = mc_map(x, in_a, in_b, out_a, out_b); // 范围映射

mc_real_t p = mc_fp_mul(a, b);    // 固定点乘法
mc_real_t q = mc_fp_div(a, b);    // 固定点除法
mc_real_t s = mc_fp_sqrt(v);      // 固定点平方根

mc_vec2_t v2 = mc_vec2(MC_FP_C(1), MC_FP_C(2));
mc_vec2_t sum = mc_vec2_add(a, b);
mc_real_t dot = mc_vec2_dot(a, b);
mc_real_t len = mc_vec2_length(a);
mc_vec2_t norm = mc_vec2_normalize(a);

mc_rect_t r = { .x=0, .y=0, .w=MC_FP_C(100), .h=MC_FP_C(50) };
bool hit = mc_rect_contains(r, point);
bool overlap = mc_rect_overlaps(a, b);
```

## 数据结构

### 环形缓冲区

```c
mc_ringbuf_t rb;
mc_ringbuf_init(&rb, sizeof(int), 64);

int v = 42;
mc_ringbuf_push(&rb, &v);
mc_ringbuf_pop(&rb, &v);

rb.overwrite = 1; // 满时覆盖旧数据
```

### 信号/槽

```c
void on_click(void* ctx, void* args) {
    int* val = (int*)args;
    // handle event
}

mc_signal_t sig;
mc_signal_init(&sig, 8);
mc_signal_connect(&sig, on_click, NULL);

int arg = 123;
mc_signal_emit(&sig, &arg);

mc_signal_disconnect(&sig, on_click, NULL);
mc_signal_deinit(&sig);
```

### 对象池

```c
typedef struct { int value; } my_obj_t;

void ctor(void* obj, void* ctx) {
    ((my_obj_t*)obj)->value = *(int*)ctx;
}

mc_pool_t pool;
mc_pool_init(&pool, sizeof(my_obj_t), 10);

int init_val = 42;
my_obj_t* obj = mc_pool_acquire(&pool, ctor, &init_val);

mc_pool_release(&pool, obj);
mc_pool_deinit(&pool);
```

## HAL 抽象

```c
// 自定义实现
uint32_t my_tick(void) { return millis(); }
void my_delay(uint32_t ms) { delay(ms); }

mc_hal_set_tick_callback(my_tick);
mc_hal_set_delay_callback(my_delay);

uint32_t now = mc_get_tick();
mc_delay(100);
```

默认实现为空（返回 0 / 无操作），必须设置回调后才可使用。

## 外部分配器

```c
// 替换为内存池分配器
static mc_allocator_t my_alloc = {
    .alloc = my_pool_alloc,
    .free = my_pool_free,
    .ctx = &my_pool_instance
};
mc_allocator_set(&my_alloc);
```

环形缓冲区、对象池、信号、序列均通过 `mc_malloc`/`mc_free` 分配内存。

## 编译

```bash
# 默认固定点模式 (无 FPU MCU)
gcc -std=c99 -Iinclude -c src/mc_math.c

# 浮点模式 (带 FPU 的 MCU)
gcc -std=c99 -DMC_USE_FLOAT -Iinclude -c src/mc_math.c
```

### CMake 构建

```bash
cmake -B build
cmake --build build -j
```

### CMake 选项

| 选项 | 默认 | 说明 |
|------|------|------|
| `MC_BUILD_TEST` | ON | 构建单元测试 |
| `MC_BUILD_EXAMPLE` | ON | 构建 raylib 示例 |

## 运行测试

```bash
cmake -B build && cmake --build build -j
cd build && ctest
```

输出示例：
```
100% tests passed, 0 tests failed out of 10
```

## raylib 示例

需先初始化 raylib 子模块：

```bash
git submodule update --init --depth 1 --branch 5.5 third_party/raylib
cmake -B build -DMC_BUILD_EXAMPLE=ON
cmake --build build -j
```

可用示例：

| 示例 | 说明 |
|------|------|
| `./build/example/basic_animate` | 基础缓动动画（cubic_in_out x, bounce_out y） |
| `./build/example/animate` | 弹簧动画，目标来回切换 |
| `./build/example/animate_value` | 点击移动小球到鼠标位置（弹簧模式） |
| `./build/example/animate_value_opts` | 不同弹簧参数演示 |
| `./build/example/multi_cursor` | 六个鼠标指针跟随，不同刚度/阻尼 |
| `./build/example/bubbles` | 200 个气泡粒子跟随鼠标 |
| `./build/example/easing_curves` | 缓动曲线可视化，方向键切换 |
| `./build/example/comprehensive_test` | 综合演示：缓动画廊/弹簧/vec2/vec4/序列/贝塞尔 |

## 库引入

### CMake 工程

```cmake
add_subdirectory(path/to/motionc)
target_link_libraries(your_project PUBLIC mc)
```

### ESP-IDF 工程

clone 仓库，丢到 `components` 目录即可，构建系统会自动注册（检测 `ESP_PLATFORM`）。

### PlatformIO 工程

clone 仓库，丢到 `lib` 目录即可。

### Arduino 工程

clone 仓库，丢到 `libraries` 目录即可。

## 设计文档

详细设计与移植计划见 [docs/superpowers/](docs/superpowers/)。

## 许可证

MIT License, Copyright (c) 2023 Forairaaaaa
