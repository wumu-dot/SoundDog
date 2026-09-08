/**
 * @file    param.h
 * @brief   FEAT-A4-04：运行期可调参数 —— 参数 ID / 范围宏 + 生效回调（校验单一源）
 *
 * 2026-09-08 Modbus 迁移（人确认）：废弃原 0xA5 私有帧。帧解析职责移交 FreeModbus 栈
 * （Middleware/Third_Party/FreeModbus），本模块仅保留「参数 ID / 取值范围」作为唯一校验源，
 * 供 modbus_app.c（寄存器回调）与 freertos.c（param_apply 生效）共用，防双源漂移。
 *
 * 数据流位置（FEAT-A4-04 §1.4a 定稿）：
 *   PC(Modbus Master) → RS485 → FreeModbus 栈 → eMBRegHoldingCB(modbus_app.c)
 *     → reg_is_valid(本头文件范围宏) → param_apply(freertos.c) → debounce_set_params
 *
 * 寄存器映射（保持寄存器 1 基址，与 modbus_app.c reg_off_to_pid 一一对应）：
 *   RTU 0x0001 ↔ PARAM_ID_TH         阈值（×100 定点，600=6.0）
 *   RTU 0x0002 ↔ PARAM_ID_ALARM_CNT  轨1 连续异常帧 → 报警
 *   RTU 0x0003 ↔ PARAM_ID_RELEASE_CNT 解除 连续正常帧 → 解除
 *   RTU 0x0004 ↔ PARAM_ID_WIN_LEN    轨2 滑窗长（固定=100，拒改）
 *   RTU 0x0005 ↔ PARAM_ID_WIN_ANOM   轨2 窗内异常帧阈值
 *
 * R27/R32 三源（编码前已研，见 FEAT-A4-04 §1.4c）：
 *   - FreeModbus：quanghona/Freemodbus-CMSIS-RTOS2-STM32；
 *   - Modbus RTU 规范/CRC16：modbus.org spec + docs.chipkin.com，crcmod 交叉锁定；
 *   - 寄存器错误语义：mb.c:prveMBError2Exception。
 */
#ifndef PARAM_H
#define PARAM_H

#include <stdint.h>
#include <stdbool.h>   /* bool param_apply */

#ifdef __cplusplus
extern "C" {
#endif

/* ---- 参数 ID（§1.4a 表，禁止自造） ---- */
#define PARAM_ID_TH         0x01u   /* 阈值，uint16 定点 ×100（600=6.0） */
#define PARAM_ID_ALARM_CNT  0x02u   /* 轨1：连续异常帧 → 报警 */
#define PARAM_ID_RELEASE_CNT 0x03u  /* 解除：连续正常帧 → 解除 */
#define PARAM_ID_WIN_LEN    0x04u   /* 轨2：滑窗长（固定=100，拒改） */
#define PARAM_ID_WIN_ANOM   0x05u   /* 轨2：窗内异常帧阈值 */

/* ---- 参数取值范围（§1.4a，禁止自造） ---- */
#define PARAM_TH_MIN_x100   100u    /* 1.0 */
#define PARAM_TH_MAX_x100   2000u   /* 20.0 */
#define PARAM_FRAME_MIN     1u
#define PARAM_FRAME_MAX     100u
#define PARAM_WIN_LEN_FIX   100u    /* 滑窗固定=100（位图容量耦合，拒改） */

/* 生效回调（由调用方 freertos.c 定义并实现，本头文件仅声明；modbus_app.c 与 freertos.c 共用）。
 * 返回 true=本次应用成功；伪=范围校验失败（调用方据此计数越界拒绝）。
 * 处理器代：经 FreeModbus 寄存器写回调单线程调用（MBSlavePollTask），非重入即可。 */
bool param_apply(uint8_t pid, uint16_t value);

#ifdef __cplusplus
}
#endif

#endif /* PARAM_H */