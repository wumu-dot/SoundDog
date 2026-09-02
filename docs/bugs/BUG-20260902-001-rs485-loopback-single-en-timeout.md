# RS485 自发自收回环 st=3（HAL\_TIMEOUT）：双根因（预调度器期 SysTick 屏蔽 + 单 EN 半双工不可自发自收）

- **Bug ID**：BUG-20260902-001

- **严重等级**：P1-功能（A5-01 回环自测无法通过，卡住阶段 4；不阻断 A4-04 主线，但阻塞 RS485 通道验证）

- **发现日期**：2026-09-02

- **修复日期**：2026-09-02（回环移入 RTOS 任务；单 EN 部分确认物理限制，改双模块对端方案）

- **关联 FEAT**：FEAT-A5-01（USART3-RS485 模块驱动）

## 现象描述

烧录 A5-01 回环自测固件，板载 CH340（USART1）串口打印：

```
RS485_Send st=0        ← 发送成功
RS485_Receive st=3     ← HAL_TIMEOUT：收不到自己刚发的数据
RS485_LOOP MISMATCH
```

修复前更严重：打印停在 `RS485_Send st=0` 后**永久无输出**，程序卡死不进频谱任务（SSCOM 只剩启动 Boot 之前的频谱残留，无新的 SPEC/BANDS 行）。

## 复现步骤

1. 烧录含回环自测的固件（回环代码在 `main.c` `osKernelStart()` 前、`MX_USART3_UART_Init()+RS485_Init()` 之后）
2. 观察 USART1 打印：`RS485_Send st=0` 后停住（修复前）或 `RS485_Receive st=3`（修复后）
3. MAX3485 模块：VCC=3.3V、GND 共地、RXD←PD8、TXD→PD9、EN←PD11、A/B 短接

## 根因分析（双根因，逐级证据链）

### 根因①：预调度器期 `HAL_UART_Receive` 的超时依赖被屏蔽的 SysTick（时序坑）

`RS485_Receive` 内部调 `HAL_UART_Receive(huart3, ..., 100)`，其超时用 `HAL_GetTick()`（读 `uwTick`，由 SysTick 中断累加）。而回环代码放在 `osKernelStart()` 前，此时 FreeRTOS port 已把 `BASEPRI` 抬到 `0x50`（= `configMAX_SYSCALL_INTERRUPT_PRIORITY=5` 左移 4 位），SysTick 中断被屏蔽 → `uwTick` 冻结 → `HAL_GetTick()` 永不增长 → 超时 100 形同虚设 → `HAL_UART_Receive` 无限等待 RXNE 置位 → 死锁（修复前卡死根因）。

**与 BUG-20260829-006（OLED** **`HAL_Delay`** **死循环）同根**：都是"预调度器期调用依赖 SysTick 的 HAL 阻塞函数"。GDB 证据链（沿用 BUG-006 已实测的 `basepri=0x50` + `uwTick` 冻结机制）成立。三源佐证：

- **源码**：HAL `UART_WaitOnFlagUntilTimeout()` 用 `HAL_GetTick()-Tickstart` 判超时；FreeRTOS CM4F port.c `vPortExitCritical` 哨兵值机制

- **Issue/社区**：[ST Community "Peripheral use before FreeRTOS Kernel Init makes the MCU to infinite loop"](https://community.st.com/t5/stm32-mcu-products/peripheral-use-before-freertos-kernel-init-makes-the-mcu-to/m-p/572825)（确认 `HAL_UART_Receive`/`HAL_Delay` 在调度器前因 GetTick 恒定卡死）；[FreeRTOS Troubleshooting FAQ](https://research.freertos.org/Why-FreeRTOS/FAQs/Troubleshooting/)（调度器前调用内核 API 会永久关中断）；[FreeRTOS forums prvPortStartFirstTask BASEPRI 0x50](https://forums.freertos.org/t/prvportstartfirsttask-fails-at-svc-0/18288)

- **坑清单**：BUG-20260829-006（本司先例，同机制）

### 根因②：单 `EN`（DE+`/RE` 同控）半双工下自发自收物理不成立（电气/协议坑）

A5-01 记录模块丝印为单 `EN`（DE 与 `/RE` 绑同一根）。`RS485_SetDirTx()` 置 PD11=1 → DE=1（发送使能）**且** `/RE=1`（低有效 → 禁收）→ RO 无输出；发完等 TC 后置 PD11=0 → `/RE=0` 才开收，但差分数据此时已传输完毕。故 **A-B 短接自发自收在此模块上必然收不到**，`st=3` 属**预期**。

修根因①后（st=3 能正常超时返回）仍 MISMATCH，即此物理限制。

## 修复方案

1. **根因①**：回环自测移出裸机区，放入 RTOS 任务（`freertos.c` `RS485LoopTask`，一次性、跑完自删）。RTOS 调度器启动后 SysTick 正常，`HAL_UART_Receive` 超时才有效。
2. **根因②**：单 EN 模块**不能**自发自收。采用**双模块对端**（方案 B）：板上 USART3 → MAX3485#1 → A/B 差分 → MAX3485#2 → 对端串口（独立 USB-TTL/PC）。A5-01 原"A-B 短接回环"步骤标记为不可行，改为双模块对接。

## 影响文件

- `firmware/soundDog/App/main.c`（回环自测 `#if 0` 注释更新：说明已移入 RTOS + 禁放原因）

- `firmware/soundDog/App/freertos.c`（新增 `RS485LoopTask` 任务 + 回环结果标志 + SpecTask 打印）

- `docs/features/FEAT-A5-01-USART3-RS485模块驱动.md`（回环结论、方案 B 接法、修订记录）

## 验证方式

- 根因①验证：烧录后回环任务能正常跑完，`RS485_Send st=0` + `RS485_Receive st=3`（超时正常返回），不再卡死；频谱任务（SPEC/BANDS）继续滚动。

- 根因②验证（待 CH340G 到货后）：双模块对端，PC 下发 → MAX3485#2 收端 SSCOM 观察到数据往返，`RS485_Receive st=0`。

**验证条件**（必填，R25）：

- 硬件/环境：本板 + 2× MAX3485（单 EN）+ ST-Link/OpenOCD；待独立 USB-TTL(CH340G) 到货做完整对端验证

- 验证的因果链：根因①=回环任务移入 RTOS 后 st=3 正常超时返回（时序坑消除）+ 频谱任务不死锁；根因②=确认模块单 EN → 自发自收不可能 → 方案 B

- 结论级别：根因①功能正常；根因②待双模块对端硬件齐备后做收发级验证

## 通用教训（提炼至 lessons\_summary）

1. FreeRTOS + STM32 HAL：**osKernelStart 前不得调用任何依赖 SysTick 的 HAL 阻塞函数（HAL\_Delay / HAL\_UART\_Receive 带超时等）**——BASEPRI=0x50 屏蔽 SysTick，超时永不触发（与 BUG-006 同根）。凡要在裸机期跑收发/延时的，先移入 RTOS 任务。
2. **半双工 RS485（DE+RE 同控单 EN）无法自发自收**：发送态禁收，A-B 短接回环必然失败。回环验证必须用双模块/对端，单模块 A-B 短接是无效测试。
3. 跨硬件照抄接线前必须核对模块使能引脚个数（单 EN vs DE/`/RE` 分开）——这决定回环测试方案是否成立（呼应 R28：电气参数/引脚须对本硬件核对）。

