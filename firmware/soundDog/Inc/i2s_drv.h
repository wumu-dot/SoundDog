/**
 * @file    i2s_drv.h
 * @brief   SoundDog I2S3 + INMP441 音频采集驱动
 *
 * 功能：
 *  - DMA 双缓冲 (ping-pong) 无丢帧 I2S 接收
 *  - INMP441 32-bit 帧数据提取为 16-bit PCM
 *  - 半满/全满回调，上层 DSP 任务异步处理
 */

#ifndef I2S_DRV_H
#define I2S_DRV_H

#include "stm32f4xx_hal.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ================================================================
 *  公共宏定义
 * ================================================================ */

/** 音频采样率 (Hz) */
#define I2S_SAMPLE_RATE         16000

/** 每帧 PCM 采样点数（耗时 = FRAME_SIZE/SAMPLE_RATE 秒） */
#define I2S_FRAME_SIZE          512    /** 512 samples = 32ms @ 16kHz */

/** DMA 半缓冲区大小 = 一帧大小 */
#define I2S_HALF_BUF_SIZE       I2S_FRAME_SIZE

/** DMA 总缓冲区 = 双倍帧大小 (两个半区) */
#define I2S_FULL_BUF_SIZE       (I2S_FRAME_SIZE * 2)

/* ================================================================
 *  数据类型
 * ================================================================ */

/** 音频帧：一帧 PCM 16-bit 数据，连带一个序号用于丢帧检测 */
typedef struct {
    int16_t  pcm[I2S_FRAME_SIZE];   /**< 16-bit 有符号 PCM 采样 */
    uint32_t frame_index;           /**< 累计帧序号（每次半满中断 +1） */
    uint32_t timestamp_ms;          /**< 帧时间戳 (HAL_GetTick) */
} audio_frame_t;

/** DMA 缓冲区接收回调（由上层注册） */
typedef void (*audio_frame_callback_t)(const audio_frame_t *frame);

/* ================================================================
 *  公共 API
 * ================================================================ */

/**
 * @brief  初始化并启动 I2S3 DMA 采集
 * @param  hi2s  I2S3 HAL 句柄 (CubeMX 生成的 &hi2s3)
 * @param  cb    每帧回调函数 (在 DMA 中断中调用，务必短小！)
 * @retval HAL_OK / HAL_ERROR / HAL_BUSY
 */
HAL_StatusTypeDef I2S_DRV_Init(I2S_HandleTypeDef *hi2s, audio_frame_callback_t cb);

/**
 * @brief  停止 I2S3 DMA 采集
 * @param  hi2s  I2S3 HAL 句柄
 */
void I2S_DRV_Stop(I2S_HandleTypeDef *hi2s);

/**
 * @brief  获取当前累计帧序号（用于丢帧检测）
 */
uint32_t I2S_DRV_GetFrameCount(void);

/**
 * @brief  从 INMP441 原始 32-bit 帧提取 16-bit PCM
 *
 * 实测（BUG-20260816-002）：本工程 I2S3 的 32-bit 帧与 INMP441 的 WS 槽
 * 存在 16 SCK（半槽）相位偏移，帧结构为 [16 位音频][16 位空闲(0xFFFF)]，
 * 音频位于 32 位字的高 16 位（bits[31:16]），低 16 位是空闲/上拉电平。
 *
 * 因此直接取高 16 位即为 16-bit 有符号 PCM（INMP441 24-bit 数据的高 16 位，
 * 即有效音频位）。若以后修正了 WS 相位使数据回到 bits[31:8]，需改回
 * (int16_t)(raw >> 8) 语义。
 *
 * @param  raw   DMA 收到的原始 uint32_t
 * @retval 16-bit 有符号 PCM 值
 */
static inline int16_t I2S_DRV_ExtractPCM(uint32_t raw)
{
    return (int16_t)(raw >> 16);   /* 音频在高 16 位（实测对齐） */
}

#ifdef __cplusplus
}
#endif

#endif /* I2S_DRV_H */
