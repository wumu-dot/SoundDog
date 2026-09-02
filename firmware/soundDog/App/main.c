/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
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
#include "cmsis_os.h"
#include "dma.h"
#include "i2s.h"
#include "usart.h"
#include "gpio.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "i2s_drv.h"
#include "rs485_drv.h"
#include "i2c.h"
#include "oled_drv.h"
#include "fft.h"
#include "spectrum.h"
#include "audio_pipe.h"
#include "FreeRTOS.h"
#include "queue.h"
#include <stdio.h>
#include <string.h>
#include <math.h>
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

/* USER CODE BEGIN PV */

/* ---- A2-02: 音频帧队列（ISR → 任务），参考 CSDN 频谱工程/CubeMX 音频 demo 模式 ---- */
/* frame_msg_t / audio_queue_get() 声明移至 audio_pipe.h（freertos.c 的 specTask 共用） */
static QueueHandle_t audio_q = NULL;   /* 必须先于 I2S_DRV_Init 创建！深度 4（~32ms 缓冲余量） */

/* ---- 音频帧回调（在 DMA ISR 中调用，务必短小！）----
 * A2-02 架构：只做拷贝+入队（~10µs），FFT 计算移到 specTask（CPU 预算 3%）。
 * 串口修复（2026-08-29）：原 ISR 内 printf（max 统计/首帧提示）与任务侧
 * printf 并发调用 HAL_UART_Transmit（不可重入）→ 输出交错 + HAL 状态风险。
 * 现全部打印移至 specTask（运行期唯一打印者）；max 统计同样移过去，
 * 判据不变（AC-02：吹气 → max 明显变化）。 */
static void audio_frame_cb(const audio_frame_t *frame)
{
    frame_msg_t msg;
    memcpy(msg.pcm, frame->pcm, sizeof(msg.pcm));
    msg.frame_index = frame->frame_index;
    /* 满时丢弃本帧（音频流允许丢帧，不阻塞 ISR） */
    xQueueSendFromISR(audio_q, &msg, NULL);
}

/* 任务侧取队列的句柄暴露（freertos.c 的 defaultTask 使用） */
QueueHandle_t audio_queue_get(void)
{
    return audio_q;
}

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
void MX_FREERTOS_Init(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

int _write(int fd, char *ptr, int len)
{
    HAL_UART_Transmit(&huart1, (uint8_t *)ptr, len, 100);
    return len;
}

/* ---- FEAT-A2-01: FFT 自测（1kHz 正弦，boot 后跑一次）----
 * 判据（§3.2）：peak bin = 16（1000Hz / 62.5Hz），幅度有限值，无 NaN。
 * 临时验证代码，A2-01 验收后由 A2-02 换成真音频通路。
 */
static void fft_selftest(void)
{
    fft_ctx_t ctx;
    static float32_t in[FFT_LEN];
    static float32_t mag[FFT_BINS];

    if (fft_init(&ctx) != 0) {
        printf("FFT init FAIL\r\n");
        return;
    }

    /* 合成 1kHz 正弦：x[n] = 0.5*sin(2*pi*1000*n/16000) */
    for (int n = 0; n < FFT_LEN; n++) {
        in[n] = 0.5f * sinf(2.0f * 3.14159265f * 1000.0f * (float)n / 16000.0f);
    }

    if (fft_run(&ctx, in, mag) != 0) {
        printf("FFT run FAIL\r\n");
        return;
    }

    /* 找峰值 bin（跳过 DC bin0） */
    int peak = 1;
    float32_t peak_val = mag[1];
    for (int k = 2; k < FFT_BINS; k++) {
        if (mag[k] > peak_val) { peak_val = mag[k]; peak = k; }
    }

    /* 整数化打印（nano.specs 无 %f）：mag*10 显示，mag=640 即 64.0 */
    printf("FFT selftest: peak bin=%d (%u Hz) mag=%u/10 DC=%u/10\r\n",
           peak,
           (unsigned)(peak * 62.5f + 0.5f),
           (unsigned)(peak_val * 10.0f + 0.5f),
           (unsigned)(mag[0] * 10.0f + 0.5f));
    /* 期望输出：peak bin=16 (1000 Hz) mag=640/10 DC=0/10 */
}
/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_DMA_Init();
  MX_I2S3_Init();
  MX_USART1_UART_Init();
  /* USER CODE BEGIN 2 */

/* 点亮绿灯 = 程序已经跑到 main 的 USER CODE 2 区 */
HAL_GPIO_WritePin(LED_STATUS_GPIO_Port, LED_STATUS_Pin, GPIO_PIN_SET);

/* 先打印，确认 printf 和串口工作 */
printf("SoundDog boot OK, SYSCLK=%lu\r\n", HAL_RCC_GetSysClockFreq());

/* FEAT-A2-03: I2C1 + OLED 初始化（自检画面 ~1s 后由 displayTask 切柱状图）。
 * ⚠️ 顺序关键（BUG-20260829-006）：必须放在 xQueueCreate 等任何 FreeRTOS
 * 内核调用之前——调度器启动前内核调用会把 BASEPRI 抬到 0x50 不落下
 * （uxCriticalNesting 哨兵值 0xaaaaaaaa 机制），SysTick 被屏蔽，
 * OLED_DRV_Init 里的 HAL_Delay 会死循环（P0 黑屏真根因）。
 * boot 三行契约（boot OK / I2S_DRV_Init ret=0 / I2S DMA started）相对
 * 顺序不变，本块打印插在第 1 行之后，按序前缀匹配判据不受影响。 */
{
    MX_I2C1_Init();
    /* BUG 排查（黑屏全 NACK）：I2C 总线扫描 0x08~0x77，打印所有应答地址。
     * 正常应见 0x3C（或 0x3D 若模块焊盘反）。若一个都没有 → 硬件层
     * （接线反/松线/无上拉/模块坏）。扫描完继续 OLED 初始化不影响。 */
    printf("I2C scan:");
    for (uint8_t a = 0x08; a <= 0x77; a++) {
        HAL_StatusTypeDef st = HAL_I2C_IsDeviceReady(&hi2c1,
                (uint16_t)(a << 1), 2, 10);
        if (st == HAL_OK) { printf(" 0x%02X", a); }
    }
    printf(" (end)\r\n");
    int32_t oled_err = OLED_DRV_Init();
    if (oled_err == 0) {
        printf("OLED init OK (selftest grid)\r\n");
    } else {
        printf("OLED init FAIL err=%ld (check wiring/addr 0x3C)\r\n", (long)oled_err);
    }
}

/* FEAT-A2-02: 真音频通路初始化（FFT 实例 + 汉明窗 + 帧队列）
 * 顺序关键：队列必须先于 I2S_DRV_Init 创建，否则 DMA 起流后
 * 回调里 xQueueSendFromISR(NULL, ...) 直接硬错误。
 * ⚠️ 此后到 osKernelStart 之间 BASEPRI=0x50（见上方 BUG-006 注释），
 * 禁止插入任何 HAL_Delay/带超时轮询等依赖 SysTick 的调用 */
{
    /* fft_ctx_get 共享实例初始化（自测的局部 ctx 保留独立，互不干扰） */
    if (fft_init(fft_ctx_get()) != 0) {
        printf("spec init FAIL (fft)\r\n");
    }
    spec_init();
    audio_q = xQueueCreate(4, sizeof(frame_msg_t));
    if (audio_q == NULL) {   /* 堆耗尽防御（评审 Minor#4）：SpecTask 侧有 NULL
                              * 防御，ISR 侧没有——NULL 队列会让首个 DMA IRQ 硬错误 */
        printf("audio queue create FAIL\r\n");
        Error_Handler();
    }
    printf("A2-02: spectrum path ready\r\n");
}

/* FEAT-A2-01: FFT 自测（1kHz 正弦 → 期望 peak bin=16），AC-10 回归保留 */
fft_selftest();

/* 启动 I2S DMA 采集（回调只做拷贝+入队，FFT 在 freertos.c 的 specTask 消费） */
HAL_StatusTypeDef ret = I2S_DRV_Init(&hi2s3, audio_frame_cb);
printf("I2S_DRV_Init ret=%d (0=HAL_OK)\r\n", ret);

/* A5-01: USART3 + RS485 初始化 */
  MX_USART3_UART_Init();
  RS485_Init();

#if 0   /* 回环自测已移入 freertos.c RS485LoopTask（BUG-20260902-001）
         * ⚠️ 不能再放这里：本区在 osKernelStart 前、BASEPRI=0x50，
         * SysTick 被 FreeRTOS 屏蔽 → HAL_UART_Receive 的 HAL_GetTick 超时
         * 永不触发 → 死等卡死（与 BUG-20260829-006 OLED HAL_Delay 同根）。
         * 移入 RTOS 任务后 HAL_UART_Receive 超时才有效。 */
    {
      uint8_t txbuf[] = "RS485_LOOP";
      uint8_t rxbuf[16] = {0};
      HAL_StatusTypeDef st = RS485_Send(txbuf, sizeof(txbuf) - 1);
      printf("RS485_Send st=%d\r\n", st);
      st = RS485_Receive(rxbuf, sizeof(txbuf) - 1);
      printf("RS485_Receive st=%d rx='%s'\r\n", st, (char*)rxbuf);
      if (st == HAL_OK)
      {
        int match = (memcmp(txbuf, rxbuf, sizeof(txbuf) - 1) == 0);
        printf("RS485_LOOP %s\r\n", match ? "MATCH" : "MISMATCH");
      }
    }
#endif

  /* USER CODE END 2 */

  /* Init scheduler */
  osKernelInitialize();  /* Call init function for freertos objects (in cmsis_os2.c) */
  MX_FREERTOS_Init();

  /* Start scheduler */
  osKernelStart();

  /* We should never get here as control is now taken by the scheduler */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Configure the main internal regulator output voltage
  */
  __HAL_RCC_PWR_CLK_ENABLE();
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLM = 8;
  RCC_OscInitStruct.PLL.PLLN = 336;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV2;
  RCC_OscInitStruct.PLL.PLLQ = 4;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV4;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV2;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_5) != HAL_OK)
  {
    Error_Handler();
  }
}

/* USER CODE BEGIN 4 */

/* USER CODE END 4 */

/**
  * @brief  Period elapsed callback in non blocking mode
  * @note   This function is called  when TIM7 interrupt took place, inside
  * HAL_TIM_IRQHandler(). It makes a direct call to HAL_IncTick() to increment
  * a global variable "uwTick" used as application time base.
  * @param  htim : TIM handle
  * @retval None
  */
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
  /* USER CODE BEGIN Callback 0 */

  /* USER CODE END Callback 0 */
  if (htim->Instance == TIM7)
  {
    HAL_IncTick();
  }
  /* USER CODE BEGIN Callback 1 */

  /* USER CODE END Callback 1 */
}

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}
#ifdef USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
