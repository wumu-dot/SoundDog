/**
 * @file    mfcc.c
 * @brief   FEAT-A3-01：预加重 + 分帧 + 汉明窗（流式实现）
 *
 * 三个自研核心机制（设计定稿 §1.5，调研确认无先例可抄）：
 *  1. 流式预加重：x_prev 跨块静态状态，不随帧清零（否则每帧首样错）
 *  2. 400 环形缓冲 + 逐样精确出帧：块喂 256 与帧移 160 不通约，
 *     逐样暖机/相位判定瞬间快照——帧移严格 160 样无抖动
 *     （块末批量出帧会有 0~255 样抖动；有界小计数防回绕死锁）
 *  3. 汉明窗表 arm_cos_f32 运行期生成（防手抄 400 项常量表，R28 教训）
 *
 * 防御设计：
 *  - 全程 float32 域计算（预加重差分有负值——lessons unsigned 陷阱）
 *  - 大数组全静态（约定：printf/DSP 大数组禁上栈）
 *  - 无 ISR 上下文调用（仅 specTask），无需临界区保护
 *
 * 内存预算（设计定稿）：RAM +4.8KB 全 .bss（ring 1.6K + 帧输出 1.6K + 窗表 1.6K）
 */
#include "mfcc.h"
#include <string.h>
/* arm_cos_f32 原型已由 mfcc.h → arm_math.h 提供（BSP include 惯例随 fft.h） */

/* ---- 内部状态（全静态） ---- */

/* 汉明窗表：mfcc_init() 运行期生成（w[n]=0.54−0.46·cos(2πn/(N−1))，N=400） */
static float32_t s_hamming[MFCC_FRAME_LEN];

/* 预加重环形缓冲：s_ring[s_wr] 恒为最老样（400 样完整窗） */
static float32_t s_ring[MFCC_FRAME_LEN];
static uint32_t   s_wr;          /* 下一个写入位置 */

/* 流式预加重状态（跨块保持——设计机制 1） */
static float32_t  s_x_prev;      /* x[n−1]，初值 0 */

/* 出帧控制（机制 2；评审 Important#1 修复 2026-08-30：
 * 原 s_total/s_next_emit 绝对样计数 uint32 在 16kHz 下 ~3.1 天回绕 →
 * 出帧判定永假 → 静默死锁一圈。改为两个有界小循环量，永不回绕，
 * 出帧时序数学完全等价：第 m 帧出帧于第 400+160(m−1) 样喂入瞬间） */
static uint32_t   s_warmup;      /* 首帧暖机计数（0→400，攒满出首帧） */
static uint32_t   s_phase;       /* 帧内相位（0→160，到 160 出帧归零） */

/* 输出帧（加窗后）+ 统计 */
static float32_t  s_frame_out[MFCC_FRAME_LEN];  /* 最新加窗帧（A3-02 输入） */
static uint32_t   s_frame_cnt;   /* 已产出帧数 */
static mfcc_stat_t s_stat;       /* 最新帧统计快照 */

/* 帧回调（2026-08-30 帧率口径修正）：逐帧同步通知消费方 */
static mfcc_frame_cb_t s_frame_cb;

void mfcc_set_frame_cb(mfcc_frame_cb_t cb)
{
  s_frame_cb = cb;
}

/* ----------------------------------------------------------------------
 * mfcc_init：生成汉明窗表（一次，调度器前调用）
 * -------------------------------------------------------------------- */
void mfcc_init(void)
{
  /* 汉明窗标准定义（§1.4）：w[n] = 0.54 − 0.46·cos(2πn/(N−1))
   * n=0/N-1 → 0.08；n=(N-1)/2 → 1.0（w[199]≈1.0，AC-02 判据） */
  const float32_t two_pi_over = 6.283185307179586f /
                                (float32_t)(MFCC_FRAME_LEN - 1u);

  for (uint32_t n = 0u; n < MFCC_FRAME_LEN; n++) {
    s_hamming[n] = 0.54f - (0.46f * arm_cos_f32(two_pi_over * (float32_t)n));
  }

  /* 状态复位（支持重初始化场景） */
  memset(s_ring, 0, sizeof(s_ring));
  memset(s_frame_out, 0, sizeof(s_frame_out));
  memset(&s_stat, 0, sizeof(s_stat));
  s_wr     = 0u;
  s_x_prev = 0.0f;
  s_warmup = 0u;
  s_phase  = 0u;
  s_frame_cnt = 0u;
}

/* ----------------------------------------------------------------------
 * 内部：出帧——快照环形缓冲最老 400 样，乘汉明窗入输出帧
 * 前提：调用瞬间 s_ring 恰含完整 400 样（s_ring[s_wr] 为最老样）
 * -------------------------------------------------------------------- */
static void emit_frame(void)
{
  float32_t acc = 0.0f;   /* 帧能量累计（float 域，防 unsigned 陷阱） */
  const uint32_t mid = (MFCC_FRAME_LEN / 2u) - 1u;   /* 199，w≈1.0 */

  for (uint32_t k = 0u; k < MFCC_FRAME_LEN; k++) {
    /* 环形读：最老样在 s_wr，其后 399 样依次环绕 */
    const float32_t y = s_ring[(s_wr + k) % MFCC_FRAME_LEN];
    const float32_t w = s_hamming[k] * y;

    s_frame_out[k] = w;
    acc += w * w;
  }

  /* 统计快照（整数缩放后存，打印侧零浮点） */
  s_stat.frame_idx   = s_frame_cnt;
  s_stat.w0_x1000    = (int32_t)(s_frame_out[0]   * 1000.0f);
  s_stat.wmid_x1000  = (int32_t)(s_frame_out[mid] * 1000.0f);
  s_stat.energy_x100 = (int32_t)((acc / (float32_t)MFCC_FRAME_LEN) * 100.0f);
  /* A3-04 顺手项（A3-01 评审 Minor#2，第三次出现清偿）：吹气实测 e=INT32_MAX
   * 为 float→int32 溢出 UB，钳位语义化输出（大音量=爆表可读，非依赖巧合值） */
  if (s_stat.energy_x100 < 0) { s_stat.energy_x100 = INT32_MAX; }  /* 负值仅溢出产生 */

  s_frame_cnt++;
  /* 帧回调：逐帧同步通知（App 消费方须立即处理，数据下帧被覆盖） */
  if (s_frame_cb != NULL) {
    s_frame_cb(s_frame_out);
  }
}

/* ----------------------------------------------------------------------
 * mfcc_feed：喂入有效 PCM 块，逐样处理
 * -------------------------------------------------------------------- */
void mfcc_feed(const int16_t *pcm, uint32_t n)
{
  if ((pcm == NULL) || (n == 0u)) { return; }

  for (uint32_t i = 0u; i < n; i++)
  {
    /* 1) 流式预加重：y = x[n] − α·x[n−1]（s_x_prev 跨块保持） */
    const float32_t x = (float32_t)pcm[i];
    const float32_t y = x - (MFCC_PREEMPH * s_x_prev);
    s_x_prev = x;

    /* 2) 入环形缓冲（写入后 s_ring[s_wr] 仍是未被覆盖的最老样语义
     *    由 emit_frame 的读取公式保证：先读后挪） */
    s_ring[s_wr] = y;
    s_wr = (s_wr + 1u) % MFCC_FRAME_LEN;

    /* 3) 逐样精确出帧（机制 2 核心：消除块边界抖动；
     *    暖机 400 出首帧，此后每 160 样出一帧——等价于原
     *    total==next_emit 判定，但有界计数永不回绕） */
    if (s_warmup < MFCC_FRAME_LEN) {
      s_warmup++;
      if (s_warmup == MFCC_FRAME_LEN) {
        emit_frame();          /* 首帧：攒满 400 样 */
        s_phase = 0u;
      }
    } else {
      s_phase++;
      if (s_phase == MFCC_FRAME_SHIFT) {
        emit_frame();
        s_phase = 0u;
      }
    }
  }
}

/* ----------------------------------------------------------------------
 * 查询接口
 * -------------------------------------------------------------------- */
const float32_t *mfcc_last_frame(void)
{
  return (s_frame_cnt > 0u) ? s_frame_out : NULL;
}

uint32_t mfcc_frame_count(void)
{
  return s_frame_cnt;
}

void mfcc_get_stat(mfcc_stat_t *out)
{
  if (out != NULL) { *out = s_stat; }
}
