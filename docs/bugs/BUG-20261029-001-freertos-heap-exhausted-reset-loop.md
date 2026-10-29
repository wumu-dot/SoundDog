# 启用 sht30 任务后启动即复位循环 —— FreeRTOS 堆池耗尽（缺口仅 600 B）

- **Bug ID**：BUG-20261029-001
- **严重等级**：P0-致命（整机不可用：上电后约每 3 s 复位一次，OLED 无限重播开机动画，Modbus 从站无法应答）
- **发现日期**：2026-10-29
- **修复日期**：2026-10-29（`configTOTAL_HEAP_SIZE` 15360 → 24576 → 32768，SWD + 串口双验证）
- **关联**：FEAT-A7-01（SHT30 温湿度）；后续 FEAT（MPU6050）预留

---

## 现象描述

`App/freertos.c` 里把 `sht30TaskHandle = osThreadNew(sht30_task, ...)` 取消注释（此前 10-25 因"复位循环"被临时禁用），重新编译烧录后：

- **OLED 反复播放开机自检画面**（自检网格 → 消失 → 再来一遍），周期约 3 s；
- **红灯 `PB0` 不亮**（PB0 = `eMBInit` + `eMBEnable` 成功标志）；
- 串口只吐到一半就断，重复出现同一小段。

关键日志片段（`boot_after_sht30_on.log`，25 秒窗口内 **8 次**启动横幅）：

```
I2S_DRV_Init ret=0 (0=HAL_OK)
FREER
[FATAL] FreeRTOS heap allocation FAILED
SoundDog boot OK, SYSCLK=168000000
I2C scan: 0x3C (end)
OLED init OK (selftest grid)
...
```

## 复现步骤

1. `App/freertos.c` 取消 `sht30_task` 任务创建那一行的注释；
2. `make -j4` → `EXIT=0`（`text 181732`，比禁用版多 2540 B，即 `sht30_task` 从被 `--gc-sections` 回收变为实际链接）；
3. SWD 烧录 → `Download verified successfully`；
4. 上电观察：OLED 约每 3 s 重播开机动画，红灯不亮；
5. 串口抓 25 s → 8 次启动横幅 + 反复 `[FATAL] FreeRTOS heap allocation FAILED`。

## 根因分析（证据链）

### 1. 直接死因：堆分配失败 → 钩子关中断死循环 → IWDG 复位

```c
/* App/rtos_hooks.c:38 */
void vApplicationMallocFailedHook(void)
{
    __disable_irq();                                   // PRIMASK=1，SysTick 不再投递
    dbg_print("\r\n[FATAL] FreeRTOS heap allocation FAILED\r\n");
    for (;;) { }                                       // 永不返回
}
```

`__disable_irq()` 之后 SysTick 中断进不来 ⇒ `xTickCount` 不再递增 ⇒ 调度器冻结；同时 `wdt` 任务也停 ⇒ IWDG（预分频 64 / 重装 500，LSI 32 kHz 下约 **1.0 s**，实际 0.68~1.9 s）到点复位。

启动耗时约 2 s + 看门狗约 1 s ≈ **3 s 一圈**，与观测周期吻合。

> ⚠️ 这也是"OLED 反复播开机动画"的真正机理：OLED 初始化与自检画面在 `main()` 里完成（不需要 RTOS），调度器一冻，`DisplayTask` 永远刷不出正常界面，屏幕就定格在开机那一帧；复位后又重画一遍。

### 2. 为什么分配会失败：池子只有 15360 B，实际需要 15960 B

SWD 直读 `heap_4` 的两个全局变量（**实测，非估算**）：

```
xFreeBytesRemaining            = xMinimumEverFreeBytesRemaining = 0x21A8 = 8616 B   （修正后 24576 配置下）
⇒ 实际占用 = 24576 − 8616 = 15960 B
原配置 15360 B ⇒ 缺口 600 B
```

占用构成：

| 项 | 字节 |
|---|---:|
| `display` 任务栈 | 4,096 |
| `spec` 任务栈 | 2,048 |
| `sht30` 任务栈（本次新增） | 2,048 |
| `mbSlave` 任务栈 | 1,024 |
| `wdt` 任务栈 | 1,024 |
| **`mbRxRecv` 任务栈** | **1,024** |
| `defaultTask` 栈 | 512 |
| **栈小计** | **11,776（73.8%）** |
| 其他（7×TCB + 音频队列 4×260=1,040 + 2 事件组 + 1 定时器 + heap_4 块头与对齐） | 4,184 |
| **合计** | **15,960** |

### 3. 真实失败点：不是任务创建，是运行期 `eMBInit`

一开始误判为"`sht30` 是最后一个创建的任务，所以它创建失败"。该推断**错误**，三条反证：

1. `SoundDog boot OK` / `I2C scan` / `OLED init OK` 都正常打出 ⇒ `MX_FREERTOS_Init` 里 6 个任务**全部创建成功**、调度器已启动；
2. 故障日志中 `FreeModbus slave enabled on USART3` 出现 **0 次**（正常固件里有）⇒ `eMBInit` 从未走到成功；
3. 红灯 `PB0`（`eMBInit`+`eMBEnable` 成功标志）不亮。

⇒ 失败点是 `MBSlavePollTask` 运行期调用 `eMBInit`，其中 `portserial.c:77` 的 `osThreadNew(handleReceivedDataTask, ...)` 需要 **1,024 B 栈**（+8 B 块头 = 1,032），而当时只剩约 432 B。

### 4. 为什么禁用 sht30 就正常

`sht30` 的 2,048 B 栈 + TCB ≈ 2,156 B 是**最后加进去的那一笔**，恰好吃掉余额。它不是"元凶"，是"最后一根稻草"。

## 修复方案

`Inc/FreeRTOSConfig.h` 的 `configTOTAL_HEAP_SIZE`：

| 阶段 | 值 | 池内空闲 | 说明 |
|---|---:|---:|---|
| 原始 | 15,360 | −600（溢出） | 缺口 600 B |
| **第一次修复** | **24,576** | 8,616（35%） | 解除复位循环 |
| **第二次扩容** | **32,768** | **16,808（51%）** | 为 MPU6050 任务预留（约 2 KB 栈 + TCB） |

RAM 占用核算：`.data 256 + .bss 62,252 = 62,508`，`. _user_heap_stack` 预留 53,252，合计 **115,760 / 131,072**，余 15,312 B（同时是 MSP 的额外生长空间）。

⛔ **不要动 `_Min_Stack_Size`（4096）** —— 任务栈之外，所有 ISR 仍跑在这块 MSP 上。

## 影响文件

- `firmware/soundDog/Inc/FreeRTOSConfig.h`（`configTOTAL_HEAP_SIZE` + 大段堆账注释）
- `firmware/soundDog/App/freertos.c`（`sht30TaskHandle = osThreadNew(...)` 取消注释）

## 验证方式

**编译**：`make -j4` → `EXIT=0`，`text 181732 / data 256 / bss 115504`。

**烧录**：SWD `Download verified successfully`，hex SHA-256 `b536164c…`。

**运行（SWD 直读）**：

```
xFreeBytesRemaining           = 0x41A8 = 16808 B
xMinimumEverFreeBytesRemaining = 0x41A8 = 16808 B
```

两条相等 ⇒ 启动后从未释放；且 16,808 与"32768 − 15960"完全吻合，说明占用测算无遗漏。

**串口**：烧录后连续抓取 12 s → 8,419 字节，`SoundDog boot OK` 出现 **0 次**（无复位）、`FATAL` 出现 **0 次**。

**验证条件**（必填，R25）：
- **硬件/环境**：本板（STM32F407ZGT6）+ ST-Link（FW V2J37S7）+ STM32CubeProgrammer v1.21.0 CLI；串口 `COM4`、115200、`DTR=False`/`RTS=False`（不复位）；电源保持原状未改动。
- **验证的因果链**：池容量 → `pvPortMalloc` 成功 → 7 个任务与 `eMBInit` 全部建成 → 调度器正常运行 → 无 IWDG 复位。破坏性因果链此前已闭环（`[FATAL]` 反复出现 + `slave enabled` 零次 + `PB0` 不亮）。
- **结论级别**：**"复位循环消除 + 池余量符合预期"**。⚠️ **不等于**"SHT30 功能正常" —— 见下方遗留项。

## 遗留项（未验证，不得据此宣布功能完成）

1. **SHT30 是否真的在线未知**：`BSP/sht30.c` 无任何日志，`sht30_task` 内 `s_present` 静默；启动的 `I2C scan`（`App/main.c:231`）只扫 **I2C1**，而 SHT30 在 **I2C3 0x44**，扫不到。若传感器未接好，寄存器 5/6 恒为 `500 / 0`，云端会显示 **0.0 ℃ / 0% RH** 的假数据。
2. **长时间稳定性未验证**：本轮最长窗口 12 s / 60 s，不足以判断长期表现。

## 通用教训

1. **`[FATAL] FreeRTOS heap allocation FAILED` 是现成打印，先看串口再猜硬件。** 本 Bug 的前身（2026-10-25）把它误判为"`HAL_I2C_Init` 栈深 / I2C3 时钟未就绪"，导致 sht30 被禁用四天，真正原因一直是堆。
2. **算堆账必须把 `eMBInit` 里的任务算进去。** `mbRxRecv`（1,024 B）不在 `MX_FREERTOS_Init` 里，而在 `portserial.c:77` 创建，极易漏算。
3. **`vApplicationMallocFailedHook` 的触发条件是"找不到够大的空闲块"，不是"堆被写坏"。** 本 Bug 中堆完全健康，只是池子小；`xFreeBytesRemaining` 与 `xMinimumEverFree` 始终相等即为佐证。
4. **钩子里的 `__disable_irq()` + IWDG 会组合出"复位循环"。** 好处是故障可见（OLED 重播、串口有 `[FATAL]`），坏处是现象看起来像"屏幕坏了"。看到"开机动画无限循环"先查串口，别查显示。
5. **改链接脚本后 `make` 不会重链。** `Makefile:219` 的 `.elf` 依赖里没有 `$(LDSCRIPT)`，改了 `.ld` 必须 `make -B` 或先删 `build/soundDog.{elf,hex,bin,map}`，否则拿到的是旧产物，"验证通过"是假的。
6. **`_Min_Heap_Size` 不是 malloc 的天花板**（`STM32F407XX_FLASH.ld:58`，0xC000）：它是**无内容段**，只做链接期预算检查；`_sbrk` 的上界是 `_estack - _Min_Stack_Size`。它的真实语义是"**给 newlib 保底多少**"。当前池子 32,768，仍可在不改 `.ld` 的前提下加到约 48,080。

---

*记录：审查官 ｜ 2026-10-29 22:0x*
