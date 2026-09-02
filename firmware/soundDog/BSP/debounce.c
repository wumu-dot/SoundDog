/**
 * @file    debounce.c
 * @brief   FEAT-A4-03：连续帧防抖判决状态机实现（双轨判据）
 *
 * 实现说明（设计定稿 §1.5 + 坑 1~4）：
 *   状态机：DB_NORMAL ──(轨1 连续≥30 | 轨2 滑窗≥30)──> DB_ALARMED
 *           DB_ALARMED ──(连续正常≥60)──────────────> DB_NORMAL
 *   计数清零规则：正常态内任一正常帧 → 连续异常计数清零；
 *                报警态内任一异常帧 → 连续正常计数清零（连续判定语义）。
 *   轨2 滑窗：DB_WIN_LEN(100) bit 位图环形缓冲（static uint32_t 4×32bit），
 *            每帧入一位（1=异常）弹一位，窗内异常数 O(1) 维护（增帧减位）——
 *            全帧比较无抽稀（ViolaWake PR#34 教训，坑 2/坑 9 判据）。
 *   报警态内不再评估判据（坑 3：防双轨重复触发，恢复后自然重计）。
 *   静态初始化=0 → 上电/看门狗复位自动回到 DB_NORMAL，计数清零（坑 4）。
 */
#include "debounce.h"
#include "FreeRTOS.h"   /* portENTER_CRITICAL/portEXIT_CRITICAL（A4-04 并发保护） */

/* ---- A4-04 运行期参数（默认 = 宏默认，经 debounce_set_params 在线覆盖） ---- */
static debounce_param_t s_param = {
  .threshold           = DB_THRESHOLD,
  .consec_anom_alarm   = DB_CONSEC_ANOM_ALARM,
  .consec_norm_release = DB_CONSEC_NORM_RELEASE,
  .win_anom_cnt        = DB_WIN_ANOM_CNT,
};

/* ---- 内部状态（静态 → 上电/看门狗复位自动清零，坑 4） ---- */
static db_state_t s_state = DB_NORMAL;

/* 轨1：连续计数 */
static uint16_t s_consec_anom = 0u;   /* 正常态：连续异常帧数（达 30 → 报警） */
static uint16_t s_consec_norm = 0u;   /* 报警态：连续正常帧数（达 60 → 解除） */

/* 轨2：100 bit 滑窗（位图环形缓冲）。容量与帧率耦合（坑 2）：
 * DB_WIN_LEN 必须 == 100（1s @100 帧/s）。若改帧率须同步改此位图容量。 */
#define DB_BITMAP_WORDS ((DB_WIN_LEN + 31u) / 32u)   /* 100 → 4 */
static uint32_t s_win_bits[DB_BITMAP_WORDS];
static uint16_t s_win_anom;   /* 窗内异常帧总数（O(1) 维护） */
static uint16_t s_win_head;   /* 窗内帧计数（0..99，同时作环形索引） */

/* ---- 内部工具 ---- */

/* 读位图第 bit 位（bit 0 = 最旧帧？见下：我们用 s_win_head 顺序索引，
 * 每一位独立存"该序号帧是否异常"，无需移位掩码的环——简化用数组字位。 */
static uint32_t db_bitmap_get(unsigned bit)
{
  return (s_win_bits[bit >> 5u] >> (bit & 31u)) & 1u;
}

static void db_bitmap_set(unsigned bit, uint32_t v)
{
  uint32_t mask = 1u << (bit & 31u);
  if (v) {
    s_win_bits[bit >> 5u] |= mask;
  } else {
    s_win_bits[bit >> 5u] &= ~mask;
  }
}

/* 入窗一帧：写入序号 s_win_head（0..99），s_win_head 递增循环。
 * 返回被弹出的旧序号（>=100 时才是有效旧帧；前 100 帧无弹出）。
 * 设计：s_win_head 即"已入帧总数 mod 100"的计数——用满 100 后
 * 每次入帧把当前位写覆盖，但旧值需先减出 s_win_anom。
 * 为区分"前 100 帧"与"满窗后"，维护 s_win_head 在 [0,99] 内递增环。 */
static void db_win_push(bool anomaly)
{
  uint32_t old = db_bitmap_get(s_win_head);
  if (old) {
    s_win_anom--;              /* 弹出旧异常帧 */
  }
  db_bitmap_set(s_win_head, anomaly ? 1u : 0u);
  if (anomaly) {
    s_win_anom++;
  }
  s_win_head = (uint16_t)((s_win_head + 1u) % DB_WIN_LEN);
}

/* ---- 主接口 ---- */

int debounce_process(bool frame_is_anomaly, int *alarm_changed,
                     int *alarm_state, int *alarm_src, uint16_t *alarm_count)
{
  int changed = 0;
  int src = (int)DB_SRC_NONE;
  uint16_t count = 0u;    /* 事件帧触发/解除计数（仅 changed=1 有效） */

  if (s_state == DB_NORMAL) {
    /* 轨1：连续异常计数（任一正常帧清零 → 连续语义） */
    if (frame_is_anomaly) {
      if (s_consec_anom < DB_COUNT_CAP) {
        s_consec_anom++;
      }
    } else {
      s_consec_anom = 0u;                    /* 中断即清零（坑 1/§3.4） */
    }
    /* 轨2：滑窗更新（每帧都入窗，正常帧位=0） */
    db_win_push(frame_is_anomaly);

    /* 双轨 OR 判据（先到先报；同帧双满足 → 轨1 优先，count=其触发帧数）。
     * A4-04：判据阈值改为读运行期参数 s_param（默认=宏，行为与 A4-03 一致）。 */
    if (s_consec_anom >= s_param.consec_anom_alarm ||
        s_win_anom >= s_param.win_anom_cnt) {
      if (s_consec_anom >= s_param.consec_anom_alarm) {
        src = (int)DB_SRC_CONSEC;
        count = s_consec_anom;               /* =30（触发阈值） */
      } else {
        src = (int)DB_SRC_MOFN;
        count = s_win_anom;                  /* 窗内异常帧数 ≥30 */
      }
      s_state = DB_ALARMED;
      /* 报警态进入时清零轨1计数（解除路径重新从 0 累计正常帧） */
      s_consec_anom = 0u;
      s_consec_norm = 0u;
      changed = 1;
    }
  } else {  /* DB_ALARMED：只评估解除判据（坑 3：不重复评估触发） */
    if (!frame_is_anomaly) {
      if (s_consec_norm < DB_COUNT_CAP) {
        s_consec_norm++;
      }
    } else {
      s_consec_norm = 0u;                    /* 任一异常帧中断解除累计 */
    }
    /* 滑窗仍持续维护（保持状态一致性；报警态内不用于触发，仅保持满窗） */
    db_win_push(frame_is_anomaly);

    if (s_consec_norm >= s_param.consec_norm_release) {
      src = (int)DB_SRC_NONE;                /* 解除事件来源=NONE */
      count = s_consec_norm;                 /* =60（解除阈值） */
      s_state = DB_NORMAL;
      s_consec_norm = 0u;
      s_win_anom = 0u;    /* 复位滑窗（新正常期从空窗起算，防旧异常帧残留误触发） */
      s_win_head = 0u;
      for (unsigned i = 0u; i < DB_BITMAP_WORDS; i++) {
        s_win_bits[i] = 0u;   /* 必须清位：否则陈旧 1 位被弹时 s_win_anom 下溢 */
      }
      changed = 1;
    }
  }

  if (alarm_changed) {
    *alarm_changed = changed;
  }
  if (alarm_state) {
    *alarm_state = (int)s_state;
  }
  if (alarm_src) {
    *alarm_src = src;
  }
  if (alarm_count) {
    *alarm_count = count;
  }
  return (int)s_state;
}

/* ---- A4-04：运行期参数读写（默认=宏，见文件头 s_param） ----
 * 并发安全（审查项3）：specTask(mel_frame_cb) 读 + paramTask(param_apply) 写，
 * 跨任务共享。读写各包临界区防撕裂；getter 返回「副本」而非内部指针，
 * 避免调用方持指针期间被 paramTask 改写读到一半（float threshold + 3×uint16）。
 * 临界区开销可忽略：一次拷贝 ~16B，周期级。 */

debounce_param_t debounce_get_param(void)
{
  debounce_param_t p;
  portENTER_CRITICAL();
  p = s_param;
  portEXIT_CRITICAL();
  return p;
}

void debounce_set_params(const debounce_param_t *p)
{
  if (!p) {
    return;
  }
  portENTER_CRITICAL();
  s_param = *p;
  portEXIT_CRITICAL();
}
