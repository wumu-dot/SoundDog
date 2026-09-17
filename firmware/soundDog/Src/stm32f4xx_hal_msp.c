/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file         stm32f4xx_hal_msp.c
  * @brief        This file provides code for the MSP Initialization
  *               and de-Initialization codes.
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
/* USER CODE BEGIN Includes */

/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN TD */

/* USER CODE END TD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN Define */

/* USER CODE END Define */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN Macro */

/* USER CODE END Macro */

/* Private variables ---------------------------------------------------------*/
/* USER CODE BEGIN PV */

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* External functions --------------------------------------------------------*/
/* USER CODE BEGIN ExternalFunctions */

/* USER CODE END ExternalFunctions */

/* USER CODE BEGIN 0 */

/* USER CODE END 0 */
/**
  * Initializes the Global MSP.
  */
void HAL_MspInit(void)
{

  /* USER CODE BEGIN MspInit 0 */

  /* STM32F4 注：无需"释放 JTAG"操作。
   * STM32F1 才有 SWJ_CFG 字段（在 AFIO_MAPR）；STM32F4 的 SYSCFG_MEMRMP 只有 MEM_MODE 字段
   * (CMSIS 头 stm32f407xx.h:11629-11633 定义 SYSCFG_MEMRMP_MEM_MODE_Msk=0x3，bit[1:0])。
   * PB3/PB5 复位后默认 MODER=0b00（模拟），HAL_GPIO_Init 配 AF6_SPI3 后直接是 I2S3_CK/SD。
   * 历史代码误用 SYSCFG->MEMRMP bit[2:1] 操作 SWJ_CFG 是 STM32F1 拷贝残留，已清理。
   * 详见 docs/summary/lessons_summary.md（若已记录）。
   */

  /* USER CODE END MspInit 0 */

  __HAL_RCC_SYSCFG_CLK_ENABLE();
  __HAL_RCC_PWR_CLK_ENABLE();

  /* System interrupt init*/
  /* PendSV_IRQn interrupt configuration */
  HAL_NVIC_SetPriority(PendSV_IRQn, 15, 0);

  /* Peripheral interrupt init */
  /* PVD_IRQn interrupt configuration（P2-7 审查缺陷）：
   * 全工程无任何 HAL_PWR_ConfigPVD()/PWR_PVD_LEVEL_* 调用，PVD 无触发源、是纯死中断。
   * 原 SetPriority(PVD,0,0) 把抢占优先级设为 0 但 EnableIRQ 了它——一旦有人开启 PVD，
   * 将以最高抢占优先级 0 打断包括临界区在内的一切，且不在 FreeRTOS
   * configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY(5) 约束内。
   * 处置：不使能（无源死中断），并用最低优先级 15 兜底；需要真正用 PVD 时再改。
   * SetPriority 即使对未使能的 IRQ 也无副作用，保留 15 明确"与其余普通中断同级"。 */
  HAL_NVIC_SetPriority(PVD_IRQn, 15, 0);
  /* HAL_NVIC_EnableIRQ(PVD_IRQn); */

  /* USER CODE BEGIN MspInit 1 */

  /* USER CODE END MspInit 1 */
}

/* USER CODE BEGIN 1 */

/* USER CODE END 1 */
