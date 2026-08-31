/**
 * @file    dist.c
 * @brief   FEAT-A4-02：距离实时比对实现（平方距离比较 + 出值 1 次 sqrt）
 *
 * R27 三源预研记录（详见 FEAT-A4-02 §1.5）：
 *   源① 源码：ARM-software/CMSIS-DSP arm_euclidean_distance_f32 标量路径实读——
 *      累加 (a-b)^2 后 1 次 sqrt，纯函数无副作用（R28 硬件无关，算法可复制）。
 *   源② Issue 区调研：ViolaWake PR#34（GitHub issue 实证：报告 max 取自抽稀子集
 *      低估 2~4x）；Edge Impulse 官方后处理 / prmon m-of-n / AdaBEAM 双轨——
 *      判决聚合口径业界证据（A4-03 立项输入）。
 *   源③ 坑清单：FEAT-A4-02 坑 1~9（坑 2：本模块比较用平方距离仅出值 1 次 sqrt
 *      预防 16 次多余 sqrt；坑 9：峰值统计须留在回调全帧比较）。
 *   本模块 16 模版比较仅对**最小者**取 sqrt（sqrt 单调 → 平方距离最小即欧氏
 *   距离最小）。
 */
#include "dist.h"

int dist_process(const float32_t *mfcc13, float32_t *d_min)
{
  if ((mfcc13 == NULL) || (d_min == NULL)) {
    return -1;
  }

  /* 平方距离最小值搜索：16 模版 × 13 维 = 208 MAC/帧（§1.5 三：CPU 0.012%） */
  float32_t best_sq = 0.0f;
  for (uint32_t c = 0u; c < MODEL_NORMAL_K; c++) {
    float32_t acc = 0.0f;
    for (uint32_t k = 0u; k < MODEL_NORMAL_D; k++) {
      const float32_t diff = mfcc13[k] - model_normal[c][k];
      acc += diff * diff;
    }
    if ((c == 0u) || (acc < best_sq)) {
      best_sq = acc;
    }
  }

  /* 出值：仅 1 次 sqrt（比较已完成，坑 2） */
  arm_sqrt_f32(best_sq, d_min);
  return 0;
}
