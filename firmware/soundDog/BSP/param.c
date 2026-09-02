/**
 * @file    param.c
 * @brief   FEAT-A4-04：运行期可调参数——帧解析状态机 + CRC16 + 坏帧/越界计数
 *
 * 职责边界（param.h §设计 / FEAT-A4-04 §1.4b 定稿）：
 *   - 本模块只做「协议解析」：逐字节喂入 → 校验帧头/地址/命令/CRC16 → 抽出 (PID, value)
 *   - 「实际生效」由调用方 freertos.c 实现的 param_apply() 负责（内部调 debounce_set_params），
 *     避免把 debounce 强耦合进 param —— param 不进入判据域。
 *   - 参数当前值存储于 debounce（s_param，单一数据源）；本模块 getter 只读代理，
 *     越界/坏帧计数在本模块。
 *
 * 帧协议（FEAT-A4-04 §1.4c 定稿，防自造）：
 *   固定 8 字节：0xA5 | 0x01(addr) | 0x21(cmd) | PID | 值LSB | 值MSB | CRC高 | CRC低
 *   CRC16 = Modbus 标准(poly 0x8005/init 0xFFFF/反射) → crcmod(0x18005,rev,init=0xFFFF) 交叉验算
 *   发送序 = 大端（CRC 高在前）。固定帧长即定界（无独立帧尾 0x5A，CRC 覆盖帧头~值）。
 *   校验向量：`A5 01 21 01 20 03`(TH=8.0)→CRC16=0x1327→帧尾 `13 27`
 *            `A5 01 21 02 28 00`(触发40)→CRC16=0xD290→帧尾 `D2 90`
 *
 * R27 三源（编码前已研，地址见 FEAT-A4-04 §1.4c「CRC16 来源」）：
 *   - CRC16 查表/位运算：libmodbus(modbus.org spec) + crcmod 交叉；
 *   - 字节序大端发送：Coskunruhi/STM32-UART-Protocol-Parser-with-CRC16（最贴近，poly 0x8005+大端）；
 *   - 逐字节状态机：EmreSoftware/STM32-UART-Protocol_LED-Control + skb666/stm32f4_iap，
 *     本实现按「固定 8 字节定长」简化为 Header→8 字节收集→CRC 校验→生效/丢弃 单飞状态机
 *     （长度固定 → 无需 LENGTH/DATA/FOOTER 多态，贴合 §1.4c「固定帧长定界」）。
 */
#include "param.h"
#include "debounce.h"   /* debounce_get_param()：运行期值单一读代理 */

/* ---- CRC16-Modbus（poly 0x8005 反射序 0xA001，init 0xFFFF，反射入出，xorout 0） ----
 * 位运算按字节走表/位，逐位反射——与 libmodbus/crcmod 一致。
 * 注意：crc16 计算后用「大端」比较（帧[6]=高字节在前）。 */
static uint16_t param_crc16_modbus(const uint8_t *data, unsigned len)
{
  uint16_t crc = 0xFFFFu;
  for (unsigned i = 0u; i < len; i++) {
    crc ^= data[i];
    for (unsigned b = 0u; b < 8u; b++) {
      if (crc & 0x0001u) {
        crc = (uint16_t)((crc >> 1u) ^ 0xA001u);
      } else {
        crc = (uint16_t)(crc >> 1u);
      }
    }
  }
  return crc;
}

/* ---- 帧解析状态机（单飞；非重入，仅 paramTask 调用） ----
 * 状态：IDLE 等帧头 → 收集 8 字节 → CRC 校验 → 生效/丢弃。
 * 坏帧计数：帧头/地址/命令/CRC16 任一错 → 丢弃 + 计数(坑 2)。 */

typedef enum {
  PSM_IDLE = 0,   /* 等 0xA5 */
  PSM_DATA        /* 已收帧头，按序收集到 8 字节为止 */
} psm_t;

static psm_t    s_psm  = PSM_IDLE;
static uint8_t  s_buf[PARAM_FRAME_LEN];
static uint8_t  s_idx  = 0u;
static uint32_t s_bad_frame_cnt = 0u;   /* 坏帧丢弃计数（诊断/评审 AC-03） */
static uint8_t  s_last_pid = 0u;        /* 最近合法帧 PID（事件打印） */
static uint16_t s_last_val = 0u;        /* 最近合法帧值  */

param_rx_t param_feed_byte(uint8_t byte)
{
  for (;;) {
    if (s_psm == PSM_IDLE) {
      if (byte == PARAM_HEADER) {
        s_buf[0] = byte;
        s_idx = 1u;
        s_psm = PSM_DATA;
      }
      return PARAM_RX_NONE;
    }

    /* PSM_DATA：收集 8 字节 */
    s_buf[s_idx++] = byte;
    if (s_idx < PARAM_FRAME_LEN) {
      return PARAM_RX_NONE;
    }

    /* 收满一帧：逐字段校验（帧头已在 s_buf[0]，无需重复查） */
    if (s_buf[1] != PARAM_ADDR || s_buf[2] != PARAM_CMD_WRITE) {
      s_bad_frame_cnt++;
      s_psm = PSM_IDLE;   /* 复位；本字节可能为下一帧帧头 → 重扫 */
      s_idx = 0u;
      if (byte == PARAM_HEADER) { continue; }   /* 重入以接纳连帧头 */
      return PARAM_RX_DROP_BAD;
    }

    /* CRC16 覆盖字节0~5（帧头~值高），大端比较 */
    uint16_t crc      = param_crc16_modbus(s_buf, 6u);
    uint16_t rx_crc   = (uint16_t)(((uint16_t)s_buf[6] << 8u) | s_buf[7]);
    if (crc != rx_crc) {
      s_bad_frame_cnt++;
      s_psm = PSM_IDLE;
      s_idx = 0u;
      if (byte == PARAM_HEADER) { continue; }
      return PARAM_RX_DROP_BAD;
    }

    /* 合法帧：PID/value 交给 param_apply（freertos.c 实现，内部校验范围+生效） */
    uint8_t  pid  = s_buf[3];
    uint16_t val  = (uint16_t)((uint16_t)s_buf[4] | ((uint16_t)s_buf[5] << 8u));
    bool applied  = param_apply(pid, val);

    /* 记录最近合法帧（事件打印用；越界拒绝时仍记录被拒的 pid/value 便于诊断） */
    s_last_pid = pid;
    s_last_val = val;

    /* 复位，接纳下一帧。统一重扫：三种路径（合法帧/坏帧/越界）对「本字节为连帧头」
     * 都重入接纳，行为一致（审查项4，防同一字节不同分支处理不一）。 */
    s_psm = PSM_IDLE;
    s_idx = 0u;
    if (byte == PARAM_HEADER) { continue; }   /* 连帧头重扫（合法帧/越界/坏帧统一） */
    return applied ? PARAM_RX_FRAME : PARAM_RX_DROP_OR;
  }
}

uint32_t param_get_bad_frame_cnt(void)
{
  return s_bad_frame_cnt;
}

uint8_t param_last_pid(void)
{
  return s_last_pid;
}

uint16_t param_last_val(void)
{
  return s_last_val;
}

/* ---- 运行期值读代理（单一数据源 = debounce.s_param） ----
 * 阈值 ×100 定点（长度/数值均 ×100 一致），其余原样返回。 */

uint16_t param_get_th_x100(void)
{
  return (uint16_t)(debounce_get_param().threshold * 100.0f + 0.5f);
}

uint16_t param_get_alarm_cnt(void)
{
  return debounce_get_param().consec_anom_alarm;
}

uint16_t param_get_release_cnt(void)
{
  return debounce_get_param().consec_norm_release;
}

uint16_t param_get_win_len(void)
{
  return (uint16_t)PARAM_WIN_LEN_FIX;   /* 滑窗固定 100，拒改 */
}

uint16_t param_get_win_anom(void)
{
  return debounce_get_param().win_anom_cnt;
}