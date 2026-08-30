/**
 * @file    mfcc_dct.h
 * @brief   FEAT-A3-03：Log 能量 + DCT-II（32 维 Mel 能量 → 13 维 MFCC）
 *
 * 数据流位置（设计定稿 §1.5）：
 *   specTask: mfcc_feed() 出帧 → mel_frame_cb → mel_process()（A3-02）
 *                                          └→ mfcc_dct_process()（本模块）
 *
 * 关键参数（FEAT-A3-03 §1.4/§1.5，禁止自造）：
 *   ln（自然对数） + ε=1e-6 偏移 / DCT-II 13×32 矩阵 sqrt(2/N)=0.25 /
 *   log 前 Mel 域 absmax 归一化（音量鲁棒，阶段 2 决策）
 *
 * R27 预研三源（§1.5 第八节）：
 *   - 源码：官方 arm_mfcc_f32.c L133-143（ln + 1e-6 + 13×32 矩阵乘三段式）；
 *   - Issue #54：维护者实测 CMSIS≈TF 逐值一致，sqrt(2/N) "almost orthogonal" 口径；
 *   - 坑：arm_mat_vec_mult_f32 未 vendored → 自写 416-MAC 循环（对策 B）；
 *     与官方 TF 不可逐值对照（归一化位置不同）→ PC 复刻自洽判据。
 */
#ifndef MFCC_DCT_H
#define MFCC_DCT_H

#include <stdint.h>

/* ARM_MATH_CM4F / __FPU_PRESENT 由 Makefile 全局定义（A2-01）
 * include 惯例统一随 fft.h/mfcc.h/mel.h（A3-01 评审 Minor#4） */
#include "arm_math.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- 参数（§1.4/§1.5 定稿） ---- */
#define MFCC_DCT_IN  32u   /* 输入维度 = A3-02 Mel 维度 */
#define MFCC_DCT_OUT 13u   /* 输出维度 = 事实包：前 13 维（k=0..12） */

/* ---- 接口 ---- */

/** @brief 32 维 Mel 能量 → 13 维 MFCC（log 域 + DCT-II）
 *  @param  mel32   [in]  A3-02 mel_process() 输出（32 × float32，未取 log）
 *  @param  mfcc13  [out] 13 维 MFCC 系数（float32）
 *  @return 0 成功；-1 参数空
 *  @note   纯函数（表 const 无状态 → 无 init，对比 mel_init 无 FFT 实例需求）。
 *          非重入（内部静态 log 域缓冲）；调用者仅 specTask（单消费者约定，
 *          同 mfcc_feed/mel_process）。 */
int mfcc_dct_process(const float32_t *mel32, float32_t *mfcc13);

#ifdef __cplusplus
}
#endif

#endif /* MFCC_DCT_H */
