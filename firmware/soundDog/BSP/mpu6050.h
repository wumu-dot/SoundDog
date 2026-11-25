/**
 * @file    mpu6050.h
 * @brief   MPU6050 振动通道（I2C2，PF1=SCL / PF0=SDA）—— 从站侧
 *
 * 规格来源：docs/features/FEAT-A7-05 §3.2（覆盖 FEAT-A7-04 的 ODR/DLPF）
 *   ODR      = 500 Hz   (SMPLRT_DIV 0x19 = 0x01)
 *   DLPF     = 260 Hz   (CONFIG 0x1A = 0x00，芯片上限)
 *   量程     = ±8 g     (ACCEL_CONFIG 0x1C = 0x10，4096 LSB/g)
 *   输出     = 去重力合成加速度 RMS / 峰值，单位 mg，写 Modbus 寄存器 7/8
 *
 * ⚠️ 能力边界（不得越界声称）：
 *   - 带宽上限 260 Hz（芯片天花板），**不做 ISO 分区对标**
 *   - 只做趋势量，**不做轴承故障诊断**（需 500 Hz~10 kHz + 包络解调）
 */
#ifndef MPU6050_H
#define MPU6050_H

#include <stdint.h>

#define MPU6050_ADDR        0x68u   /* AD0 接 GND；悬空则可能 0x69 */

/* 只读遥测（mg，0~5000），由 mpu6050.c 维护，modbus_app.c 只读 */
extern volatile uint16_t g_vib_rms;
extern volatile uint16_t g_vib_peak;

/**
 * @brief 初始化 I2C2 + MPU6050（含 WHO_AM_I 校验与唤醒/量程/DLPF 配置）
 * @return 0 成功；-1 失败（器件不在、ID 不符、I2C 错误）
 * @note  必须在任务上下文调用（内部用 vTaskDelay）
 */
int mpu6050_init(void);

/** @brief 器件是否在线（供上层判断数据有效性） */
int mpu6050_present(void);

/**
 * @brief 振动采集任务：500 Hz 采样，1 s 窗口结算 RMS/峰值并写全局量
 * @note  优先级 BelowNormal；init 失败会每 10 s 重试（支持后接线）
 */
void mpu6050_task(void *argument);

#endif /* MPU6050_H */
