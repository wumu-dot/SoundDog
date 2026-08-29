# ISR 内 printf 与任务 printf 并发——串口输出交错 + HAL_UART_Transmit 不可重入风险

- **Bug ID**：BUG-20260829-003
- **严重等级**：P2-一般（输出可读性 + huart1 状态被踩的隐患）
- **发现日期**：2026-08-29
- **修复日期**：2026-08-29
- **关联 FEAT**：FEAT-A2-02（FFT 幅度谱）

## 现象描述
串口输出出现两段日志互相穿插（一行没打完接着另一行），且偶发 HAL_BUSY 导致丢行。典型表现：max 统计行嵌进 SPEC 行中间。

## 复现步骤
1. audio_frame_cb（DMA ISR 上下文）内保留 printf（首帧提示/max 统计）
2. specTask 同时周期打印 SPEC 行
3. 运行期观察 USART1 输出交错

## 根因分析
`_write()` 底层是 `HAL_UART_Transmit()`（阻塞式、**不可重入**）。ISR 与任务并发调用 → gState 状态机被踩（HAL_BUSY）+ 输出字节流交错。ISR 上下文本就禁止阻塞/非重入调用。

## 修复方案
ISR 回调只保留"拷贝 + 入队"（约 10µs）；全部打印和 max 统计移到消费侧 specTask（运行期唯一 printf 写入者）。回调纯净化后 ISR 用时 <10µs，符合四层架构约定。

## 影响文件
- `firmware/soundDog/App/main.c`（audio_frame_cb 删 printf/max 统计）
- `firmware/soundDog/App/freertos.c`（specTask 增加打印与统计）

## 验证方式
烧录后串口观察：SPEC/max 行不再交错，boot 三行完整无穿插，吹气 max 数值跟随变化。

**验证条件**（必填，R25）：
- 硬件/环境：新麦克风 + 1kHz 音源
- 验证的因果链：输入扰动（吹气/1kHz）→ max/SPEC 输出跟随 + 长时间运行无交错行
- 结论级别：功能正常（A2-02 阶段 4 验收时确认）

## 通用教训（提炼至 lessons_summary）
1. ISR 上下文禁止 printf（阻塞 + HAL_UART_Transmit 不可重入双重违规）；打印一律移到任务侧消费点。
2. "运行期唯一打印者"原则：全固件同一时刻只允许一个上下文写 USART1。
