#include "mc_wave.h"
#include "mc_math.h"

/* 1/4 周期 256 点正弦 LUT（Q16.16，含端点），线性插值后幅值 13 时
 * 与 libm sinf 的整数化偏差 < 1。生成式：
 *   round(sin(i * pi/2 / 256) * 65536), i = 0..256 */
static const int32_t k_wave_sin_q16[257] = {
    0, 402, 804, 1206, 1608, 2010, 2412, 2814, 3216, 3617, 4019, 4420, 4821,
    5222, 5623, 6023, 6424, 6824, 7224, 7623, 8022, 8421, 8820, 9218, 9616,
    10014, 10411, 10808, 11204, 11600, 11996, 12391, 12785, 13180, 13573,
    13966, 14359, 14751, 15143, 15534, 15924, 16314, 16703, 17091, 17479,
    17867, 18253, 18639, 19024, 19409, 19792, 20175, 20557, 20939, 21320,
    21699, 22078, 22457, 22834, 23210, 23586, 23961, 24335, 24708, 25080,
    25451, 25821, 26190, 26558, 26925, 27291, 27656, 28020, 28383, 28745,
    29106, 29466, 29824, 30182, 30538, 30893, 31248, 31600, 31952, 32303,
    32652, 33000, 33347, 33692, 34037, 34380, 34721, 35062, 35401, 35738,
    36075, 36410, 36744, 37076, 37407, 37736, 38064, 38391, 38716, 39040,
    39362, 39683, 40002, 40320, 40636, 40951, 41264, 41576, 41886, 42194,
    42501, 42806, 43110, 43412, 43713, 44011, 44308, 44604, 44898, 45190,
    45480, 45769, 46056, 46341, 46624, 46906, 47186, 47464, 47741, 48015,
    48288, 48559, 48828, 49095, 49361, 49624, 49886, 50146, 50404, 50660,
    50914, 51166, 51417, 51665, 51911, 52156, 52398, 52639, 52878, 53114,
    53349, 53581, 53812, 54040, 54267, 54491, 54714, 54934, 55152, 55368,
    55582, 55794, 56004, 56212, 56418, 56621, 56823, 57022, 57219, 57414,
    57607, 57798, 57986, 58172, 58356, 58538, 58718, 58896, 59071, 59244,
    59415, 59583, 59750, 59914, 60075, 60235, 60392, 60547, 60700, 60851,
    60999, 61145, 61288, 61429, 61568, 61705, 61839, 61971, 62101, 62228,
    62353, 62476, 62596, 62714, 62830, 62943, 63054, 63162, 63268, 63372,
    63473, 63572, 63668, 63763, 63854, 63944, 64031, 64115, 64197, 64277,
    64354, 64429, 64501, 64571, 64639, 64704, 64766, 64827, 64884, 64940,
    64993, 65043, 65091, 65137, 65180, 65220, 65259, 65294, 65328, 65358,
    65387, 65413, 65436, 65457, 65476, 65492, 65505, 65516, 65525, 65531,
    65535, 65536,
};

#ifdef MC_USE_FLOAT
static mc_real_t wave_lut(int i) { return (mc_real_t)k_wave_sin_q16[i] / 65536.0f; }
#define WAVE_FP_MUL(a, b) ((a) * (b))
#define WAVE_FP_DIV(a, b) ((a) / (b))
#else
static mc_real_t wave_lut(int i) { return k_wave_sin_q16[i]; }
#define WAVE_FP_MUL(a, b) mc_fp_mul((a), (b))
#define WAVE_FP_DIV(a, b) mc_fp_div((a), (b))
#endif

/* sin(rad)，rad 为 mc_real_t 弧度。LUT 定义在 Q16.16 域，float 模式下
 * 读取时换算回 0..1，两种模式共用同一张表。 */
static mc_real_t wave_sin(mc_real_t rad)
{
    const mc_real_t two_pi = MC_REAL_FROM_FLOAT(6.28318530717958647692);
    const mc_real_t quarter = MC_REAL_FROM_FLOAT(1.57079632679489661923);

    /* 归约到 [0, 2*pi)。注意 mc_fp_div 返回的是**精确分数商**（Q16.16），
     * 必须先取整得到地板商再乘回去，否则 r 会坍缩为 ~0。 */
    mc_real_t r = rad;
    if (r >= two_pi || r <= -two_pi) {
        mc_real_t q = MC_REAL_TO_INT(WAVE_FP_DIV(rad, two_pi));
        r = rad - q * two_pi;
    }
    while (r >= two_pi) r -= two_pi;
    while (r < 0) r += two_pi;

    int quadrant = MC_REAL_TO_INT(WAVE_FP_DIV(r, quarter));

    /* 折回 [0, pi/2)：sin(pi-t)=sin(t)、sin(pi+t)=-sin(t)、sin(2pi-t)=-sin(t) */
    mc_real_t t;
    switch (quadrant) {
    case 1:  t = 2 * quarter - r; break;
    case 2:  t = r - 2 * quarter; break;
    default: t = two_pi - r; break;   /* quadrant 3（r 贴近 2*pi 时可能归到 4） */
    case 0:  t = r; break;
    }
    if (t > quarter) t = quarter;
    if (t < 0) t = 0;

    mc_real_t pos = WAVE_FP_MUL(WAVE_FP_DIV(t, quarter), MC_REAL_FROM_INT(256));
    int i = MC_REAL_TO_INT(pos);
    mc_real_t frac = pos - MC_REAL_FROM_INT(i);
    if (i >= 256) { i = 255; frac = MC_FP_C(1); }
    mc_real_t v = wave_lut(i) + WAVE_FP_MUL(wave_lut(i + 1) - wave_lut(i), frac);

    /* quadrant 贴近 2*pi 时折算为 4：sin(2*pi-e) = -sin(e) 的符号取反 */
    if (quadrant >= 2)
        v = -v;
    return v;
}

void mc_wave_config_default(mc_wave_config_t *cfg)
{
    cfg->a_scale = MC_REAL_FROM_INT(10);
    cfg->a_offset = MC_REAL_FROM_INT(0);
    cfg->b_scale = MC_REAL_FROM_INT(13);
    cfg->b_offset = MC_REAL_FROM_INT(-10);
    cfg->period = MC_REAL_FROM_INT(60);
    cfg->b_shift = 20;
}

int16_t mc_wave_sample_a(const mc_wave_t *wg, int32_t x)
{
    mc_real_t v = wave_sin(MC_REAL_FROM_INT(x) / (wg->cfg.period / MC_FP_SCALE));
    return (int16_t)MC_REAL_TO_INT(WAVE_FP_MUL(v, wg->cfg.a_scale) + wg->cfg.a_offset);
}

int16_t mc_wave_sample_b(const mc_wave_t *wg, int32_t x)
{
    mc_real_t v = wave_sin(MC_REAL_FROM_INT(x - wg->cfg.b_shift) /
                           (wg->cfg.period / MC_FP_SCALE));
    return (int16_t)MC_REAL_TO_INT(WAVE_FP_MUL(v, wg->cfg.b_scale) + wg->cfg.b_offset);
}

/* SUT RingBuffer 的 put（allowOverwrite）：isFull 判定 (w+1)%cap==r，
 * 满时推进 r——容量 N 实际持有 N-1 个样本，off-by-one 语义原样保留。 */
static void wave_ring_put(mc_wave_ring_t *rb, int16_t v)
{
    if (rb->cap == 0) return;
    if ((rb->w + 1) % rb->cap == rb->r)
        rb->r = (rb->r + 1) % rb->cap;
    rb->buf[rb->w] = v;
    rb->w = (rb->w + 1) % rb->cap;
}

void mc_wave_init(mc_wave_t *wg, int16_t *storage_a, int16_t *storage_b,
                  uint32_t length, const mc_wave_config_t *cfg)
{
    if (length == 0 || length > MC_WAVE_MAX_LENGTH)
        return;
    wg->a.cap = wg->b.cap = length;
    wg->a.w = wg->a.r = wg->b.w = wg->b.r = 0;
    wg->a.buf = storage_a;
    wg->b.buf = storage_b;
    if (cfg)
        wg->cfg = *cfg;
    else
        mc_wave_config_default(&wg->cfg);
    for (uint32_t i = 0; i < length; i++) {
        wave_ring_put(&wg->a, mc_wave_sample_a(wg, (int32_t)i));
        wave_ring_put(&wg->b, mc_wave_sample_b(wg, (int32_t)i));
    }
    wg->x = (int32_t)length;
}

void mc_wave_update(mc_wave_t *wg)
{
    wave_ring_put(&wg->a, mc_wave_sample_a(wg, wg->x));
    wave_ring_put(&wg->b, mc_wave_sample_b(wg, wg->x));
    wave_ring_put(&wg->b, mc_wave_sample_b(wg, wg->x));
    wg->x++;
}

uint32_t mc_wave_count(const mc_wave_ring_t *rb)
{
    if (rb->cap == 0) return 0;
    if (rb->w >= rb->r) return rb->w - rb->r;
    return rb->cap - rb->r + rb->w;
}

int16_t mc_wave_at(const mc_wave_ring_t *rb, uint32_t i)
{
    return rb->buf[(rb->r + i) % rb->cap];
}
