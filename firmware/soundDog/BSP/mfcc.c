/**
 * @file    mfcc.c
 * @brief   FEAT-A3-01：预加重 + 分帧 + 汉明窗（流式实现）
 *
 * 三个自研核心机制（设计定稿 §1.5，调研确认无先例可抄）：
 *  1. 流式预加重：x_prev 跨块静态状态，不随帧清零（否则每帧首样错）
 *  2. 400 环形缓冲 + 逐样精确出帧：块喂 256 与帧移 160 不通约，
 *     逐样判定 total==next_emit 瞬间快照——帧移严格 160 样无抖动
 *     （块末批量出帧会有 0~255 样抖动）
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

/* arm_cos_f32（FastMathFunctions，Makefile:96 已链接） */
#include "dsp/fast_math_functions.h"

/* ---- 内部状态（全静态） ---- */

/* 汉明窗表：mfcc_init() 运行期生成（w[n]=0.54−0.46·cos(2πn/(N−1))，N=400） */
static float32_t s_hamming[MFCC_FRAME_LEN];

/* 预加重环形缓冲：s_ring[s_wr] 恒为最老样（400 样完整窗） */
static float32_t s_ring[MFCC_FRAME_LEN];
static uint32_t   s_wr;          /* 下一个写入位置 */

/* 流式预加重状态（跨块保持——设计机制 1） */
static float32_t  s_x_prev;      /* x[n−1]，初值 0 */

/* 出帧控制（设计机制 2） */
static uint32_t   s_total;       /* 已喂入总样数（单调递增） */
static uint32_t   s_next_emit;   /* 下次出帧的 total 判定点（首帧 400，此后 +160） */

/* 输出帧（加窗后）+ 统计 */
static float32_t  s_frame_out[MFCC_FRAME_LEN];  /* 最新加窗帧（A3-02 输入） */
static uint32_t   s_frame_cnt;   /* 已产出帧数 */
static mfcc_stat_t s_stat;       /* 最新帧统计快照 */

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
  s_wr        = 0u;
  s_x_prev    = 0.0f;
  s_total     = 0u;
  s_next_emit = MFCC_FRAME_LEN;   /* 首帧：攒满 400 样即出 */
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

  s_frame_cnt++;
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
    s_total++;

    /* 3) 逐样精确出帧判定（机制 2 核心：消除块边界抖动） */
    if (s_total == s_next_emit) {
      emit_frame();
      s_next_emit += MFCC_FRAME_SHIFT;
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
