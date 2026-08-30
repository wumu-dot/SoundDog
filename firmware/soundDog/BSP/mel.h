/**
 * @file    mel.h
 * @brief   FEAT-A3-02：Mel 滤波器组（400 样加窗帧 → 512 补零 rfft → 32 维 Mel 能量）
 *
 * 数据流位置（设计定稿 §1.5）：
 *   specTask: mfcc_feed() 出帧 → mel_process()（本模块） → 串口 MEL 行（1s 节流）
 *                                    └→ A3-03 消费（Log + DCT-II）
 *
 * 关键参数（FEAT-A3-02 §1.4 / §1.5，禁止自造）：
 *   FFT 512 点（400 样补零 112） / fmin=300Hz / fmax=8000Hz / 32 个三角滤波
 *   采样率 16kHz（A1-04 核实）
 *
 * R27 预研三源结论（附 1/附 2/附 3）：
 *   - 官方 arm_mfcc_f32 整体调用会窗叠窗（自带归一化+加窗，A3-01 帧已加窗）
 *     → 只复用其 Mel 段算法（稀疏 dot_prod，L120-131 同构）；
 *   - 400→512 补零获官方 Issue #279 维护者直接背书；
 *   - 幅度谱口径（非功率谱）与官方一致（#338/#341 印证）。
 */
#ifndef MEL_H
#define MEL_H

#include <stdint.h>

/* ARM_MATH_CM4F / __FPU_PRESENT 由 Makefile 全局定义（A2-01）
 * include 惯例统一随 fft.h/mfcc.h（A3-01 评审 Minor#4） */
#include "arm_math.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- 参数（§1.4/§1.5 定稿） ---- */
#define MEL_FFT_LEN   512u  /* FFT 点数：400 样 + 112 零（官方 #279 背书） */
#define MEL_FRAME_IN  400u  /* 输入帧长 = A3-01 帧长（25ms @ 16kHz） */
#define MEL_NB_BANDS  32u   /* Mel 滤波器个数（= 输出维度） */

/* ---- 接口 ---- */

/** @brief 初始化：512 点 rfft 实例（独立于 fft.c 的 256 点实例——
 *         fft_run 单消费者格局零触碰，设计 §1.5 决策）。
 *  @return 0 成功；-1 初始化失败（arm_rfft_fast_init_f32 非 SUCCESS）
 *  @note   幂等；任务入口调用（先于任何 mel_process）。 */
int mel_init(void);

/** @brief 400 样加窗帧 → 32 维 Mel 能量（幅度谱口径）
 *  @param  frame400  [in]  A3-01 mfcc_last_frame() 输出（400 × float32，已加窗）
 *  @param  mel_out   [out] 32 维 Mel 能量（float32，未取 log——A3-03 边界）
 *  @return 0 成功；-1 参数/初始化异常
 *  @note   内部立即拷贝输入（坑 7：last_frame 为静态缓冲，下一次
 *          mfcc_feed 即覆写，禁止跨迭代持指针）。非重入；
 *          调用者仅 specTask（单消费者约定，同 mfcc_feed）。 */
int mel_process(const float32_t *frame400, float32_t *mel_out);

#ifdef __cplusplus
}
#endif

#endif /* MEL_H */
