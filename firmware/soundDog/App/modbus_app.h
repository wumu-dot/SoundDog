/**
 * @file    modbus_app.h
 * @brief   FreeModbus 从站应用层：将运行期参数映射为 Modbus 保持寄存器
 *
 * 数据流位置（FEAT-A4-04 Modbus 迁移定稿）：
 *   PC(Modbus Master) → RS485 → FreeModbus 栈 → eMBRegHoldingCB(本模块)
 *                          → param_apply(freertos.c，复用 A4-04 校验/生效) → debounce_set_params
 *
 * 保持寄存器映射（1 基址，与 A4-04 §1.4a 参数表一一对应，禁止自造）：
 *   0x0001(输入)0x01  TH 阈值（×100 定点，600=6.0）
 *   0x0002(输入)0x02  轨1：连续异常帧 → 报警
 *   0x0003(输入)0x03  解除：连续正常帧 → 解除
 *   0x0004(输入)0x04  轨2：滑窗长（固定=100，拒改）
 *   0x0005(输入)0x05  轨2：窗内异常帧阈值
 *
 * 本模块只实现回调供协议栈调用（全局符号，FreeModbus 链接期引用）；
 * 不含任务/初始化——从站轮询任务（eMBPoll）由 freertos.c MBSlavePollTask 负责。
 */
#ifndef MODBUS_APP_H
#define MODBUS_APP_H

#ifdef __cplusplus
extern "C" {
#endif

/* 保持寄存器数量（1..5，与 A4-04 §1.4a 表并列，禁止自造） */
#define MODBUS_REG_HOLDING_NUM   5u

#ifdef __cplusplus
}
#endif

#endif /* MODBUS_APP_H */