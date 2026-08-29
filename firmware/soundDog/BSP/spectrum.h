/**
 * @file    spectrum.h
 * @brief   256 点 FFT 幅度谱 + 32 频带聚合（FEAT-A2-02）
 *
 * 输入：两帧 I2S pcm 拼接的 256 点窗口（16kHz → 窗长 16ms）
 * 处理：均值去直流 → 汉明窗 → fft_run（A2-01）→ 128 bins → 4 bins/频带聚合
 * 输出：32 频带能量（0~4kHz，每带 250Hz；A2-03 OLED / A2-04 串口的输入接口）
 *
 * 上下文：任务级调用（specTask），非 ISR。单次 ~1ms @168MHz。
 */

#ifndef SPECTRUM_H
#define SPECTRUM_H

#include <stdint.h>

#define SPEC_WINDOW    256u                  /* FFT 窗口点数（两帧拼接） */
#define SPEC_BANDS     32u                   /* 输出频带数（128 bins / 4） */
#define SPEC_BAND_HZ   250u                  /* 每频带带宽（62.5Hz/bin × 4） */

void spec_init(void);
/* bands[32]：各频带幅度累加值（整数；打印/显示端自行归一化） */
void spec_process(const int16_t *pcm256, uint32_t *bands);

/* ---- AC-01 证据接口：最近一次 spec_process 的统计结果 ----
 * 峰值幅度/噪声底均为 ×100 整数（nano.specs 无 %f）；
 * 噪声底 = 127 bins（bin1~127）幅度均值（跳过 DC bin0） */
uint32_t spec_peak_bin(void);      /* 峰值 bin（1~127；1kHz 理论值 16） */
uint32_t spec_peak_mag(void);      /* 峰值幅度 ×100 */
uint32_t spec_noise_floor(void);   /* 噪声底（bin 均值）×100 */

/* ---- FEAT-A2-03: 显示快照（specTask 写 / displayTask 读，各临界区） ----
 * spec_display_update：spec_process 后由 specTask 调用（写侧，~几 µs）
 * spec_display_get：displayTask 每 200ms 调用（读侧拷贝 32 字） */
void spec_display_update(const uint32_t *bands);
void spec_display_get(uint32_t *out32);

#endif /* SPECTRUM_H */
