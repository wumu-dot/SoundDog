/**
  * @file    i2c.h
  * @brief   I2C1 配置层（FEAT-A2-03：OLED SSD1306 总线，手写仿 CubeMX 风格）
  *
  * 引脚（pin_config.h 权威，R20）：PB6=SCL / PB7=SDA，AF4_I2C1，开漏+内部上拉。
  * 模式：Fast Mode 400kHz（F4 由 HAL 按 ClockSpeed 自动算 CCR，无 TIMINGR）。
  * 总线规划：OLED(0x3C) + 未来 SHT30(0x44) 共用。
  */

#ifndef __I2C_H__
#define __I2C_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "stm32f4xx_hal.h"

extern I2C_HandleTypeDef hi2c1;

void MX_I2C1_Init(void);

#ifdef __cplusplus
}
#endif

#endif /* __I2C_H__ */
