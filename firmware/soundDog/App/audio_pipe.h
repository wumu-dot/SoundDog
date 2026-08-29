/**
 * @file    audio_pipe.h
 * @brief   FEAT-A2-02 音频管道：ISR → 任务的帧队列接口（app 层胶水）
 *
 * 生产者：main.c 的 audio_frame_cb（DMA ISR 上下文，只做拷贝+入队）
 * 消费者：freertos.c 的 specTask（拼 256 点窗口 → FFT 幅度谱）
 *
 * 队列项 = 拷贝的整帧数据（值传递，回调返回后 DMA 缓冲即复用，无撕裂）。
 */

#ifndef AUDIO_PIPE_H
#define AUDIO_PIPE_H

#include "FreeRTOS.h"
#include "queue.h"
#include "i2s_drv.h"
#include <stdint.h>

/* 队列元素：一帧 128 个 int16 采样（16kHz 下 8ms） */
typedef struct {
    int16_t  pcm[I2S_PCM_PER_FRAME];
    uint32_t frame_index;
} frame_msg_t;

/* 帧队列句柄（main.c 中创建；任务侧调用获取） */
QueueHandle_t audio_queue_get(void);

#endif /* AUDIO_PIPE_H */
