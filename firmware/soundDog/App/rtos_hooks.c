/*
 * App/rtos_hooks.c
 *
 * P1-2（审查缺陷）修复：FreeRTOS 栈溢出 / 堆分配失败钩子。
 *
 * 背景：
 *   - cmsis_os2.c:2428 / :2442 提供 __WEAK 空实现，本文件以强符号覆盖之。
 *   - 启用开关在 Inc/FreeRTOSConfig.h：configCHECK_FOR_STACK_OVERFLOW=2、
 *     configUSE_MALLOC_FAILED_HOOK=1。
 *   - 一旦触发即系统已处于异常态：__disable_irq() 关中断，轮询 USART1 打印
 *     崩溃任务名后 for(;;) 挂起。能打出任务名即可定位是哪个任务栈吃紧。
 *
 * §8.3-C 已知约束（审查官核过，不阻断）：
 *   两钩子均运行于 PendSV 上下文，而 PendSV 与 SysTick 同为优先级 15（同优先级
 *   不可互相抢占）→ SysTick 不推进 → HAL_UART_Transmit 的 Timeout 实际失效。
 *   正常情形 TXE/TC 轮询不依赖 tick，字节仍能发出；但若 USART 时钟未就绪，会
 *   永久停在 HAL 内部而到不了下方 for(;;)。两种结局都卡死，安全目标已达成。
 */
#include "FreeRTOS.h"
#include "task.h"
#include "usart.h"
#include <string.h>

/*
 * 关中断 + 阻塞发一字符串。全部靠 USART1->DR/SR 直接轮询（不依赖 HAL 超时，
 * 规避 PendSV 下 SysTick 冻结导致 Timeout 永不触发的 §8.3-C 问题）。
 */
static void dbg_print(const char *str)
{
    while (*str != '\0') {
        /* 等 TXE（发送数据寄存器空） */
        while ((huart1.Instance->SR & USART_SR_TXE) == 0) { }
        huart1.Instance->DR = (uint16_t)(uint8_t)(*str++);
    }
    /* 等 TC（发送完成），确保最后字节出线后进入告警死循环 */
    while ((huart1.Instance->SR & USART_SR_TC) == 0) { }
}

void vApplicationMallocFailedHook(void)
{
    __disable_irq();
    dbg_print("\r\n[FATAL] FreeRTOS heap allocation FAILED\r\n");
    for (;;) { }
}

void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName)
{
    (void)xTask;
    __disable_irq();
    /* 任务名常量缓冲，禁止用任务栈上数据（栈已坏） */
    static char s_name[configMAX_TASK_NAME_LEN + 1];
    s_name[0] = '\0';
    if (pcTaskName != NULL) {
        (void)strncpy(s_name, pcTaskName, configMAX_TASK_NAME_LEN);
        s_name[configMAX_TASK_NAME_LEN] = '\0';
    }
    dbg_print("\r\n[FATAL] STACK OVERFLOW in task: ");
    dbg_print(s_name);
    dbg_print("\r\n");
    for (;;) { }
}