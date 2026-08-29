/**
 * rs485_drv.c — RS485 半双工驱动（USART3 + TTL-RS485 模块 EN 方向控制）
 *
 * 引脚（pin_config.h 权威）：
 *   USART3_TX = PD8   USART3_RX = PD9   RS485_EN = PD11
 *
 * 半双工时序：
 *   发送：EN(PD11)=1 → HAL_UART_Transmit 写完 → 等 TC 标志 → EN=0
 *   空闲：EN 恒 = 0（接收态）
 *   红线：同一时刻只允许一端发送；EN 只在发送窗口为高
 */
#include "rs485_drv.h"
#include "main.h"
#include "usart.h"
#include "pin_config.h"

/* ------------------------------------------------------------------ */
/* 内部辅助：方向切换                                                  */
/* ------------------------------------------------------------------ */
void RS485_SetDirTx(void)
{
  HAL_GPIO_WritePin(RS485_DE_PORT, RS485_DE_PIN, GPIO_PIN_SET);   /* 高 = 发送 */
}

void RS485_SetDirRx(void)
{
  HAL_GPIO_WritePin(RS485_DE_PORT, RS485_DE_PIN, GPIO_PIN_RESET); /* 低 = 接收 */
}

/* ------------------------------------------------------------------ */
/* 初始化：EN 初始为低（接收态），防上电瞬间误发                       */
/* ------------------------------------------------------------------ */
void RS485_Init(void)
{
  RS485_SetDirRx();
}

/* ------------------------------------------------------------------ */
/* 发送：方向→发送→等 TC→回接收                                       */
/* ------------------------------------------------------------------ */
HAL_StatusTypeDef RS485_Send(uint8_t *buf, uint16_t len)
{
  HAL_StatusTypeDef st;

  RS485_SetDirTx();                                   /* 进入发送态 */

  st = HAL_UART_Transmit(&huart3, buf, len, 100);     /* 阻塞发送 */

  /* 等 TC：确保移位寄存器最后一个字节已发出，再切回接收态，防截断 */
  while (__HAL_UART_GET_FLAG(&huart3, UART_FLAG_TC) == RESET)
  {
  }

  RS485_SetDirRx();                                   /* 回接收态 */

  return st;
}

/* ------------------------------------------------------------------ */
/* 接收：接收态直接收（阻塞）                                          */
/* ------------------------------------------------------------------ */
HAL_StatusTypeDef RS485_Receive(uint8_t *buf, uint16_t len)
{
  return HAL_UART_Receive(&huart3, buf, len, 100);
}
