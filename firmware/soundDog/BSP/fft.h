/**
 * @file    fft.h
 * @brief   CMSIS-DSP 实数 FFT 封装（FEAT-A2-01）
 *
 * 依赖：firmware/cmsis-dsp/（官方 v1.14.4 vendored，仅保留 FFT 所需子集）
 * 上下文：任务级调用（非 ISR）。fft_run 内部约 0.5ms @168MHz。
 * ⚠️ fft_run 不可重入（内部静态输出缓冲）+ fft_ctx_get() 全局共享实例——
 * 仅允许单一消费者调用（当前为 specTask；A2-03/04 若新增调用方须先加串行化）。
 *
 * 用法（A2-01 自测示例）：
 *   fft_ctx_t ctx;
 *   fft_init(&ctx);
 *   fft_run(&ctx, in256, mag128);   // mag[128] = |X[0..127]|，bin k ↔ k*62.5Hz
 */

#ifndef FFT_H
#define FFT_H

#include "arm_math.h"

#define FFT_LEN        256u   /* 实数输入点数（A2-02 两帧拼窗） */
#define FFT_BINS       128u   /* 有效输出 bin 数（0 ~ Fs/2） */
#define FFT_FS         16000.0f  /* A1-04 实测采样率 */
#define FFT_BIN_HZ     (FFT_FS / (float)FFT_LEN)  /* 62.5 Hz/bin */

typedef struct {
    arm_rfft_fast_instance_f32 inst;   /* CMSIS-DSP 实数 FFT 实例 */
} fft_ctx_t;

int fft_init(fft_ctx_t *ctx);
int fft_run(fft_ctx_t *ctx, const float32_t *in, float32_t *mag);

/* A2-02：全局共享实例（spec_init 内初始化；自测与真音频共用） */
fft_ctx_t *fft_ctx_get(void);

#endif /* FFT_H */
