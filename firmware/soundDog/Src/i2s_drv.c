/**
 * @file    i2s_drv.c
 * @brief   SoundDog I2S3 DMA 双缓冲采集驱动实现
 *
 * 工作原理：
 *   DMA Circular 模式 → 总缓冲区 = 2 × I2S_FRAME_SIZE
 *   ┌──────────────┬──────────────┐
 *   │  半区 0       │  半区 1       │
 *   │  512 samples  │  512 samples  │
 *   └──────────────┴──────────────┘
 *   半区 0 满 → HalfCplt IRQ → 提取 PCM → 调用回调
 *   半区 1 满 → Cplt IRQ    → 提取 PCM → 调用回调
 *
 * INMP441 数据提取：
 *   32-bit I2S 帧 [31:8] = 24-bit 有符号 PCM，低 8 位为 0。
 *   软件 >>8 得到 24-bit，再 >>8 取高 16 位供后续 FFT/MFCC 使用。
 */

#include "i2s_drv.h"
#include <string.h>

/* ================================================================
 *  静态变量
 * ================================================================ */

/** DMA 环形大缓冲区（2 × I2S_FRAME_SIZE 个 uint32_t） */
static uint32_t        dma_buf[I2S_FULL_BUF_SIZE];

/** 用户注册的帧回调 */
static audio_frame_callback_t  user_callback = NULL;

/** 累计帧计数器（丢帧检测用） */
static volatile uint32_t       frame_cnt = 0;

/** 当前正在处理的帧 (volatile — 中断和主循环都可能访问) */
static audio_frame_t           current_frame;

/** HAL I2S 句柄备份 */
static I2S_HandleTypeDef      *i2s_handle = NULL;

/* ================================================================
 *  内部辅助函数
 * ================================================================ */

/**
 * @brief  从 DMA 半缓冲区提取一帧 16-bit PCM
 * @param  src      DMA 半缓冲起始地址（uint32_t 数组）
 * @param  num      DMA 搬运的 half-word 数量（= I2S_HALF_BUF_SIZE）
 *
 * 注意：
 *  - 由于 DMA Data Width = 32-bit，src 实际是 uint32_t[]，
 *    但 HAL API 声明为 uint16_t*，取其低 16 位。
 *    在 32-bit 模式下，每次 DMA 传输 32-bit，src[i] 即为完整的 I2S 帧。
 *
 *  如果 HAL 对数据的字节序处理与预期不符，用下面的方法逐 16 进制打印排查：
 *    printf("%08lX\r\n", ((uint32_t*)src)[i]);
 */
static void extract_frame(const uint32_t *src, uint32_t count)
{
    if (count > I2S_FRAME_SIZE) {
        count = I2S_FRAME_SIZE;
    }

    for (uint32_t i = 0; i < count; i++) {
        current_frame.pcm[i] = I2S_DRV_ExtractPCM(src[i]);
    }
    current_frame.frame_index  = frame_cnt++;
    current_frame.timestamp_ms = HAL_GetTick();
}

/* ================================================================
 *  DMA 中断回调（HAL 弱定义，在此覆盖）
 * ================================================================ */

/**
 * @brief  DMA 半完成中断 — 半区 0 已满
 *
 * 此时 DMA 正在填充半区 1，CPU 可以安全读取半区 0。
 */
void HAL_I2S_RxHalfCpltCallback(I2S_HandleTypeDef *hi2s)
{
    if (hi2s->Instance != SPI3) return;  /* 仅处理 I2S3 */
    if (user_callback == NULL)   return;

    /* 提取半区 0: dma_buf[0 .. I2S_HALF_BUF_SIZE-1] */
    extract_frame(dma_buf, I2S_HALF_BUF_SIZE);
    user_callback(&current_frame);
}

/**
 * @brief  DMA 完成中断 — 半区 1 已满
 *
 * 此时 DMA 回绕到半区 0，CPU 可以安全读取半区 1。
 */
void HAL_I2S_RxCpltCallback(I2S_HandleTypeDef *hi2s)
{
    if (hi2s->Instance != SPI3) return;  /* 仅处理 I2S3 */
    if (user_callback == NULL)   return;

    /* 提取半区 1: dma_buf[I2S_HALF_BUF_SIZE .. I2S_FULL_BUF_SIZE-1] */
    extract_frame(&dma_buf[I2S_HALF_BUF_SIZE], I2S_HALF_BUF_SIZE);
    user_callback(&current_frame);
}

/* ================================================================
 *  公共 API
 * ================================================================ */

/**
 * @brief  初始化并启动采集
 */
HAL_StatusTypeDef I2S_DRV_Init(I2S_HandleTypeDef *hi2s, audio_frame_callback_t cb)
{
    if (hi2s == NULL) return HAL_ERROR;

    i2s_handle    = hi2s;
    user_callback = cb;
    frame_cnt     = 0;

    /* 清空 DMA 缓冲区 */
    memset(dma_buf, 0, sizeof(dma_buf));
    memset(&current_frame, 0, sizeof(current_frame));

    /* 启动 DMA 循环接收
     *
     * ⚠️ 踩坑记录（BUG-20260816-001）：
     * 本工程 DMA 配置为 32 位宽（PSIZE=WORD，CubeMX 生成），DMA NDTR 按 32 位字计数，
     * 每次 I2S 帧（32-bit）触发 1 次 32 位搬运。
     * 但 HAL_I2S_Receive_DMA 对 24/32-bit 格式会把 Size 翻倍（RxXferSize = Size<<1，
     * 其心智模型是"按 16 位半字计数"），因此：
     *   - 传 I2S_FULL_BUF_SIZE(1024) → NDTR=2048 字 = 8192 字节 → 溢出 dma_buf(4096B)
     *     4096 字节，启动后 ~32ms 开始踩坏紧随其后的 huart1 / FreeRTOS 全局区
     *     （pxReadyTasksLists 等）→ 串口打印丢失 + RTOS 调度器 HardFault。
     *   - 传 I2S_HALF_BUF_SIZE(512)  → NDTR=1024 字 = 4096 字节 = dma_buf 正好；
     *     半满中断在 512 采样处触发，与驱动双缓冲设计吻合。✓
     */
    HAL_StatusTypeDef ret = HAL_I2S_Receive_DMA(hi2s,
                                                (uint16_t *)dma_buf,
                                                I2S_HALF_BUF_SIZE);
    return ret;
}

/**
 * @brief  停止采集
 */
void I2S_DRV_Stop(I2S_HandleTypeDef *hi2s)
{
    if (hi2s == NULL) return;
    HAL_I2S_DMAStop(hi2s);
    user_callback = NULL;
    i2s_handle    = NULL;
}

/**
 * @brief  获取累计帧数
 */
uint32_t I2S_DRV_GetFrameCount(void)
{
    return frame_cnt;
}
