/* USER CODE BEGIN Header */
/*
 * FreeRTOS Kernel V10.3.1
 * Portion Copyright (C) 2017 Amazon.com, Inc. or its affiliates.  All Rights Reserved.
 * Portion Copyright (C) 2019 StMicroelectronics, Inc.  All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
 * the Software, and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * http://www.FreeRTOS.org
 * http://aws.amazon.com/freertos
 *
 * 1 tab == 4 spaces!
 */
/* USER CODE END Header */

#ifndef FREERTOS_CONFIG_H
#define FREERTOS_CONFIG_H

/*-----------------------------------------------------------
 * Application specific definitions.
 *
 * These definitions should be adjusted for your particular hardware and
 * application requirements.
 *
 * These parameters and more are described within the 'configuration' section of the
 * FreeRTOS API documentation available on the FreeRTOS.org web site.
 *
 * See http://www.freertos.org/a00110.html
 *----------------------------------------------------------*/

/* USER CODE BEGIN Includes */
/* Section where include file can be added */
/* USER CODE END Includes */

/* Ensure definitions are only used by the compiler, and not by the assembler. */
#if defined(__ICCARM__) || defined(__CC_ARM) || defined(__GNUC__)
  #include <stdint.h>
  extern uint32_t SystemCoreClock;
#endif
#ifndef CMSIS_device_header
#define CMSIS_device_header "stm32f4xx.h"
#endif /* CMSIS_device_header */

#define configENABLE_FPU                         0
#define configENABLE_MPU                         0

#define configUSE_PREEMPTION                     1
#define configSUPPORT_STATIC_ALLOCATION          1
#define configSUPPORT_DYNAMIC_ALLOCATION         1
#define configUSE_IDLE_HOOK                      0
#define configUSE_TICK_HOOK                      0
#define configCPU_CLOCK_HZ                       ( SystemCoreClock )
#define configTICK_RATE_HZ                       ((TickType_t)1000)
#define configMAX_PRIORITIES                     ( 56 )
#define configMINIMAL_STACK_SIZE                 ((uint16_t)128)
/* ============================================================================
 * FreeRTOS 堆池大小 —— 调之前先把下面这段读完（2026-10-29 实测更新）
 * ============================================================================
 * 【为什么是 32768】两次扩容：15360 → 24576（2026-10-29 修复复位循环）→ 32768（2026-10-29
 *   晚，为 MPU6050 预留）。原值 15360 时启用 sht30 任务后启动即进入复位循环：
 *   串口打 [FATAL] FreeRTOS heap allocation FAILED（rtos_hooks.c:38）⇒ 关中断死循环
 *   ⇒ IWDG 约 1 s 复位 ⇒ 约 3 s 一圈。注意：这不是"堆被写坏"，是**池子用完**，
 *   vApplicationMallocFailedHook 的触发条件只是 pvPortMalloc 找不到够大的空闲块。
 *
 * 【实测账目】SWD 直读 heap_4 的 xFreeBytesRemaining（不是估算）：
 *   池 32768 − 空闲 16808 = **实际占用 15960 B**，且 xMinimumEverFree 与当前值相等
 *   ⇒ 启动后从未释放。历史：原配置 15360 ⇒ **缺口仅 600 B**；24576 时余 8616（35%）；
 *   32768 时余 16808（51%），为 MPU6050 任务（约 2 KB 栈 + TCB）留出空间。
 *
 * 【占用构成】7 个任务栈 = 11776 B（73.8%）：
 *     display 4096 ｜ spec 2048 ｜ sht30 2048 ｜ mbSlave 1024
 *     ｜ wdt 1024 ｜ **mbRxRecv 1024** ｜ default 512
 *   ⚠️ mbRxRecv 不在 MX_FREERTOS_Init 里，它由 eMBInit → portserial.c:77 的
 *      osThreadNew 创建。**算堆账漏掉它是最容易犯的错**（2026-10-29 已犯过一次）。
 *   其余 4184 B = 7×TCB + 音频队列(4×frame_msg_t=1040) + 2 个事件组 + 1 个定时器
 *   + heap_4 块头与对齐。
 *
 * 【真实失败点】不是任务创建 —— 6 个任务全建成、调度器已启动；是运行期
 *   eMBInit 里分配 mbRxRecv 那 1024 B 时失败。旁证：故障日志中
 *   "FreeModbus slave enabled on USART3" 出现 0 次，且红灯 PB0 不亮。
 *
 * 【还要加任务时，先看这里】RAM 128 KB 当前是这样切的（`size -A` + `nm` 实测）：
 *   ┌ .data 256 ｜ .bss 62252（含本堆池 32768 + FFT 表/缓冲 29484）  ← 到此 0x2000F430
 *   ├ ._user_heap_stack 53252 —— **无内容段，不占任何字节**，只是把位置计数器
 *   │    往上推 0xC000(49152) + 0x1000(4096)，用来做**链接期 RAM 预算检查**
 *   ├ .ccmram 0 —— 64 KB CCM 一个字节没用（0x10000000，⚠️ DMA 访问不到，别放 I/O 缓冲）
 *   └ 到 _estack(0x20020000) 还剩 **15312 B** 真空白（也是 MSP 的额外生长空间）
 *
 * ⭐⭐ **`_Min_Heap_Size` 的语义，别理解错**（`STM32F407XX_FLASH.ld:58`，0xC000）：
 *   - 它**不是**"划给 newlib malloc 的 48 KB"，**不占任何内存**（该段无 CONTENTS）；
 *   - 它**不是** malloc 的天花板 —— `Src/sysmem.c:56` 的 `_sbrk` 用的上界是
 *     `_estack - _Min_Stack_Size` = 0x2001F000，即 malloc 实际能用约 **71 KB**；
 *   - 它的真实作用：**给"堆 + 栈"的总和设一个静态预算**，链接时若 .bss 之后
 *     剩不下 48 KB+4 KB 就报错；同时保证运行期 malloc 有地方长，不会一头撞进栈。
 *   - 为什么 CubeMX 留 0x200、本工程留 0xC000：后者是建仓时（首个提交 4987512）
 *     手工设的，为"可能要用 malloc"留的余量。
 *   - ⚠️ **本工程并非"零 malloc"**（2026-10-29 SWD 实测更正）：
 *     工程代码确实零调用（App/BSP/Src 无 malloc/calloc/realloc/free），
 *     但 **stdio 路径用了一次**：`__sbrk_heap_end` = 0x2000D838，比 `_end`(0x2000D430)
 *     多出 **0x408 = 1,032 B**（≈ malloc(1024) + 8 B 管理头），首次输出时发生，
 *     之后 25 s 连续 printf 复测**不再增长**。
 *     ⇒ **`_Min_Heap_Size` 不能降到 0x400(1024)**——这次就要 1,032，会刚好卡死；
 *       要降就 0x800(2048) 起步。
 *
 * ⭐ **所以要扩本宏，第一步不是改链接脚本**：
 *   约束是 `_ebss + _Min_Heap_Size + _Min_Stack_Size ≤ _estack`
 *   ⇒ 当前 `_ebss`=0x2000F430，到 _estack 余 15,312 B
 *   ⇒ **本宏最多能加到 32768+15312 ≈ 48080 而不动 .ld**。
 *   只有要超过这个数，才需要把 `_Min_Heap_Size` 降下来（如 0x800）。
 *   ⚠️ 降它的代价：万一将来引入 newlib malloc 且用量大，堆可能长到与栈相撞
 *      （`_sbrk` 只跟"栈的保留下限"比，不跟当前 SP 比）—— 那块预算就是防这个的。
 * ⛔ **不要动 _Min_Stack_Size(4096)** —— 任务栈之外，所有 ISR 仍跑在这块 MSP 上。
 *
 * 【加完任务怎么验】别拍脑袋加 8 KB，用两条命令读真值：
 *   arm-none-eabi-nm build/soundDog.elf | grep -E "xFreeBytesRemaining|xMinimumEver"
 *   STM32_Programmer_CLI -c port=SWD -r32 0x<两变量地址> 8   # 各 4 B，读完必须 -rst
 *   判据：空闲 < 30% ⇒ 该考虑扩容，而不是等它复位。
 * ========================================================================== */
#define configTOTAL_HEAP_SIZE                    ((size_t)32768)
#define configMAX_TASK_NAME_LEN                  ( 16 )
#define configUSE_TRACE_FACILITY                 1
#define configUSE_16_BIT_TICKS                   0
#define configUSE_MUTEXES                        1
/* P1-2（审查缺陷）：此前两钩子在本文件根本不存在，由 FreeRTOS.h 兜底为 0 —— 一旦
 * 栈溢出/堆分配失败，现象只有 HardFault 卡死，毫无现场信息。启用后由 App/rtos_hooks.c
 * 提供钩子（cmsis_os2.c 有 __WEAK 空实现，强符号覆盖），打印崩溃任务后 for(;;) 挂起。 */
#define configCHECK_FOR_STACK_OVERFLOW           2
#define configUSE_MALLOC_FAILED_HOOK             1
#define configQUEUE_REGISTRY_SIZE                8
#define configUSE_RECURSIVE_MUTEXES              1
#define configUSE_COUNTING_SEMAPHORES            1
#define configUSE_PORT_OPTIMISED_TASK_SELECTION  0
/* USER CODE BEGIN MESSAGE_BUFFER_LENGTH_TYPE */
/* Defaults to size_t for backward compatibility, but can be changed
   if lengths will always be less than the number of bytes in a size_t. */
#define configMESSAGE_BUFFER_LENGTH_TYPE         size_t
/* USER CODE END MESSAGE_BUFFER_LENGTH_TYPE */

/* Co-routine definitions. */
#define configUSE_CO_ROUTINES                    0
#define configMAX_CO_ROUTINE_PRIORITIES          ( 2 )

/* Software timer definitions. */
#define configUSE_TIMERS                         1
#define configTIMER_TASK_PRIORITY                ( 2 )
#define configTIMER_QUEUE_LENGTH                 10
#define configTIMER_TASK_STACK_DEPTH             256

/* The following flag must be enabled only when using newlib */
#define configUSE_NEWLIB_REENTRANT          1

/* CMSIS-RTOS V2 flags */
#define configUSE_OS2_THREAD_SUSPEND_RESUME  1
#define configUSE_OS2_THREAD_ENUMERATE       1
#define configUSE_OS2_EVENTFLAGS_FROM_ISR    1
#define configUSE_OS2_THREAD_FLAGS           1
#define configUSE_OS2_TIMER                  1
#define configUSE_OS2_MUTEX                  1

/* Set the following definitions to 1 to include the API function, or zero
to exclude the API function. */
#define INCLUDE_vTaskPrioritySet             1
#define INCLUDE_uxTaskPriorityGet            1
#define INCLUDE_vTaskDelete                  1
#define INCLUDE_vTaskCleanUpResources        0
#define INCLUDE_vTaskSuspend                 1
#define INCLUDE_vTaskDelayUntil              1
#define INCLUDE_vTaskDelay                   1
#define INCLUDE_xTaskGetSchedulerState       1
#define INCLUDE_xTimerPendFunctionCall       1
#define INCLUDE_xQueueGetMutexHolder         1
#define INCLUDE_uxTaskGetStackHighWaterMark  1
#define INCLUDE_xTaskGetCurrentTaskHandle    1
#define INCLUDE_eTaskGetState                1

/*
 * The CMSIS-RTOS V2 FreeRTOS wrapper is dependent on the heap implementation used
 * by the application thus the correct define need to be enabled below
 */
#define USE_FreeRTOS_HEAP_4

/* Cortex-M specific definitions. */
#ifdef __NVIC_PRIO_BITS
 /* __BVIC_PRIO_BITS will be specified when CMSIS is being used. */
 #define configPRIO_BITS         __NVIC_PRIO_BITS
#else
 #define configPRIO_BITS         4
#endif

/* The lowest interrupt priority that can be used in a call to a "set priority"
function. */
#define configLIBRARY_LOWEST_INTERRUPT_PRIORITY   15

/* The highest interrupt priority that can be used by any interrupt service
routine that makes calls to interrupt safe FreeRTOS API functions.  DO NOT CALL
INTERRUPT SAFE FREERTOS API FUNCTIONS FROM ANY INTERRUPT THAT HAS A HIGHER
PRIORITY THAN THIS! (higher priorities are lower numeric values. */
#define configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY 5

/* Interrupt priorities used by the kernel port layer itself.  These are generic
to all Cortex-M ports, and do not rely on any particular library functions. */
#define configKERNEL_INTERRUPT_PRIORITY 		( configLIBRARY_LOWEST_INTERRUPT_PRIORITY << (8 - configPRIO_BITS) )
/* !!!! configMAX_SYSCALL_INTERRUPT_PRIORITY must not be set to zero !!!!
See http://www.FreeRTOS.org/RTOS-Cortex-M3-M4.html. */
#define configMAX_SYSCALL_INTERRUPT_PRIORITY 	( configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY << (8 - configPRIO_BITS) )

/* Normal assert() semantics without relying on the provision of an assert.h
header file. */
/* USER CODE BEGIN 1 */
#define configASSERT( x ) if ((x) == 0) {taskDISABLE_INTERRUPTS(); for( ;; );}
/* USER CODE END 1 */

/* Definitions that map the FreeRTOS port interrupt handlers to their CMSIS
standard names. */
#define vPortSVCHandler    SVC_Handler
#define xPortPendSVHandler PendSV_Handler

/* IMPORTANT: After 10.3.1 update, Systick_Handler comes from NVIC (if SYS timebase = systick), otherwise from cmsis_os2.c */

#define USE_CUSTOM_SYSTICK_HANDLER_IMPLEMENTATION 0

/* USER CODE BEGIN Defines */
/* Section where parameter definitions can be added (for instance, to override default ones in FreeRTOS.h) */
/* USER CODE END Defines */

#endif /* FREERTOS_CONFIG_H */
