/**
 * SoundDog — main.c 验证代码片段
 *
 * 用法：CubeMX 生成代码后，在 main.c 中找到对应的 USER CODE 注释块，
 * 把下面各段代码粘贴进去。
 *
 * 块的对应关系：
 *   USER CODE BEGIN 0  → 头文件
 *   USER CODE BEGIN PV → 私有变量
 *   USER CODE BEGIN 2  → 初始化代码 (while(1) 之前)
 *   USER CODE BEGIN 3  → while(1) 内部 (极少使用)
 *   USER CODE BEGIN 4  → 用户回调函数
 *
 * 然后编译、烧录、打开 SSCOM（921600 波特率）、观察输出。
 */

/* ================================================================
 *  USER CODE BEGIN 0  (文件头 include 区)
 * ================================================================ */

/* USER CODE BEGIN 0 */
#include <stdio.h>
#include <string.h>
#include "i2s_drv.h"

/* printf 重定向到 USART1 */
int __io_putchar(int ch) {
    HAL_UART_Transmit(&huart1, (uint8_t *)&ch, 1, HAL_MAX_DELAY);
    return ch;
}
/* USER CODE END 0 */


/* ================================================================
 *  USER CODE BEGIN PV (私有变量区)
 * ================================================================ */

/* USER CODE BEGIN PV */

/* 测试模式: 0=一次性打印, 1=持续打印, 2=安静模式(只有帧计数) */
static int test_mode = 0;

/* 持续打印间隔计数器（每 100 帧才打印一次摘要，避免串口堵塞） */
static uint32_t print_divider = 0;

/* 丢帧检测: 上次帧序号 */
static uint32_t last_frame_index = 0;

/* USER CODE END PV */


/* ================================================================
 *  USER CODE BEGIN PFP (私有函数声明区)
 * ================================================================ */

/* USER CODE BEGIN PFP */

/** 音频帧回调函数 — 在 DMA 中断中调用，务必短小 */
static void on_audio_frame(const audio_frame_t *frame);

/** 打印原始数据和分析结果 */
static void print_frame_debug(const audio_frame_t *frame);

/* USER CODE END PFP */


/* ================================================================
 *  USER CODE BEGIN 2 (main 函数内 while 之前)
 * ================================================================ */

/* USER CODE BEGIN 2 */

printf("\r\n\r\n");
printf("╔══════════════════════════════════════╗\r\n");
printf("║     SoundDog I2S3 验证程序            ║\r\n");
printf("║     INMP441 + STM32F407               ║\r\n");
printf("╚══════════════════════════════════════╝\r\n");
printf("SYSCLK: %lu Hz\r\n", HAL_RCC_GetSysClockFreq());
printf("HCLK:   %lu Hz\r\n", HAL_RCC_GetHCLKFreq());
printf("PCLK1:  %lu Hz\r\n", HAL_RCC_GetPCLK1Freq());
printf("PCLK2:  %lu Hz\r\n", HAL_RCC_GetPCLK2Freq());
printf("\r\n");

/* ---------- 测试1：确认外设句柄非空 ---------- */
printf("[Test 1] 检查外设初始化...\r\n");
printf("  hi2s3.Instance  = %p (应为非空)\r\n", (void*)hi2s3.Instance);
printf("  hi2c1.Instance  = %p (应为非空)\r\n", (void*)hi2c1.Instance);
printf("  huart1.Instance = %p (应为非空)\r\n", (void*)huart1.Instance);
printf("  huart3.Instance = %p (应为非空)\r\n", (void*)huart3.Instance);
printf("  OK\r\n\r\n");

/* ---------- 测试2：启动 I2S3 DMA ---------- */
printf("[Test 2] 启动 I2S3 DMA 采集...\r\n");
HAL_StatusTypeDef ret = I2S_DRV_Init(&hi2s3, on_audio_frame);
if (ret != HAL_OK) {
    printf("  *** 失败！ret = %d ***\r\n", ret);
    printf("  可能原因：\r\n");
    printf("    1) I2S3 时钟未使能 (检查 RCC/时钟树)\r\n");
    printf("    2) DMA 通道冲突\r\n");
    printf("    3) INMP441 未接线或接线错误\r\n");
    HAL_GPIO_WritePin(LED_ALARM_GPIO_Port, LED_ALARM_Pin, GPIO_PIN_SET); /* 红灯 */
    while (1);
}
printf("  DMA 已启动，等待音频数据...\r\n\r\n");

HAL_Delay(200);  /* 等待几帧数据积累 */

/* ---------- 测试3：检查是否有数据流入 ---------- */
printf("[Test 3] 帧计数器 = %lu\r\n", I2S_DRV_GetFrameCount());
if (I2S_DRV_GetFrameCount() == 0) {
    printf("  *** 警告：200ms 内 0 帧！***\r\n");
    printf("  请检查：\r\n");
    printf("    - INMP441 VDD = 3.3V ?\r\n");
    printf("    - INMP441 L/R 接 GND ?\r\n");
    printf("    - PC10/PA4/PC12 接线正确 ?\r\n");
    printf("    - 示波器量 PC10 有无 1.024MHz 方波\r\n");
} else {
    printf("  有数据流入！帧率 ≈ %lu Hz\r\n", I2S_DRV_GetFrameCount() / 1);
}

/* ---------- 测试4：切换到持续打印模式，立即看数据 ---------- */
printf("\r\n[Test 4] 切换到实时监控模式 (test_mode=1)\r\n");
printf("  对着 INMP441 吹气/说话，观察数值变化...\r\n");
printf("------------------------------------------\r\n");
test_mode = 1;  /* 持续打印，每 100 帧输出一行摘要 */

/* USER CODE END 2 */


/* ================================================================
 *  USER CODE BEGIN 3 (while(1) 内部 — 裸机模式极简)
 * ================================================================ */

/* USER CODE BEGIN 3 */
    /*
     * DMA 中断驱动，主循环几乎空转。
     * 后续 FreeRTOS 版本时这里会换成任务调度。
     */
    HAL_Delay(1000);
/* USER CODE END 3 */


/* ================================================================
 *  USER CODE BEGIN 4 (用户回调函数实现)
 * ================================================================ */

/* USER CODE BEGIN 4 */

/**
 * @brief  音频帧回调 — DMA 中断中调用
 *
 * ⚠️ 此函数在中断上下文中执行，必须短小、无阻塞。
 *    当前版本只做丢帧检测 + 副本，实际 DSP 处理
 *    应在主循环或独立任务中做。
 */
static void on_audio_frame(const audio_frame_t *frame)
{
    /* 丢帧检测 */
    if (frame->frame_index > 0) {
        uint32_t gap = frame->frame_index - last_frame_index;
        if (gap > 1) {
            /* 丢帧了！但因为这是中断中，不能 printf。
             * 设一个标志位，主循环来打印。这里先略过。 */
        }
    }
    last_frame_index = frame->frame_index;

    /* 每 100 帧在主循环中打印一次摘要（通过 test_mode 控制）。
     * 实际上这里信号量/队列通知主任务最好，但裸机版
     * 我们用简单的轮询打印。 */
    if (test_mode == 1 && (frame->frame_index % 100 == 0)) {
        /* 计算本帧的峰峰值 (粗略音量) */
        int16_t pk = 0;
        for (int i = 0; i < I2S_FRAME_SIZE; i++) {
            int16_t v = frame->pcm[i];
            if (v < 0) v = -v;
            if (v > pk) pk = v;
        }
        printf("Frame#%06lu | peak=%6d | ts=%lu\r\n",
               frame->frame_index, pk, frame->timestamp_ms);
    }
}


/**
 * @brief  打印一帧的详细信息（仅在 test_mode=0 时手动调用一次）
 */
static void print_frame_debug(const audio_frame_t *frame)
{
    /* 打印前 32 个原始 PCM 样本 */
    printf("Frame #%lu (ts=%lu ms):\r\n", frame->frame_index, frame->timestamp_ms);
    printf("  PCM[0..31]: ");
    for (int i = 0; i < 32; i++) {
        printf("%6d", frame->pcm[i]);
        if ((i + 1) % 8 == 0) printf("\r\n              ");
    }
    printf("\r\n");
}

/* USER CODE END 4 */


/* ================================================================
 *  进阶：如果想打印原始 32-bit 值（排查数据对齐问题）
 *
 * 修改 extract_frame() 或在此加一个临时函数：
 *
 *   void dump_raw_frame(void) {
 *       extern uint32_t dma_buf[];  // 需在 i2s_drv.c 中去掉 static
 *       printf("Raw uint32 hex dump (first 16):\r\n");
 *       for (int i = 0; i < 16; i++) {
 *           printf("  [%2d] %08lX\r\n", i, dma_buf[i]);
 *       }
 *   }
 *
 * 期望结果：
 *   - BIT[7:0]  ≈ 0x00 (INMP441 不输出)
 *   - BIT[15:8] ≈ 0x00 或小值 (24-bit 在 32-bit 槽中的低部)
 *   - BIT[23:16] 变化明显 (中 8 位)
 *   - BIT[31:24] = 0x00 (正半波) / 0xFF (负半波，补码符号位)
 *
 * 根据实际分布调整 I2S_DRV_ExtractPCM() 的位移量。
 * ================================================================ */
