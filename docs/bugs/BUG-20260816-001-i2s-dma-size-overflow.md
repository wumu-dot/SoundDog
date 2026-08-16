# I2S3 DMA 长度翻倍导致缓冲区溢出，踩坏 huart1 和 FreeRTOS 全局区

- **Bug ID**：BUG-20260816-001
- **严重等级**：P0-致命（导致串口打印丢失 + RTOS 调度器 HardFault，音频链路完全不可用）
- **发现日期**：2026-08-16
- **修复日期**：2026-08-16

## 现象描述

换新板供电正常后（5V 适配器），boot 打印完整但：
1. `I2S DMA started! waiting data...`（DMA 半满回调打印）永远不出现
2. `osKernelStart()` 后 RTOS 无任何任务运行，红灯/任务探针全无输出
3. OpenOCD halt 后 PC 停在 `vListInsertEnd`（list.c:97），BFAR=0xE000EDF8（DCRDR 调试寄存器），CFSR=BFARVALID——典型的"链表指针被踩成垃圾值后写非法地址"总线错误
4. 故障点在 `xTaskCreate → prvAddNewTaskToReadyList → vListInsertEnd`，即第一个任务创建时就崩

## 复现步骤

1. 用 `build_and_flash.bat` 烧录 A1 固件（I2S3 + DMA 双缓冲采集）
2. 串口 115200 观察：boot 两行正常，之后无任何输出
3. OpenOCD `halt` 读 PC/CFSR/BFAR 复现故障

## 根因分析

DMA 数据宽度与 HAL 长度语义不匹配，导致 DMA 实际搬运量 = 缓冲区 2 倍：

- 工程 DMA 配置为 **32 位宽**（`DMA_PDATAALIGN_WORD`/`DMA_MDATAALIGN_WORD`，CubeMX 生成，见 soundDog.ioc），NDTR 按 **32 位字**计数，每个 I2S 32-bit 帧触发 1 次 32 位搬运（实测 32kHz 字速率，与 SCK 1.024MHz/32 吻合）
- 但 `HAL_I2S_Receive_DMA` 对 24/32-bit 格式会把 Size 翻倍：`RxXferSize = Size<<1`（HAL 心智模型是"按 16 位半字计数"）
- 驱动传 `Size=I2S_FULL_BUF_SIZE=1024` → `NDTR = 2048` **个字** = **8192 字节**，而 `dma_buf` 只有 4096 字节 → **溢出 4096 字节**
- 内存布局：`dma_buf`(0x2000054c) 之后紧邻 `huart1`(0x2000154c)、`pxReadyTasksLists`(0x200016bc)、`pxCurrentTCB`、`cmsis_os2` 状态、`ucHeap`——DMA 从第 1025 个字（启动后 ~32ms，恰为第一次半满中断时刻）开始持续踩踏整个 FreeRTOS 全局区
- 后果：huart1 被踩 → printf 静默失败；就绪链表被踩 → 任务创建时 `vListInsertEnd` 写垃圾指针 → 总线错误 → HardFault → RTOS 永远起不来

## 修复方案

`I2S_DRV_Init` 调用 HAL 时传 `I2S_HALF_BUF_SIZE`（512）而非 `I2S_FULL_BUF_SIZE`（1024）：
`NDTR = 2×512 = 1024` 字 = 4096 字节 = `dma_buf` 正好；半满中断在 512 采样处触发，与驱动双缓冲（半区 512 采样）设计吻合。

## 影响文件

- `firmware/soundDog/Src/i2s_drv.c`（I2S_DRV_Init 的 HAL_I2S_Receive_DMA Size 参数）

## 验证方式

1. 修复后烧录，串口应看到 `I2S DMA started! waiting data...` 及 `[n] max=xxx` 随声音变化
2. RTOS 调度器正常（defaultTask 运行）
3. OpenOCD 检查不再出现 HardFault

## 遗留提醒

- 若以后在 CubeMX 里把 DMA 数据宽度改成 **Half Word**，则 Size 应恢复传 `I2S_FULL_BUF_SIZE`（HAL 翻倍后半字计数与 32 位帧 = 2 半字吻合）。**DMA 宽度与 Size 必须配套，改一个就要改另一个**
