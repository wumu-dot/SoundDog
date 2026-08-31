/**
 * @file    debounce.h
 * @brief   FEAT-A4-03：连续帧防抖判决状态机（双轨判据）
 *
 * 数据流位置（设计定稿）：
 *   specTask: ... → dist_process()（A4-02，s_dmin）
 *                              └→ debounce_process()（本模块）→ 报警/解除事件
 *
 * 关键参数（FEAT-A4-03 §1.4/§1.5/阶段2，禁止自造）：
 *   阈值 TH = 6.0f（A4-02 存证 CSV 定标：正常误判 1% / 金属漏判 16%）
 *   轨1（规格判据）：连续异常帧 ≥ 30（300ms 无间隔）→ 报警
 *   轨2（敲击比率判据）：100 帧滑窗内异常帧 ≥ 30（1s 窗 30% 占比，m-of-n）→ 报警
 *   解除：连续正常帧 ≥ 60（600ms 无间隔超阈以下）
 *   双轨 OR 先到先报；报警态内不再评估判据（防重复触发）
 *
 * R27 三源结论（A4-02 阶段 4 已研，本 FEAT 直接引用）：
 *   - prmon：m-of-n 连续帧比率判据（≥10% 时间步被标记才判异常）→ 轨2 依据；
 *   - Edge Impulse：窗口均值后处理（不裸用单帧/抽稀 max 过阈值）；
 *   - ViolaWake PR#34：max 取自抽稀子集会低估 2~4× → 滑窗必须全帧比较
 *     （本模块在 mel_frame_cb 内逐帧喂入，天然全帧，坑 9 判据）。
 *   - 坑清单：FEAT-A4-03 §1.5 坑 1~4（事件打印即时/位图容量耦合/双轨不重复触发/
 *     重启残留清零）。
 */
#ifndef DEBOUNCE_H
#define DEBOUNCE_H

#include <stdint.h>
#include <stdbool.h>   /* bool 参数类型（同 freertos.c 惯例） */

#ifdef __cplusplus
extern "C" {
#endif

/* ---- 参数（§1.4/§1.5 定稿，禁止自造） ---- */
#define DB_THRESHOLD            6.0f   /* d_min 异常阈值（A4-02 定标；A4-04 可下发覆盖） */
#define DB_CONSEC_ANOM_ALARM    30u    /* 轨1：连续异常帧数 → 报警 */
#define DB_CONSEC_NORM_RELEASE  60u    /* 解除：连续正常帧数 → 解除 */
#define DB_WIN_LEN              100u   /* 轨2：滑窗长度（= 1s @100 帧/s） */
#define DB_WIN_ANOM_CNT         30u    /* 轨2：窗内异常帧数 ≥ 此值 → 报警（30% 占比） */
#define DB_COUNT_CAP            60000u /* 计数封顶（AC-08：60×1000 冗余，防溢出） */

/* ---- 状态 ---- */
typedef enum {
  DB_NORMAL = 0,   /* 正常态（未报警） */
  DB_ALARMED       /* 报警态 */
} db_state_t;

/* 报警触发来源（ALARM 行 src= 字段，用于诊断/评审） */
typedef enum {
  DB_SRC_NONE = 0,
  DB_SRC_CONSEC = 1,  /* 轨1：连续 30 帧 */
  DB_SRC_MOFN = 2,    /* 轨2：滑窗 30/100 帧 */
} db_src_t;

/* ---- 接口 ---- */

/** @brief 逐帧判决（100 帧/s 网格，与 dist_process 同回调）
 *  @param  frame_is_anomaly  [in]  本帧是否异常（d_min > DB_THRESHOLD）
 *  @param  alarm_changed     [out] 可选：事件标志（1=本次刚触发/解除，0=无变化；NULL 忽略）
 *  @param  alarm_state       [out] 可选：当前报警状态（1=报警中，0=正常；NULL 忽略）
 *  @param  alarm_src         [out] 可选：本次事件来源（解除=DB_SRC_NONE，
 *                                    轨1=DB_SRC_CONSEC，轨2=DB_SRC_MOFN；NULL 忽略）
 *  @param  alarm_count       [out] 可选：本次事件触发/解除帧数
 *                                    （报警=触发帧数≥30，解除=连续正常帧数；NULL 忽略）
 *  @return 当前报警状态（1=报警中，0=正常）
 *  @note   纯状态机（内部静态计数/位图 → 有状态，同 mel 需要 init 清理的例外；
 *          静态初始化=0 → 上电/看门狗复位自动清零，坑 4 判据达成）。
 *          非重入；调用者仅 specTask（单消费者约定，同 mel_process/dist_process）。
 *          alarm_src/alarm_count 仅事件帧（alarm_changed=1）有效。 */
int debounce_process(bool frame_is_anomaly, int *alarm_changed,
                     int *alarm_state, int *alarm_src, uint16_t *alarm_count);

#ifdef __cplusplus
}
#endif

#endif /* DEBOUNCE_H */
