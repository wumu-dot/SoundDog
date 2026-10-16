/**
  ******************************************************************************
  * @file    iwdg_drv.c
  * @author  SoundDog project
  * @brief   IWDG 独立看门狗驱动实现（FEAT-A7-02，方案 A）。
  *
  * 设计要点（三源预研坑清单，详见 FEAT-A7-02 §1.4 / §3.5）：
  *  - 超时留 >30% 余量：LSI 实测漂移约 ±7%（29.8~32.6kHz），超时取 1s 标称，
  *    喂狗周期 200ms → 即使 LSI 最慢（超时拉长到 ~1.07s）+ 调度抖动，5 个喂狗
  *    周期内仍有充足余量，杜绝"长时间阻塞被误复位"。
  *  - 禁 ISR 喂狗：只暴露给任务调用（IWDG_DRV_Refresh 由独立 watchdog 任务调用）。
  *  - 惰性启动：IWDG_DRV_Init 在喂狗任务上下文执行，规避"看门狗先使能、
  *    首次喂狗前先超时 → 烧录即循环复位"（FEAT §3.5 故障表第 3 行）。
  *  - 开发期 DBGMCU 冻结：调试 halt 时冻结计数，单步不触发复位（FEAT §3.5 第 4 行）。
  *  - STM32F4 的 DBGMCU 无需 RCC 时钟使能，直接写 DBGMCU->APB1FZ 即可。
  ******************************************************************************
  */
#include "iwdg_drv.h"
#include "stm32f4xx_hal.h"

static IWDG_HandleTypeDef s_hiwdg;

/* 超时配置：标称 1s（LSI=32kHz，PR=64 → 500Hz，RLR=500 → 1.0s） */
#define IWDG_DRV_PRESCALER   IWDG_PRESCALER_64
#define IWDG_DRV_RELOAD      500u

int IWDG_DRV_Init(void)
{
  HAL_StatusTypeDef st;

  s_hiwdg.Instance        = IWDG;
  s_hiwdg.Init.Prescaler  = IWDG_DRV_PRESCALER;
  s_hiwdg.Init.Reload     = IWDG_DRV_RELOAD;

  st = HAL_IWDG_Init(&s_hiwdg);
  if (st != HAL_OK)
  {
    return -1;
  }

#if (IWDG_DRV_DEV_FREEZE == 1)
  /* 开发期：调试 halt 时冻结 IWDG 计数（仅写 APB1FZ，F4 无需开 DBGMCU 时钟） */
  __HAL_DBGMCU_FREEZE_IWDG();
#endif

  return 0;
}

int IWDG_DRV_Refresh(void)
{
  return (HAL_IWDG_Refresh(&s_hiwdg) == HAL_OK) ? 0 : -1;
}