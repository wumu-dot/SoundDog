/**
 * @file    param.h
 * @brief   FEAT-A4-04：运行期可调参数模块接口
 *
 * 数据流位置（设计 §1.4b 定稿）：
 *   paramTask(freertos.c):  收 USART3 字节 → 帧解析状态机(本模块) → 校验/生效
 *
 * 帧协议（FEAT-A4-04 §1.4c 定稿，防自造；PC 端 gen_param_frames.py 同源）：
 *   固定 8 字节：0xA5 | 0x01(addr) | 0x21(cmd写) | PID | 值LSB | 值MSB | CRC高 | CRC低
 *   CRC16 = Modbus 标准(poly 0x8005/init 0xFFFF/反射/大端发送)
 *   帧尾 = 固定帧长定界（无需独立 0x5A 字节，CRC 覆盖帧头~值）
 *   取值/生效：越界拒绝保留旧值；坏帧丢弃计数（§1.4a 人确认）
 *
 * R27 三源（编码前已研，均 verified，见 §1.4c/实现注释）：
 *   - CRC16：libmodbus/ArduinoModbus 查表实现 + crcmod(0x18005,rev,init 0xFFFF) 交叉验算；
 *   - 帧解析状态机：CSDN「状态机逐字节解析」+ eet-china「通用接收机」(WaitingForHeader→Reading→Reset)
 *     + cnblogs「增量解析(半包/粘包)」——成功或失败均 Reset，防数据粘滞；
 *   - RS485 收帧上下文：MaJerle/stm32-usart-uart-dma-rx-tx(ST FAE) 轮询/中断范式——本工程
 *     短线点对点 + 8 字节定长 + 低频下发 → 采用参数化轮询接收（比 DMA 更轻，无 DMA 通道依赖）。
 */
#ifndef PARAM_H
#define PARAM_H

#include <stdint.h>
#include <stdbool.h>   /* bool（同 debounce.h 惯例） */

#ifdef __cplusplus
extern "C" {
#endif

/* ---- 帧布局（固定 8 字节，§1.4c，禁止自造） ---- */
#define PARAM_HEADER        0xA5u   /* 帧头（§1.4c 固定帧长定界，无独立帧尾） */
#define PARAM_FRAME_LEN     8u
#define PARAM_ADDR          0x01u   /* 本设备地址 */
#define PARAM_CMD_WRITE     0x21u   /* 写单一参数 */

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

/* ---- 解析结果 ---- */
typedef enum {
  PARAM_RX_NONE = 0,   /* 未完成一帧（继续等字节） */
  PARAM_RX_FRAME,      /* 完整合法帧已收讫，待处理 */
  PARAM_RX_DROP_BAD,   /* 坏帧丢弃（帧头/地址/命令/CRC 错），计数+1 */
  PARAM_RX_DROP_OR     /* 越界被拒，保留旧值，计数+1（可区分看门的坏帧 vs 越界） */
} param_rx_t;

/* ---- 参数读写 ---- */
uint16_t param_get_th_x100(void);        /* 阈值（×100 定点） */
uint16_t param_get_alarm_cnt(void);      /* 轨1 触发帧数 */
uint16_t param_get_release_cnt(void);    /* 解除帧数 */
uint16_t param_get_win_len(void);        /* 滑窗长（固定 100） */
uint16_t param_get_win_anom(void);       /* 窗内异常帧阈值 */
uint32_t param_get_bad_frame_cnt(void);  /* 坏帧丢弃计数（诊断/评审） */

/* 最近一次「完整合法帧」解析出的 参数 ID / 值（打印事件用）。
 * 合法帧(生效)与越界拒绝(未生效)都会更新二者（freertos 分别打印 set/rej 值）；
 * 坏帧（帧头/地址/命令/CRC 错）不更新。 */
uint8_t  param_last_pid(void);
uint16_t param_last_val(void);

/* 逐字节喂入帧解析状态机，返回本字节是否闭合一帧（成功/坏帧/越界）。
 * 处理器代：must re-in Args onFrame/onDrop callbacks（见实现注释第 2 行）。
 * 非重入；仅 paramTask 单消费者调用（同 debounce/mel 单消费者约定）。 */
param_rx_t param_feed_byte(uint8_t byte);

/* 生效回调（由调用方 freertos.c 定义，param.c 编译期符号链接调用，免 debounce 编进 param
 * 模块——param 只解析协议，实际 apply 由 freertos.c 调 debounce_set_params 做）。
 * 返回 true=本次应用成功；伪=范围校验失败（调用方应据此计数越界拒绝）。 */
bool param_apply(uint8_t pid, uint16_t value);

#ifdef __cplusplus
}
#endif

#endif /* PARAM_H */