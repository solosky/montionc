#ifndef MC_WAVE_H
#define MC_WAVE_H

#include "mc_types.h"
#include <stdint.h>

/**
 * @file mc_wave.h
 * @brief Dual-channel travelling water-wave sample generator.
 *
 * smooth_ui_toolkit WaterWaveGenerator 语义的 C 移植（经 VAMeter 固件
 * 审计过的版本）：双正弦通道 A/B 填充环形样本缓冲，B 每次更新推进两列
 * （滚动速度是 A 的两倍）。通道公式：
 *
 *   A(x) = sin(x / period) * a_scale + a_offset
 *   B(x) = sin((x - b_shift) / period) * b_scale + b_offset
 *
 * 默认参数（SUT Config_t 默认值）：a_scale=10, a_offset=0,
 * b_scale=13, b_offset=-10, period=60, b_shift=20。
 *
 * 零 math.h 依赖：sin 由 1/4 周期 256 点 LUT + 线性插值实现（定点
 * Q16.16 / float 双模式），幅值 13 时与 libm sinf 的整数化偏差 < 1。
 *
 * 存储由调用方提供（motionc 惯例：外部分配）。环形缓冲保留 SUT
 * RingBuffer 的 off-by-one 语义——容量 N 实际持有 N-1 个样本。
 */

#define MC_WAVE_MAX_LENGTH 4096u

typedef struct {
    mc_real_t a_scale;   /**< A 通道幅值，默认 10 */
    mc_real_t a_offset;  /**< A 通道偏置，默认 0 */
    mc_real_t b_scale;   /**< B 通道幅值，默认 13 */
    mc_real_t b_offset;  /**< B 通道偏置，默认 -10 */
    mc_real_t period;    /**< 采样步进弧度除数（sin(x/period)），默认 60 */
    int32_t   b_shift;   /**< B 通道 x 平移，默认 20 */
} mc_wave_config_t;

typedef struct {
    int16_t *buf;        /**< 调用方存储，容量 == length */
    uint32_t cap;        /**< 容量（== length） */
    uint32_t w, r;       /**< 写/读游标（SUT RingBuffer 语义） */
} mc_wave_ring_t;

typedef struct {
    mc_wave_ring_t a, b;
    int32_t x;                    /**< 样本计数器，init 后从 length 续计 */
    mc_wave_config_t cfg;
} mc_wave_t;

/** @brief 填充 SUT 默认配置（10/0/13/-10/60/20）。 */
void mc_wave_config_default(mc_wave_config_t *cfg);

/**
 * @brief Initialize with caller storage and fill a full period of samples.
 *
 * length 1..MC_WAVE_MAX_LENGTH；初始化后两环各持有 length-1 个样本，
 * x 从 length 续计（对齐 SUT init(waveLenght) 语义）。
 */
void mc_wave_init(mc_wave_t *wg, int16_t *storage_a, int16_t *storage_b,
                  uint32_t length, const mc_wave_config_t *cfg);

/** @brief Push one sample to A and two to B ("put wave b twice to make it faster"). */
void mc_wave_update(mc_wave_t *wg);

/** @brief Valid sample count（环满时为 cap-1）。 */
uint32_t mc_wave_count(const mc_wave_ring_t *rb);

/** @brief i-th valid sample counting from the oldest. */
int16_t mc_wave_at(const mc_wave_ring_t *rb, uint32_t i);

/** @brief 通道公式求值（测试/外推用）。 */
int16_t mc_wave_sample_a(const mc_wave_t *wg, int32_t x);
int16_t mc_wave_sample_b(const mc_wave_t *wg, int32_t x);

#endif /* MC_WAVE_H */
