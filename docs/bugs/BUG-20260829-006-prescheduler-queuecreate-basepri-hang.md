# 调度器启动前调用 xQueueCreate 残留 BASEPRI=0x50 → SysTick 全灭 → HAL_Delay 死循环（OLED 黑屏真根因）

- **Bug ID**：BUG-20260829-006
- **严重等级**：P0-致命（整机挂死：串口无声 + OLED 黑屏，从 A2-03 加入 OLED 起必现）
- **发现日期**：2026-08-29
- **修复日期**：2026-08-29（初始化顺序重排；GDB + 上板验证通过）
- **关联 FEAT**：FEAT-A2-03（OLED 频谱显示）

## 现象描述
烧录后串口输出到 `I2C scan: 0x3C (end)` 为止**永久无声**（无 `OLED init OK/FAIL`、无 `I2S DMA started`、无 SPEC 行），OLED 全黑。手动复位后每次 boot 复现。屏幕根本没收到任何初始化命令（挂死发生在 init 第一句延时，0xAE 都没发出去）。

## 复现步骤
1. 当前固件烧录，观察串口：boot 五行后停在 `I2C scan: 0x3C (end)`
2. 板子不复位则无任何后续输出（用户体感"不复位就没数据"）
3. GDB 挂接复现调用栈（见根因）

## 根因分析（GDB 实测取证，逐级证据链）
调用栈：
```
#0 HAL_GetTick()  #1 HAL_Delay(Delay=100)
#2 OLED_DRV_Init() at oled_drv.c:71   ← init 第一句 HAL_Delay(100) 永不返回
#3 main() at main.c:232
```
寄存器实测：`basepri=0x50`（= configMAX_SYSCALL_INTERRUPT_PRIORITY，优先级 5）、`uwTick` 冻结。
断点二分（main.c 各行停点读 basepri/uwTick）：
```
@ spec_init 后、xQueueCreate 前:  basepri=0x0   uwTick=11（SysTick 活着）
@ xQueueCreate 后:               basepri=0x50  uwTick=13（从此冻结）
```
**因果链**：`xQueueCreate → pvPortMalloc(heap_4.c) → vTaskSuspendAll/xTaskResumeAll → taskENTER/EXIT_CRITICAL`。FreeRTOS CM4F port 在调度器启动前 `uxCriticalNesting = 0xaaaaaaaa`（哨兵值，port.c:146）；`vPortExitCritical` 只有嵌套数减到 0 才恢复中断（port.c:420-427）——哨兵值永远减不到 0，**BASEPRI 抬到 0x50 后不清零**。此后优先级 ≥5 的全部中断（含 SysTick=15）被屏蔽 → `uwTick` 不增 → `HAL_Delay(100)` 死循环。调度器启动（xPortStartScheduler port.c:370 将嵌套归 0）后首个任务临界区会把 BASEPRI 清回 0——**损害窗口 = 第一次内核 API 调用 ~ osKernelStart 之间**。

A2-02 存活纯属侥幸：队列创建与调度器启动之间没有依赖 HAL_GetTick 的代码。A2-03 在该窗口加了 `OLED_DRV_Init`（首句 HAL_Delay），正好踩进窗口。
（附带更正：此前 FEAT-A2-03 阶段4（四轮）把黑屏归因为"照抄 4ilo 电气参数不适配"——**该结论被本轮 GDB 证据推翻**：init 从未执行到参数设置那一步。）

## 修复方案
**调整 main.c 初始化顺序：所有依赖 HAL tick / 中断的初始化（MX_I2C1_Init + I2C 扫描 + OLED_DRV_Init 全屏自检）移到 xQueueCreate 之前执行**；`xQueueCreate → I2S_DRV_Init(DMA) → osKernelStart` 收尾。窗口内不再有 tick 依赖调用（I2S_DRV_Init 在 A2-02 期间已实证可在脏 BASEPRI 下正常返回 ret=0）。
备选（不采用）：删 HAL_Delay——治标，且后续任何预调度器期延时需求会再踩雷。

## 影响文件
- `firmware/soundDog/App/main.c`（初始化块重排；改动影响地图已输出，执行待用户确认 R9）

## 验证方式
修复后烧录（证据已取得）：
1. GDB 挂接运行中的板子：`prvIdleTask`（调度器正常运行，非死循环）；`err_count=0`（OLED 全部 I2C 传输零错误）；`uwTick=29197`（SysTick 持续计数，非冻结）
2. 用户上板目视确认：OLED 点亮显示（黑屏消除）→ 全屏白 2s 诊断期 → 自检网格/柱状图画面
3. text=157180 编译 0 error，烧录 157288 字节

**验证条件**（必填，R25）：
- 硬件/环境：本板 + 新麦克风 + OLED 0x3C + ST-Link/OpenOCD/GDB
- 验证的因果链：GDB 三重运行证据（调度器在跑/I2C 零错/tick 活着）+ 用户目视屏幕点亮；破坏性因果链此前已闭环（挂死调用栈 HAL_Delay←OLED_DRV_Init←main + basepri=0x50 + 断点二分定位 xQueueCreate）
- 结论级别：功能正常（挂死消除 + 屏幕恢复显示；柱状图跟随声音的交互级验证属 FEAT-A2-03 AC-01 验收范畴）

## 通用教训（提炼至 lessons_summary）
1. FreeRTOS CM4F：**osKernelStart 之前调用任何会进临界区/堆分配的内核 API（xQueueCreate/xTaskCreate/信号量等），会把 BASEPRI 抬起且不落下**（uxCriticalNesting 哨兵值 0xaaaaaaaa 机制）。预调度器期不得再调用依赖 SysTick 的 HAL 函数（HAL_Delay/带超时的轮询），或把这类初始化挪到首个内核调用之前。
2. "串口无声"≠死机也≠外设坏：先用 GDB 挂接看调用栈 + basepri/primask + uwTick，5 分钟定位 vs 盲目改参数数小时（本轮前四轮排查全部押错层——字符串、参数、接线、电源，实际是调度器前内核调用时序）。
3. 跨硬件照抄开源代码要分层核对：协议/流程可抄，电气参数须对本硬件（详见 R28 规则，本案为反例佐证）。
