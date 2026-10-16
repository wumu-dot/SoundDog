/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    iwdg_drv.h
  * @author  SoundDog project
  * @brief   IWDG 独立看门狗驱动（FEAT-A7-02，方案 A：补 STM32CubeF4 官方 HAL 源）。
  *          - 独立喂狗任务在【任务上下文】周期调用 IWDG_DRV_Refresh()（禁 ISR 喂狗）。
  *          - 开发期可编译期开启 DBGMCU 冻结（调试 halt 时冻结 IWDG 计数，避免单步触发复位）。
  * @note    底层 HAL 源 stm32f4xx_hal_iwdg.c/.h 取自官方仓库（见 FEAT-A7-02 §来源），TO 复制不改。
  ******************************************************************************
  */
#ifndef IWDG_DRV_H
#define IWDG_DRV_H

#include <stdint.h>

/* 开发期开关：调试冻结看门狗计数（release 需置 0） */
#ifndef IWDG_DRV_DEV_FREEZE
#define IWDG_DRV_DEV_FREEZE   1
#endif

/**
  * @brief  IWDG 初始化 + 启动（在喂狗任务上下文调用，规避"使能早于首喂 → 立即复位"坑）。
  *         超时按 LSI 标称 32kHz 计算并留 >30% 余量；返回 HAL_OK 表示已启动。
  * @retval int  0=成功；<0 = HAL 状态错误
  */
int IWDG_DRV_Init(void);

/**
  * @brief  刷新 IWDG 计数（必须在任务上下文周期调用，禁止在 ISR 中喂）。
  * @retval int  0=成功；<0 = HAL 状态错误
  */
int IWDG_DRV_Refresh(void);

#endif /* IWDG_DRV_H */