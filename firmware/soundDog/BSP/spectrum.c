/**
 * @file    spectrum.c
 * @brief   256 点 FFT 幅度谱 + 32 频带聚合实现（FEAT-A2-02）
 *
 * 流程（规格 §3.6 A2-02）：
 *   1. 均值去直流（防 peak 落 bin0）
 *   2. 汉明窗（系数 boot 时运行时生成，用 libm cosf，省 flash）
 *   3. int16 → float32 定标（÷32768，避免溢出且幅度规一）
 *   4. fft_run → mag[128]（A2-01 封装）
 *   5. 每 4 bins 聚合 1 频带（幅度累加，"能量"语义，A2-04 串口格式预演）
 */

#include "spectrum.h"
#include "fft.h"
#include <math.h>

static float32_t win[FFT_LEN];      /* 汉明窗系数 */
static float32_t fft_in[FFT_LEN];   /* 加窗后输入 */

/* 最近一次统计结果（AC-01 证据，getter 见 spectrum.h） */
static uint32_t s_peak_bin;
static uint32_t s_peak_x100;
static uint32_t s_noise_x100;

void spec_init(void)
{
    /* 汉明窗：w[n] = 0.54 - 0.46*cos(2*pi*n/(N-1)) */
    for (int n = 0; n < FFT_LEN; n++) {
        win[n] = 0.54f - 0.46f * cosf(2.0f * 3.14159265f * (float)n / (float)(FFT_LEN - 1));
    }
}

void spec_process(const int16_t *pcm256, uint32_t *bands)
{
    if ((pcm256 == NULL) || (bands == NULL)) {
        return;
    }

    /* 1. 均值去直流 */
    int32_t mean = 0;
    for (int i = 0; i < FFT_LEN; i++) {
        mean += pcm256[i];
    }
    /* ⚠️ 陷阱修复（2026-08-29 实测确诊）：FFT_LEN 是 256u（unsigned），
     * "mean /= FFT_LEN" 会把负的 mean 隐式转为 unsigned 再除——
     * 负直流偏移期 mean=-918 会变成 2^24-918=16776298，污染整个频谱
     * （恒定假峰 bin1 + 幅度超数学上限）。必须显式带符号除法。 */
    mean /= (int32_t)FFT_LEN;

    /* 2+3. 加窗 + int16→float32（÷32768 定标） */
    for (int i = 0; i < FFT_LEN; i++) {
        fft_in[i] = ((float)(pcm256[i] - mean) / 32768.0f) * win[i];
    }

    /* 4. FFT + 幅度谱 → mag[128]（fft_run 内部静态缓冲） */
    static float32_t mag[FFT_BINS];
    fft_ctx_t *ctx = fft_ctx_get();
    fft_run(ctx, fft_in, mag);

    /* 5. 峰值 bin + 噪声底（AC-01 证据；跳过 DC bin0，去直流后仍可能有残余）
     * 噪声底用全 bin 均值近似——1kHz 单音时其余 127 bin 即噪声底 */
    {
        float32_t sum = 0.0f;
        float32_t mx  = mag[1];
        int      pk   = 1;
        for (int k = 1; k < FFT_BINS; k++) {
            sum += mag[k];
            if (mag[k] > mx) { mx = mag[k]; pk = k; }
        }
        s_peak_bin  = (uint32_t)pk;
        s_peak_x100 = (uint32_t)(mx * 100.0f + 0.5f);
        s_noise_x100 = (uint32_t)(sum / (float)(FFT_BINS - 1) * 100.0f + 0.5f);
    }

    /* 6. 4 bins 聚合 1 频带：×100 整数化（幅度已定标 0~1 区间，直接累加会全 0） */
    for (int b = 0; b < (int)SPEC_BANDS; b++) {
        float32_t acc = 0.0f;
        for (int k = 0; k < 4; k++) {
            acc += mag[b * 4 + k];
        }
        bands[b] = (uint32_t)(acc * 100.0f + 0.5f);
    }
}

uint32_t spec_peak_bin(void)    { return s_peak_bin; }
uint32_t spec_peak_mag(void)    { return s_peak_x100; }
uint32_t spec_noise_floor(void) { return s_noise_x100; }

/* ---- FEAT-A2-03: 显示快照 ----
 * 写侧（specTask，~62Hz）与读侧（displayTask，5Hz）各自进临界区，
 * 读写窗口均为 32 字 memcpy（µs 级），冲突概率可忽略，无需互斥量。 */
static uint32_t s_disp_bands[SPEC_BANDS];
static uint8_t  s_disp_valid = 0u;

void spec_display_update(const uint32_t *bands)
{
    if (bands == NULL) { return; }
    uint32_t primask = __get_PRIMASK();
    __disable_irq();
    memcpy(s_disp_bands, bands, sizeof(s_disp_bands));
    s_disp_valid = 1u;
    __set_PRIMASK(primask);
}

void spec_display_get(uint32_t *out32)
{
    if (out32 == NULL) { return; }
    uint32_t primask = __get_PRIMASK();
    __disable_irq();
    if (s_disp_valid) {
        memcpy(out32, s_disp_bands, sizeof(s_disp_bands));
    }
    __set_PRIMASK(primask);
}
