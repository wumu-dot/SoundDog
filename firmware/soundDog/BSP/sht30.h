/**
 * @file    sht30.h
 * @brief   SHT30 温湿度传感器驱动（I2C3）—— 从站侧
 *
 * 设计要点：
 *   1. 独占 I2C3（PA8=SCL / PC9=SDA），**不碰** OLED 所在的 I2C1（PB6/PB7）
 *   2. 对外只输出「×10 定点整数」，可直接映射到 Modbus 保持寄存器
 *      （避免 float 运算与传输，与从站既有 u16 寄存器风格一致）
 *   3. 初始化自带 GPIO/时钟配置，不依赖 HAL_I2C_MspInit 的 I2C1 分支
 *   4. 无日志输出（遵循 BUG-003：USART1 单写者约束，本驱动不写串口）
 *
 * 移植来源：ov-watch/firmware/Drivers/BSP/sensor/temp_sensor.c
 *   改动：hi2c2 → hi2c3；HAL_Delay → vTaskDelay；float → ×10 定点
 */

#ifndef __SHT30_H
#define __SHT30_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------
 * 最新一次读数（×10 定点），供 Modbus 回调直接取用
 *   g_temp_x10：温度 ×10（253 = 25.3℃）
 *   g_humi_x10：湿度 ×10（612 = 61.2 %RH）
 * 由 sht30_task() 周期刷新；未初始化/读取失败时保持上一次有效值。
 * ⚠️ 读侧无须加锁：单写者（sht30_task）+ 32 位对齐的 int16 赋值天然原子。
 * ------------------------------------------------------------------ */
extern volatile int16_t g_temp_x10;
extern volatile int16_t g_humi_x10;

/**
 * @brief 初始化 I2C3 并探测 SHT30
 * @return 0 = 成功且器件在线；-1 = I2C 初始化失败或器件未应答
 */
int sht30_init(void);

/**
 * @brief 传感器轮询任务（FreeRTOS 任务体）
 *
 * 自行调用 sht30_init()，之后每 SHT30_TASK_PERIOD_MS 读一次并刷新全局值。
 * ⚠️ 必须作为**独立任务**运行：内部含 vTaskDelay 等待测量完成，
 *    ⛔ 不可在 Modbus 回调或临界区内调用（否则阻塞协议栈，触 P0-3 红线）。
 */
void sht30_task(void *argument);

/**
 * @brief 查询上次 init/read 时器件是否在线
 */
bool sht30_present(void);

/**
 * @brief 读一次温湿度（单次测量，高重复性）
 *
 * @param[out] temp_x10  温度 ×10（例：253 = 25.3 ℃），范围约 -450 ~ 1300
 * @param[out] humi_x10  湿度 ×10（例：612 = 61.2 %RH），范围 0 ~ 1000
 * @return 0 = 成功；-1 = 未初始化/通信失败/器件不在线
 *
 * @note 内部含 20 ms 测量等待（vTaskDelay，会让出 CPU）。
 *       不可在中断上下文调用。
 */
int sht30_read_x10(int16_t *temp_x10, int16_t *humi_x10);

#ifdef __cplusplus
}
#endif

#endif /* __SHT30_H */
