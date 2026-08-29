/* FEAT-A2-03: I2C1 配置层（手写仿 CubeMX i2s.c/usart.c 风格，不动 .ioc）
 * 参考实现（R27）：cornch-k/stm32-ssd1306-hal（F411 同构，I2C Fast Mode + framebuffer）
 */

#include "i2c.h"
#include "pin_config.h"

void Error_Handler(void);

I2C_HandleTypeDef hi2c1;

/* I2C1 init function */
void MX_I2C1_Init(void)
{
  hi2c1.Instance = I2C1;
  hi2c1.Init.ClockSpeed = 400000;            /* Fast Mode 400kHz（§3 设计定稿） */
  hi2c1.Init.DutyCycle = I2C_DUTYCYCLE_2;
  hi2c1.Init.OwnAddress1 = 0;
  hi2c1.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
  hi2c1.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
  hi2c1.Init.OwnAddress2 = 0;
  hi2c1.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
  hi2c1.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;
  if (HAL_I2C_Init(&hi2c1) != HAL_OK)
  {
    Error_Handler();
  }
}

void HAL_I2C_MspInit(I2C_HandleTypeDef* i2cHandle)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};
  if (i2cHandle->Instance == I2C1)
  {
    /* I2C1 clock enable */
    __HAL_RCC_I2C1_CLK_ENABLE();

    __HAL_RCC_GPIOB_CLK_ENABLE();

    /**I2C1 GPIO Configuration（与 pin_config.h 宏一致，R20）
    PB6     ------> I2C1_SCL
    PB7     ------> I2C1_SDA
    */
    GPIO_InitStruct.Pin = OLED_I2C_SCL_PIN | OLED_I2C_SDA_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_OD;      /* I2C 规定开漏 */
    GPIO_InitStruct.Pull = GPIO_PULLUP;          /* 内部上拉兜底（模块通常自带 4.7k） */
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    GPIO_InitStruct.Alternate = GPIO_AF4_I2C1;
    HAL_GPIO_Init(OLED_I2C_SCL_PORT, &GPIO_InitStruct);
  }
}

void HAL_I2C_MspDeInit(I2C_HandleTypeDef* i2cHandle)
{
  if (i2cHandle->Instance == I2C1)
  {
    /* Peripheral clock disable */
    __HAL_RCC_I2C1_CLK_DISABLE();

    /**I2C1 GPIO Configuration
    PB6     ------> I2C1_SCL
    PB7     ------> I2C1_SDA
    */
    HAL_GPIO_DeInit(GPIOB, OLED_I2C_SCL_PIN | OLED_I2C_SDA_PIN);
  }
}
