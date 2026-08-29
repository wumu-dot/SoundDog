/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * File Name          : freertos.c
  * Description        : Code for freertos applications
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
#include "FreeRTOS.h"
#include "task.h"
#include "main.h"
#include "cmsis_os.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "audio_pipe.h"        /* A2-02: frame_msg_t + audio_queue_get() */
#include "spectrum.h"        /* A2-02: spec_process + 峰值/噪声底 getter */
#include "oled_drv.h"        /* A2-03: OLED_DrawSpectrum + 错误计数 */
#include <string.h>
#include <stdio.h>
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
/* USER CODE BEGIN Variables */
/* FEAT-A2-02: 频谱任务（消费音频帧队列 → 256 点 FFT → 峰值打印）
 * 独立任务而非塞进 defaultTask：栈独立可控（printf + DSP 余量），
 * defaultTask 保持 CubeMX 原样（512B 栈放不下打印路径）。 */
static osThreadId_t specTaskHandle;
static const osThreadAttr_t specTask_attributes = {
  .name = "specTask",
  .stack_size = 512 * 4,              /* 2KB；大数组走静态，栈只留调用开销 */
  .priority = (osPriority_t) osPriorityNormal,
};

/* FEAT-A2-03: 显示任务（PROJECT_PLAN §5.1：Normal / 4096B / 200ms 周期）
 * 数据流：spec_display_get 临界区拷贝 32 频带 → OLED_DrawSpectrum 页式刷新。
 * 不碰 USART1（串口单写者）/ 不碰 FFT（fft_run 单消费者）。 */
static osThreadId_t displayTaskHandle;
static const osThreadAttr_t displayTask_attributes = {
  .name = "displayTask",
  .stack_size = 1024 * 4,             /* 4096B（PROJECT_PLAN 规格） */
  .priority = (osPriority_t) osPriorityNormal,
};
/* USER CODE END Variables */
/* Definitions for defaultTask */
osThreadId_t defaultTaskHandle;
const osThreadAttr_t defaultTask_attributes = {
  .name = "defaultTask",
  .stack_size = 128 * 4,
  .priority = (osPriority_t) osPriorityNormal,
};

/* Private function prototypes -----------------------------------------------*/
/* USER CODE BEGIN FunctionPrototypes */
static void SpecTask(void *argument);
static void DisplayTask(void *argument);
/* USER CODE END FunctionPrototypes */

void StartDefaultTask(void *argument);

void MX_FREERTOS_Init(void); /* (MISRA C 2004 rule 8.1) */

/**
  * @brief  FreeRTOS initialization
  * @param  None
  * @retval None
  */
void MX_FREERTOS_Init(void) {
  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* USER CODE BEGIN RTOS_MUTEX */
  /* add mutexes, ... */
  /* USER CODE END RTOS_MUTEX */

  /* USER CODE BEGIN RTOS_SEMAPHORES */
  /* add semaphores, ... */
  /* USER CODE END RTOS_SEMAPHORES */

  /* USER CODE BEGIN RTOS_TIMERS */
  /* start timers, add new ones, ... */
  /* USER CODE END RTOS_TIMERS */

  /* USER CODE BEGIN RTOS_QUEUES */
  /* add queues, ... */
  /* USER CODE END RTOS_QUEUES */

  /* Create the thread(s) */
  /* creation of defaultTask */
  defaultTaskHandle = osThreadNew(StartDefaultTask, NULL, &defaultTask_attributes);

  /* USER CODE BEGIN RTOS_THREADS */
  /* add threads, ... */
  /* FEAT-A2-02: 频谱消费任务（audio_frame_cb 生产 → 队列 → 本任务 FFT） */
  specTaskHandle = osThreadNew(SpecTask, NULL, &specTask_attributes);
  /* FEAT-A2-03: OLED 显示任务（200ms 周期取快照刷新） */
  displayTaskHandle = osThreadNew(DisplayTask, NULL, &displayTask_attributes);
  /* USER CODE END RTOS_THREADS */

  /* USER CODE BEGIN RTOS_EVENTS */
  /* add events, ... */
  /* USER CODE END RTOS_EVENTS */

}

/* USER CODE BEGIN Header_StartDefaultTask */
/**
  * @brief  Function implementing the defaultTask thread.
  * @param  argument: Not used
  * @retval None
  */
/* USER CODE END Header_StartDefaultTask */
void StartDefaultTask(void *argument)
{
  /* USER CODE BEGIN StartDefaultTask */
  /* Infinite loop */
  for(;;)
  {
    osDelay(1);
  }
  /* USER CODE END StartDefaultTask */
}

/* Private application code --------------------------------------------------*/
/* USER CODE BEGIN Application */
/* A1 回归输出：每 31 帧 max 统计（int32 累加，修复 -32768 负峰溢出）。
 * 2026-08-29 从 ISR 回调移入本任务——运行期串口唯一写入者，消除交错 */
static void frame_max_stat(uint32_t n_frames, const frame_msg_t *m)
{
  if ((n_frames % 31u) != 0u) { return; }
  int32_t max_val = 0;
  for (int i = 0; i < (int)I2S_PCM_PER_FRAME; i++) {
    int32_t v = m->pcm[i];
    if (v > 0 && v > max_val)        { max_val = v; }
    else if (v < 0 && (-v > max_val)) { max_val = -v; }
  }
  printf("[%lu] max=%ld\r\n", (unsigned long)m->frame_index, (long)max_val);
}

/* ---- FEAT-A2-02: 频谱消费任务 ----
 * 架构参考（R27）：
 *   - github.com/Ahmed-Marnissi/Audio_PDM：DMA 回调入队 → FFT 任务消费
 *   - gist davidnoronha1/mpmc.cpp：收满 256 点再统一加窗+FFT
 * 帧节拍 8ms（128 样本 @16kHz），两帧拼 16ms 窗口；处理 ~1ms @168MHz。 */
static void SpecTask(void *argument)
{
  (void)argument;

  QueueHandle_t q = audio_queue_get();

  /* 静态大缓冲：避免压爆任务栈（2KB 栈只留调用/printf 开销） */
  static frame_msg_t msg;
  static int16_t     pcm256[SPEC_WINDOW];
  static uint32_t    bands[SPEC_BANDS];

  if (q == NULL) {
    printf("specTask: audio queue NULL, halt\r\n");
    for (;;) { osDelay(1000); }
  }

  uint32_t n_frames = 0u;    /* 已收帧数（max 统计节拍，A1 回归 AC-02） */
  uint32_t n_proc = 0u;      /* 已处理窗口数（SPEC 打印节拍） */
  uint32_t idx1 = 0u;        /* 本窗首帧 index（丢帧校验，评审 Important#1） */
  uint32_t oled_last_err = 0u;  /* OLED 错误打印改道（评审 Important#2）见下 */

  /* 拼窗前提：SPEC_WINDOW 必须恰为两帧之和，否则 memcpy 错位（评审 Minor#7） */
  _Static_assert(SPEC_WINDOW == (2u * I2S_PCM_PER_FRAME),
                 "SPEC_WINDOW must equal 2 x I2S_PCM_PER_FRAME");

  for (;;)
  {
    /* 收两帧 → 拼一个 256 点窗口 */
    if (xQueueReceive(q, &msg, portMAX_DELAY) != pdTRUE) { continue; }
    n_frames++;
    if (n_frames == 1u) {
      /* 行首必须为 "I2S DMA started"（规格 §3.4 boot 三行契约，后续 FEAT 验收
       * 按此前缀匹配；A1-02 亦如此）。2026-08-29 从 ISR 移入本任务打印 */
      printf("I2S DMA started (first frame idx=%lu)\r\n",
             (unsigned long)msg.frame_index);
    }
    frame_max_stat(n_frames, &msg);
    memcpy(pcm256, msg.pcm, sizeof(msg.pcm));
    idx1 = msg.frame_index;

    if (xQueueReceive(q, &msg, portMAX_DELAY) != pdTRUE) { continue; }
    n_frames++;
    frame_max_stat(n_frames, &msg);
    /* 丢帧校验（评审 Important#1）：队列满时 ISR 按策略丢帧，若第二帧与首帧
     * 不连续，拼窗会产生相位跳变 → 虚假宽带分量。废弃本窗，下窗从本帧起拼。 */
    if (msg.frame_index != (idx1 + 1u)) { continue; }
    memcpy(&pcm256[I2S_PCM_PER_FRAME], msg.pcm, sizeof(msg.pcm));

    spec_process(pcm256, bands);

    /* A2-03: 更新显示快照（写侧临界区，~µs 级；displayTask 5Hz 读） */
    spec_display_update(bands);

    /* 16ms/窗 × 32 ≈ 0.5s 打印一次，避免串口刷屏
     * AC-01 证据行：peak_bin 期望 ∈ [15,17]（1kHz/62.5Hz=16） */
    if ((++n_proc % 32u) == 0u) {
      uint32_t pk    = spec_peak_bin();
      uint32_t mag   = spec_peak_mag();
      uint32_t noise = spec_noise_floor();
      uint32_t ratio = (noise != 0u) ? (mag / noise) : 0u;
      printf("SPEC peak_bin=%lu mag=%lu/100 noise=%lu/100 ratio=%lu\r\n",
             (unsigned long)pk, (unsigned long)mag,
             (unsigned long)noise, (unsigned long)ratio);

      /* OLED I2C 错误计数顺带打印（评审 Important#2：USART1 单写者——
       * BUG-003 规则，运行期只允许本任务写串口；原 DisplayTask 内打印
       * 与本任务并发调用 HAL_UART_Transmit 不可重入 → 丢行/交错）。
       * 仅错误计数变化时打印（AC-07 不刷屏判据不变）。 */
      uint32_t oled_err = OLED_ErrCount();
      if (oled_err != oled_last_err) {
        printf("OLED i2c err count=%lu (wiring/addr?)\r\n",
               (unsigned long)oled_err);
        oled_last_err = oled_err;
      }
    }
  }
}

/* ---- FEAT-A2-03: OLED 显示任务 ----
 * 节奏：先 osDelay(1000) 让 main.c 的自检网格画面可见（AC-09），
 * 之后每 200ms（PROJECT_PLAN §5.1）取频谱快照重绘。
 * 页式刷新 8×129B @400kHz ≈25ms 阻塞——Normal 优先级下可接受，
 * 音频链路（DMA ISR + specTask）不受影响。 */
static void DisplayTask(void *argument)
{
  (void)argument;
  static uint32_t bands32[SPEC_BANDS];   /* 快照拷贝（静态，避免压栈） */

  osDelay(1000);                          /* 自检画面展示窗口 */

  for (;;)
  {
    spec_display_get(bands32);
    OLED_DrawSpectrum(bands32);
    /* I2C 错误打印已移至 specTask（评审 Important#2：USART1 单写者，
     * BUG-003 规则——本任务不得写串口） */
    osDelay(200);
  }
}
/* USER CODE END Application */

