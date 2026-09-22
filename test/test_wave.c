/*
 * test_wave.c — mc_wave dual-channel water wave generator.
 *
 * 参照实现即 smooth_ui_toolkit WaterWaveGenerator 的公式（sinf 直算），
 * 断言 LUT 实现与其整数化结果一致（±1）。
 */
#include <stdio.h>
#include <math.h>
#include "mc_wave.h"

static int tests_run = 0, tests_passed = 0;
#define TEST(n) do { tests_run++; printf("  %s ... ", n); } while(0)
#define PASS() do { tests_passed++; printf("PASS\n"); } while(0)
#define CHECK(c) do { if (!(c)) { printf("FAIL at %d\n", __LINE__); return 1; } } while(0)

/* SUT 公式参照（VAMeter wp_wave_a/b_formula 的直算版） */
static int ref_a(int x) { return (int)(sinf(x / 60.0f) * 10.0f + 0.0f); }
static int ref_b(int x) { return (int)(sinf((x - 20) / 60.0f) * 13.0f - 10.0f); }

static int test_formula_matches_reference() {
    TEST("sample_a/sample_b match SUT sinf formulas within +-1");
    mc_wave_t wg;
    mc_wave_config_t cfg;
    mc_wave_config_default(&cfg);
    int16_t ba[384], bb[384];
    mc_wave_init(&wg, ba, bb, 384, &cfg);

    int max_err_a = 0, max_err_b = 0;
    for (int x = -20; x < 2000; x++) {
        int ea = mc_wave_sample_a(&wg, x) - ref_a(x);
        if (ea < 0) ea = -ea;
        if (ea > max_err_a) max_err_a = ea;
        int eb = mc_wave_sample_b(&wg, x) - ref_b(x);
        if (eb < 0) eb = -eb;
        if (eb > max_err_b) max_err_b = eb;
    }
    CHECK(max_err_a <= 1);
    CHECK(max_err_b <= 1);
    PASS(); return 0;
}

static int test_ring_semantics() {
    TEST("ring holds length-1 samples; update pushes a=1 b=2 with wrap");
    mc_wave_t wg;
    mc_wave_config_t cfg;
    mc_wave_config_default(&cfg);
    int16_t ba[64], bb[64];
    mc_wave_init(&wg, ba, bb, 64, &cfg);

    CHECK(mc_wave_count(&wg.a) == 63);          /* off-by-one：容量 64 持有 63 */
    CHECK(mc_wave_count(&wg.b) == 63);
    /* init 推入 x=0..63 共 64 次，环满驱逐最老（x=0）——持有 x=1..63，
     * at(i) == formula(i+1)（SUT RingBuffer 语义的忠实结果） */
    for (uint32_t i = 0; i < 63; i++) {
        CHECK(mc_wave_at(&wg.a, i) == ref_a((int)i + 1));
        CHECK(mc_wave_at(&wg.b, i) == ref_b((int)i + 1));
    }

    /* x=64 一次 update：A 推一次、B 同 x 推两次，x 变 65 */
    mc_wave_update(&wg);
    CHECK(wg.x == 65);
    CHECK(mc_wave_at(&wg.a, 62) == ref_a(64));  /* A 最新 = 新推入 */
    CHECK(mc_wave_at(&wg.b, 62) == ref_b(64));  /* B 最新 = 第二次推入 */
    CHECK(mc_wave_at(&wg.b, 61) == ref_b(64));  /* B 次新 = 第一次推入（同 x） */

    /* 持续更新不越界，环保持 length-1 */
    for (int i = 0; i < 300; i++)
        mc_wave_update(&wg);
    CHECK(mc_wave_count(&wg.a) == 63);
    CHECK(mc_wave_count(&wg.b) == 63);
    PASS(); return 0;
}

static int test_b_shift_anchor() {
    TEST("b channel at x=b_shift equals b_offset (sin(0)=0)");
    mc_wave_t wg;
    mc_wave_config_t cfg;
    mc_wave_config_default(&cfg);
    int16_t ba[8], bb[8];
    mc_wave_init(&wg, ba, bb, 8, &cfg);
    CHECK(mc_wave_sample_b(&wg, 20) == -10);
    CHECK(mc_wave_sample_a(&wg, 0) == 0);
    PASS(); return 0;
}

static int test_custom_config() {
    TEST("custom config scales are respected");
    mc_wave_t wg;
    mc_wave_config_t cfg;
    mc_wave_config_default(&cfg);
    cfg.a_scale = MC_REAL_FROM_INT(0);       /* A 通道归零 */
    cfg.b_scale = MC_REAL_FROM_INT(1);
    cfg.b_offset = MC_REAL_FROM_INT(0);
    int16_t ba[8], bb[8];
    mc_wave_init(&wg, ba, bb, 8, &cfg);
    for (int x = 0; x < 8; x++)
        CHECK(mc_wave_sample_a(&wg, x) == 0);
    CHECK(mc_wave_sample_b(&wg, 100) >= 0);  /* sin(80/60) > 0, scale 1 */
    CHECK(mc_wave_sample_b(&wg, 100) <= 1);
    PASS(); return 0;
}

int main(void) {
    printf("mc_wave tests:\n");
    if (test_formula_matches_reference()) return 1;
    if (test_ring_semantics()) return 1;
    if (test_b_shift_anchor()) return 1;
    if (test_custom_config()) return 1;
    printf("%d/%d tests passed\n", tests_passed, tests_run);
    return tests_passed == tests_run ? 0 : 1;
}
