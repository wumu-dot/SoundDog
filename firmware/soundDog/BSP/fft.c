/**
 * @file    fft.c
 * @brief   CMSIS-DSP 实数 FFT 封装实现（FEAT-A2-01）
 *
 * 实现要点：
 *   - arm_rfft_fast_f32：256 点实数 FFT，输出 128 个复数（交织 re,im）
 *   - arm_cmplx_mag_f32：求模 → mag[k] = |X[k]|，k=0..127
 *   - ARM_MATH_CM4F 已在 Makefile 定义，走 M4F+FPU 优化路径
 */

#include "fft.h"

int fft_init(fft_ctx_t *ctx)
{
    if (ctx == NULL) {
        return -1;
    }
    if (arm_rfft_fast_init_f32(&ctx->inst, FFT_LEN) != ARM_MATH_SUCCESS) {
        return -2;
    }
    return 0;
}

int fft_run(fft_ctx_t *ctx, const float32_t *in, float32_t *mag)
{
    /* rfft_fast 输出缓冲需要 FFT_LEN 个 float（128 复数 × 2） */
    static float32_t out[FFT_LEN];

    if ((ctx == NULL) || (in == NULL) || (mag == NULL)) {
        return -1;
    }

    arm_rfft_fast_f32(&ctx->inst, (float32_t *)in, out, 0 /*forward*/);
    arm_cmplx_mag_f32(out, mag, FFT_BINS);

    return 0;
}

/* A2-02：全局共享实例（spec_init / fft_selftest 共用，初始化一次） */
static fft_ctx_t g_fft_ctx;

fft_ctx_t *fft_ctx_get(void)
{
    return &g_fft_ctx;
}
