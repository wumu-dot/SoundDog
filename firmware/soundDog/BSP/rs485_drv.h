/**
 * rs485_drv.h — RS485 半双工驱动接口
 *
 * 用法（回环测试示例）：
 *   RS485_Init();
 *   uint8_t tx[] = "RS485_LOOP";
 *   uint8_t rx[16];
 *   RS485_Send(tx, sizeof(tx)-1);      // 自发自收
 *   RS485_Receive(rx, sizeof(tx)-1);
 */
#ifndef RS485_DRV_H
#define RS485_DRV_H

#include "stm32f4xx_hal.h"

void            RS485_Init(void);                                   /* EN=0 接收态 */
void            RS485_SetDirTx(void);                               /* EN=1 发送态 */
void            RS485_SetDirRx(void);                               /* EN=0 接收态 */
HAL_StatusTypeDef RS485_Send(uint8_t *buf, uint16_t len);           /* 方向→发送→等TC→回接收 */
HAL_StatusTypeDef RS485_Receive(uint8_t *buf, uint16_t len);        /* 接收态阻塞收 */

#endif /* RS485_DRV_H */
