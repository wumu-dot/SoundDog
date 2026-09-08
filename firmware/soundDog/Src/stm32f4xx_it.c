/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    stm32f4xx_it.c
  * @brief   Interrupt Service Routines.
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */

/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "stm32f4xx_it.h"
/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "port.h"   /* A4-04 Modbus：prvvUART*ISR 从站串口 ISR 入口声明 */
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN TD */

/* USER CODE END TD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
/* USER CODE BEGIN PV */

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

/* USER CODE END 0 */

/* External variables --------------------------------------------------------*/
extern DMA_HandleTypeDef hdma_spi3_rx;
extern TIM_HandleTypeDef htim7;
extern I2S_HandleTypeDef hi2s3;
extern UART_HandleTypeDef huart3;   /* A4-04 Modbus：USART3（RS485） */

/* USER CODE BEGIN EV */

/* USER CODE END EV */

/******************************************************************************/
/*           Cortex-M4 Processor Interruption and Exception Handlers          */
/******************************************************************************/
/**
  * @brief This function handles Non maskable interrupt.
  */
void NMI_Handler(void)
{
  /* USER CODE BEGIN NonMaskableInt_IRQn 0 */

  /* USER CODE END NonMaskableInt_IRQn 0 */
  /* USER CODE BEGIN NonMaskableInt_IRQn 1 */
   while (1)
  {
  }
  /* USER CODE END NonMaskableInt_IRQn 1 */
}

/**
  * @brief This function handles Hard fault interrupt.
  */
void HardFault_Handler(void)
{
  /* USER CODE BEGIN HardFault_IRQn 0 */

  /* USER CODE END HardFault_IRQn 0 */
  while (1)
  {
    /* USER CODE BEGIN W1_HardFault_IRQn 0 */
    /* USER CODE END W1_HardFault_IRQn 0 */
  }
}

/**
  * @brief This function handles Memory management fault.
  */
void MemManage_Handler(void)
{
  /* USER CODE BEGIN MemoryManagement_IRQn 0 */

  /* USER CODE END MemoryManagement_IRQn 0 */
  while (1)
  {
    /* USER CODE BEGIN W1_MemoryManagement_IRQn 0 */
    /* USER CODE END W1_MemoryManagement_IRQn 0 */
  }
}

/**
  * @brief This function handles Pre-fetch fault, memory access fault.
  */
void BusFault_Handler(void)
{
  /* USER CODE BEGIN BusFault_IRQn 0 */

  /* USER CODE END BusFault_IRQn 0 */
  while (1)
  {
    /* USER CODE BEGIN W1_BusFault_IRQn 0 */
    /* USER CODE END W1_BusFault_IRQn 0 */
  }
}

/**
  * @brief This function handles Undefined instruction or illegal state.
  */
void UsageFault_Handler(void)
{
  /* USER CODE BEGIN UsageFault_IRQn 0 */

  /* USER CODE END UsageFault_IRQn 0 */
  while (1)
  {
    /* USER CODE BEGIN W1_UsageFault_IRQn 0 */
    /* USER CODE END W1_UsageFault_IRQn 0 */
  }
}

/**
  * @brief This function handles Debug monitor.
  */
void DebugMon_Handler(void)
{
  /* USER CODE BEGIN DebugMonitor_IRQn 0 */

  /* USER CODE END DebugMonitor_IRQn 0 */
  /* USER CODE BEGIN DebugMonitor_IRQn 1 */

  /* USER CODE END DebugMonitor_IRQn 1 */
}

/******************************************************************************/
/* STM32F4xx Peripheral Interrupt Handlers                                    */
/* Add here the Interrupt Handlers for the used peripherals.                  */
/* For the available peripheral interrupt handler names,                      */
/* please refer to the startup file (startup_stm32f4xx.s).                    */
/******************************************************************************/

/**
  * @brief This function handles PVD interrupt through EXTI line 16.
  */
void PVD_IRQHandler(void)
{
  /* USER CODE BEGIN PVD_IRQn 0 */

  /* USER CODE END PVD_IRQn 0 */
  HAL_PWR_PVD_IRQHandler();
  /* USER CODE BEGIN PVD_IRQn 1 */

  /* USER CODE END PVD_IRQn 1 */
}

/**
  * @brief This function handles DMA1 stream2 global interrupt.
  */
void DMA1_Stream2_IRQHandler(void)
{
  /* USER CODE BEGIN DMA1_Stream2_IRQn 0 */

  /* USER CODE END DMA1_Stream2_IRQn 0 */
  HAL_DMA_IRQHandler(&hdma_spi3_rx);
  /* USER CODE BEGIN DMA1_Stream2_IRQn 1 */

  /* USER CODE END DMA1_Stream2_IRQn 1 */
}

/**
  * @brief This function handles TIM7 global interrupt.
  */
void TIM7_IRQHandler(void)
{
  /* USER CODE BEGIN TIM7_IRQn 0 */

  /* USER CODE END TIM7_IRQn 0 */
  HAL_TIM_IRQHandler(&htim7);
  /* USER CODE BEGIN TIM7_IRQn 1 */

  /* USER CODE END TIM7_IRQn 1 */
}

/**
  * @brief This function handles SPI3 global interrupt.
  * @note  DIAG v8: 最小化处理——只清 OVR 标志，不调用 HAL_I2S_IRQHandler
  *        （HAL 的 OVR 处理会禁用中断并重置 State，反而破坏 DMA 会话）。
  */
void SPI3_IRQHandler(void)
{
  if (__HAL_I2S_GET_FLAG(&hi2s3, I2S_FLAG_OVR)) {
    __HAL_I2S_CLEAR_OVRFLAG(&hi2s3);
  }
}

/**
 * @brief A4-04 Modbus：USART3（RS485）中断入口。
 *
 * 实现忠实于 quanghona/Freemodbus-CMSIS-RTOS2-STM32 的 Core/Src/stm32f4xx_it.c
 * （R27/R32，本工程电参数不变，仅复用其代码形态）：
 *   - RXNE：收单字节 → prvvUARTRxReceiveCharISR（写入 portserial 环形缓冲）；
 *   - IDLE：总线空闲=帧尾 → prvvUARTRxISR（置位，唤醒收帧线程 drain）；
 *   - TC：  发送完成 → prvvUARTTxReadyISR（推动协议栈继续发下一字节）；
 *   - ORE： 溢出清标记防卡死；
 *   - 末尾仍调 HAL_UART_IRQHandler：喂 HAL_UART_Transmit_IT 的 TXE 内部状态机
 *     （quanghona 关键约定——PutByte 走 HAL_IT，TXE 必须经 HAL 驱动才能续传）。
 */
void USART3_IRQHandler(void)
{
  if (__HAL_UART_GET_FLAG(&huart3, UART_FLAG_RXNE))
  {
    prvvUARTRxReceiveCharISR((CHAR)(huart3.Instance->DR & 0xFF));
    __HAL_UART_CLEAR_FLAG(&huart3, UART_FLAG_RXNE);
  }
  if (__HAL_UART_GET_FLAG(&huart3, UART_FLAG_IDLE))
  {
    prvvUARTRxISR();
    __HAL_UART_CLEAR_IDLEFLAG(&huart3);
  }
  if (__HAL_UART_GET_FLAG(&huart3, UART_FLAG_TC))
  {
    prvvUARTTxReadyISR();
    __HAL_UART_CLEAR_FLAG(&huart3, UART_FLAG_TC);
  }
  if (__HAL_UART_GET_FLAG(&huart3, UART_FLAG_ORE))
  {
    (void)(huart3.Instance->DR & 0x01FF);   /* 读 DR 清 ORE（复用 quanghona 兜底） */
    __HAL_UART_CLEAR_OREFLAG(&huart3);
  }
  HAL_UART_IRQHandler(&huart3);
}

/* USER CODE BEGIN 1 */

/* USER CODE END 1 */
