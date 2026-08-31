/**
 * @file    dist.h
 * @brief   FEAT-A4-02：距离实时比对（13 维 MFCC × 16 模版 → 最小欧氏距离 d_min）
 *
 * 数据流位置（设计定稿 §1.5）：
 *   specTask: mfcc_feed() 出帧 → mel_frame_cb → mel_process()（A3-02）
 *                                          └→ mfcc_dct_process()（A3-03）
 *                                          └→ dist_process()（本模块）→ d_min
 *
 * 关键参数（FEAT-A4-02 §1.4/§1.5，禁止自造）：
 *   欧氏距离（模版=sklearn 欧氏质心，度量同空间）/ 平方距离比较免 16 次 sqrt /
 *   逐帧 100 帧/s（mel_frame_cb 同网格）
 *
 * R27 预研（§1.5 R29 四件套之①）：
 *   - 源码：官方 arm_euclidean_distance_f32 标量路径（平方差累加+sqrt，纯函数），
 *     本地未 vendored DistanceFunctions .c → 沿 A3-03 对策 B 先例自写同构算法；
 *   - 模式：Edge Impulse K-means 块 "nearest centroid distance" 官方模式；
 *   - 坑：见 FEAT §1.5 二（度量一致性/sqrt 次数/gc-sections/撞行/定标/登记/门控）。
 */
#ifndef DIST_H
#define DIST_H

#include <stdint.h>

/* ARM_MATH_CM4F / __FPU_PRESENT 由 Makefile 全局定义（A2-01） */
#include "arm_math.h"

/* 模版库：A4-01 产物（编译期常量，16×13） */
#include "model_normal.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- 接口 ---- */

/** @brief 13 维 MFCC × 16 模版 → 最小欧氏距离 d_min
 *  @param  mfcc13  [in]  A3-03 mfcc_dct_process() 输出（13 × float32）
 *  @param  d_min    [out] 与 16 个模版的最小欧氏距离（float32）
 *  @return 0 成功；-1 参数空
 *  @note   纯函数（模版 const 无状态 → 无 init，同 mfcc_dct）。
 *          非重入；调用者仅 specTask（单消费者约定，同 mel_process/mfcc_dct_process）。 */
int dist_process(const float32_t *mfcc13, float32_t *d_min);

#ifdef __cplusplus
}
#endif

#endif /* DIST_H */
