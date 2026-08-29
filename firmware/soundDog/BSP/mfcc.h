/**
 * @file    mfcc.h
 * @brief   FEAT-A3-01：MFCC 前置预处理（预加重 + 分帧 + 汉明窗）
 *
 * 数据流位置（设计定稿 §1.5）：
 *   specTask 有效窗(256 样) → spec_process()（A2 原路）
 *                         └→ mfcc_feed()（本模块，旁路）
 *
 * 产出：400 样加窗帧（float32），供 A3-02 arm_mfcc_f32 使用
 * （512 点补零在 A3-02 落地，本模块不管）。
 *
 * 关键参数（FEAT-A3-01 §1.4，禁止自造）：
 *   帧长 400 样（25ms@16kHz）/ 帧移 160 样（10ms）/ α=0.97 / 汉明窗 N=400
 *
 * R27 调研结论（§7.1）：预加重+流式分帧无开源先例（同类项目均为
 * 整段缓冲回放式），本模块为自研；算法层参数与 4 项目交叉一致。
 */
#ifndef MFCC_H
#define MFCC_H

#include <stdint.h>

/* ARM_MATH_CM4F / __FPU_PRESENT 由 Makefile 全局定义（A2-01） */
#include "dsp/transform_functions.h"   /* float32_t */

#ifdef __cplusplus
extern "C" {
#endif

/* ---- 参数（§1.4） ---- */
#define MFCC_FRAME_LEN   400u   /* 帧长 25ms @ 16kHz */
#define MFCC_FRAME_SHIFT 160u   /* 帧移 10ms（重叠 240 样 = 15ms） */
#define MFCC_PREEMPH     0.97f  /* 预加重系数 α */

/* ---- 打印统计快照（值拷贝，避免外部直接摸内部状态） ---- */
typedef struct {
  uint32_t frame_idx;    /* 最新帧号（0 起） */
  int32_t  w0_x1000;     /* 加窗帧首样 ×1000（静音≈0，有声≈w[0]×输入量级） */
  int32_t  wmid_x1000;   /* 加窗帧中心样 ×1000（w[199]≈1.0，最灵敏观测点） */
  int32_t  energy_x100;  /* 帧均方能量 ×100（float 域算完缩放，防 unsigned 陷阱） */
} mfcc_stat_t;

/* ---- 接口 ---- */

/** @brief 初始化：arm_cos_f32 生成汉明窗表（一次，调度器启动前调） */
void mfcc_init(void);

/** @brief 喂入有效 PCM 块（int16 原始样值，来自 specTask 有效窗）
 *  @note  逐样处理：流式预加重 + 400 环形缓冲 + 逐样精确出帧（帧移严格 160 样）。
 *         非重入；调用者仅 specTask（单消费者约定）。 */
void mfcc_feed(const int16_t *pcm, uint32_t n);

/** @brief 最新加窗帧（400×float32；mfcc_feed 出帧时更新）
 *  @note  供 A3-02 读取（同任务上下文或读侧临界区）。 */
const float32_t *mfcc_last_frame(void);

/** @brief 已产出帧数（验收打印用） */
uint32_t mfcc_frame_count(void);

/** @brief 取打印统计快照（§3.3 MFCC 行数据源） */
void mfcc_get_stat(mfcc_stat_t *out);

#ifdef __cplusplus
}
#endif

#endif /* MFCC_H */
