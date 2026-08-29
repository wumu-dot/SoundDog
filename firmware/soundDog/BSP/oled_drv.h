/**
  * @file    oled_drv.h
  * @brief   SSD1306 OLED 驱动层（FEAT-A2-03：0.96" 128×64, I2C1, 地址 0x3C）
  *
  * 架构（四层约定，参照 i2s_drv 模式）：
  *   应用层（freertos.c displayTask）→ 本驱动 → i2c.c 配置层 → HAL
  * 上下文：任务级调用（非 ISR）。全屏页式刷新 ~25ms @400kHz。
  *
  * 参考实现（R27）：4ilo/ssd1306-stm32HAL（framebuffer+页式刷新模式）
  */

#ifndef __OLED_DRV_H__
#define __OLED_DRV_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/* 初始化 SSD1306（含 charge pump）+ 显示自检画面（网格，AC-09）。
 * 返回 0=成功，非 0=I2C 通信失败次数 */
int32_t OLED_DRV_Init(void);

/* 绘制 32 频带柱状图到 framebuffer 并页式刷新整屏。
 * bands[32]：×100 整数能量（spectrum.c 输出口径）。内部做整数对数归一化。
 * 返回 0=成功，非 0=累计 I2C 失败次数（评审 Minor#4：自 init 起累计，
 * 含本次刷新新增失败——与 OLED_ErrCount 同源） */
int32_t OLED_DrawSpectrum(const uint32_t *bands);

/* I2C 错误累计计数（AC-07 证据接口；NACK/超时各计 1 次/页） */
uint32_t OLED_ErrCount(void);

#ifdef __cplusplus
}
#endif

#endif /* __OLED_DRV_H__ */
