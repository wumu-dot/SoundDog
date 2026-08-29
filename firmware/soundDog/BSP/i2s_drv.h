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

/**
 * INMP441 实测位流结构（BUG-002 RAW dump 2026-08-28 确诊，v10）：
 *   每 4 个 DMA 字（32-bit each）含 1 个真实采样：
 *     word[4k+0] = 0x0000_PPPP → PPPP = 24-bit 采样的高 16 位（bits[23:8]），即 16-bit PCM
 *     word[4k+1] = 0x0000_LL7F → LL = 24-bit 采样的低 8 位（bits[7:0]），0x7F 为 SD 悬空伪迹
 *     word[4k+2..3] = 0x0000FFFF → INMP441 非活动槽期间 SD 悬空电平（伪数据）
 *   提取：pcm = (int16_t)(word[4k] & 0xFFFF)。
 *   实测有效采样率 = 512 字/帧 ÷ 4 × 125 帧/s = 16000 Hz（与设计一致；
 *   BUG-002 旧注"采样率 2 倍"是把 DMA 字当采样点计数所致的误判）。
 */
#define I2S_PCM_PER_FRAME       (I2S_FRAME_SIZE / 4)   /**< 每半缓冲真实采样数 = 128 */

/* ================================================================
 *  数据类型
 * ================================================================ */

/** 音频帧：一帧 PCM 16-bit 数据，连带一个序号用于丢帧检测 */
typedef struct {
    int16_t  pcm[I2S_FRAME_SIZE];   /**< 16-bit 有符号 PCM 采样（有效长度 = samples） */
    uint16_t samples;               /**< 本帧有效采样数（= I2S_PCM_PER_FRAME） */
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
 * v10 确诊（BUG-20260816-002，RAW dump 2026-08-28）：音频位于 word[4k] 的
 * 低 16 位（24-bit 采样的高 16 位）。v9 的 & 0xFFFF 掩码本身没错，错在
 * 对全部 512 个字都提取——3/4 是悬空伪迹，max 统计被伪迹淹没。
 * 调用方必须先按 4 字周期选字（见 extract_frame / I2S_PCM_PER_FRAME）。
 *
 * @param  raw   被选中 word[4k] 的原始 uint32_t
 * @retval 16-bit 有符号 PCM 值
 */
static inline int16_t I2S_DRV_ExtractPCM(uint32_t raw)
{
    return (int16_t)(raw & 0xFFFF);   /* 音频在低 16 位（v10 确诊，仅对 word[4k] 有效） */
}

#ifdef __cplusplus
}
#endif

#endif /* I2S_DRV_H */
