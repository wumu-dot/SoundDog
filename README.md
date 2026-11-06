# SoundDog — 工业异响音频检测节点

> *"Like a watchdog, but it listens."*

基于 **STM32F407** 的分布式工业音频异常检测节点：通过 **INMP441 数字麦克风 + I2S3 + DMA** 采集音频，后续用传统 DSP（FFT → Mel → MFCC → 距离比对 → 防抖判决）实时判断设备运行声音是否异常，并通过 RS485 工业总线组网上报。

技术路线详见 [`PROJECT_PLAN.md`](PROJECT_PLAN.md)（Phase A 传统 DSP → Phase B 数据积累 → Phase C 可选 CMSIS-NN）。

---

## 当前状态

> **2026-10-16 修订说明**：上一版（10-16 13:05）依据的是网关侧 **10-12** 的实测，当时把断点判定在"网关模组 `RXD` → `DI` 段"。**该结论已于同日 16:48 被推翻**（只改 TTL 接线即通），因此 A5 行与"已知遗留"整体改写，并补入 `INC-20261016-001`（从站空片事故）。标 🟢 的 A1~A4 结论不受影响（它们验证于 08~09 月，测的是音频链路而非总线）。
>
> **2026-11-05 修订说明**：A4-04 已扩到 **9 个保持寄存器**（0~4 参数 / 5~6 温湿度 / 7~8 振动占位）；A7-06 评测基线落地——`EVAL` 10 Hz 旁挂（10 字段）+ 噪声底残差"提示级"级联已烧录验证；新增 `scripts/` 离线评测工具与 `data/` 证据日志。A7 行、目录结构、寄存器约束与"已知遗留"同步更新。

| 阶段 | 状态 | 说明 |
|------|------|------|
| A1 音频采集链路 | 🟢 **全线收官（2026-08-28）** | A1-01 ✅ / A1-02 ✅（安静 max 160~1606、吹气 3441~32767）/ A1-03 ✅（左声道有效）/ A1-04 ✅（**逻辑分析仪实测**：WS=16kHz 精确、SCK≈1MHz，Fs=16000Hz 无误） |
| A2 FFT 频谱 | 🟢 **完工** | A2-01 ✅ CMSIS-DSP 集成；A2-02 ✅ FFT 幅度谱；A2-03 ✅ OLED 柱状显示；A2-04 ✅ BANDS 串口行 |
| A3 MFCC 特征 | 🟢 **完工** | A3-01 ✅ MFCC 前置（分帧/预加重/汉明）；A3-02 ✅ Mel 滤波（512 rfft + 32 维）；A3-03 ✅ Log+DCT-II（13 维 MFCC） |
| A4 距离比对/判决 | 🟢 **完工** | A4-02 ✅ d_min 实时比对；A4-03 ✅ 连续帧防抖判决（报警/解除）；A4-04 ✅ Modbus RTU 从站 + 参数下发（**9 个保持寄存器**：0~4 参数 / 5~6 温湿度 / 7~8 振动占位） |
| A5 RS485 组网 | 🟢 **已闭环（2026-10-27）**；原「B→A 未稳定」已降为历史 | ✅ **10-27 结案：232 轮全对**（80.2 s/74 轮 + 清理后 19.3 s/20 轮 + 新设备 86.3 s/79 轮），`0x108`/超时/事务外丢帧均 0；10-29 扩 9 寄存器后又跑 **4,842 轮 / 83.4 min** 零错误。<br>以下为 2026-10-16 排查记录（保留备查）：<br>A5-01 ✅ USART3+RS485 硬件就绪；**PC 适配器作主站侧**根因 = 模块 TTL 侧 `TXD`/`RXD` 接反（应为 `TXD`→PD9、`RXD`→PD8），对调后收到合法应答 `01 03 02 02 58 B8 DE`（数据 600 = 阈值 6.0，CRC 独立复核通过）。<br>**2026-10-16 网关侧进展（此前"断点在网关模组 `RXD`→`DI` 段"的结论已被推翻）**：<br>① ⛔ **旧结论作废** —— 16:48 未换件、未焊接、未改固件，**只改 TTL 接线**即收到 `0x2E` × 151 / 15.057 s ⇒ 数据本来就能到 `DI`，模组无板内断路。<br>② ✅ **A→B（主站发→从站收）已通**：接线改正后从站侧连续多次采集全为合法帧（`01 03 00 0n 00 01 CRC`），乱码消失；SWD 直读 `rx_buff` 同形。<br>③ ~~🟡 **B→A（从站发→主站收）未稳定**：5 轮观测仅 1 轮 5/5 全对~~ —— **10-27 已证伪归因**：实际是网关侧事务层问题，修复后 **232 轮全对**。机理仍值得记：RTU 响应帧无事务号（esp-modbus issue #16）＋ 网关侧迟到帧被下一事务吃下。<br>⚠️ **待结**：`−6.35V` 待复测（`1.67V` 的"共地缺失"支已排除，但 `1.67V` 本身仍未解释） |
| A6 报警输出 | 🔴 **未开始** | FEAT-A6-01/02/03 规格已立；**无报警驱动逻辑**。现有仅 `main.c:214` 绿灯开机指示（跑到 main USER CODE 2）与 `freertos.c:452-462` 红灯临时代码（`eMBInit`/`eMBEnable` 成功）；引脚预留见 `pin_config.h:50-55`（`LED_ALARM` PB0 / `LED_STATUS` PB1 / `BUZZER` PE6）及 `:87-90`（TIM3 PWM 通道，**未启用**） |
| A7 辅助功能 | 🟡 **A7-01 / A7-02 已落地运行；A7-06 评测基线已出** | A7-01 SHT30 已落地（`BSP/sht30.c` + `sht30_task`：I2C3 PA8/PC9，2 s 刷新 `g_temp_x10`/`g_humi_x10`，经 Modbus 寄存器 5/6 只读上传）；启用时暴露堆缺口 → BUG-20261029-001（已扩堆 32768 修复）。A7-02 IWDG 已落地（`BSP/iwdg_drv.c` + `WDTPollTask`：200 ms 喂狗 / 1 s 超时），**已随主固件运行、无 IWDG 复位**（注意：SWD 烧录时需先冻结 IWDG，否则 `reset halt` 下约 1 s 被复位打断，见 §评测基线）。A7-03 Flash 未开始；A7-04 MPU6050 / A7-05 多源融合仍为计划书（未实施）。**A7-06 能力矩阵与对比基线已完成**（见下一节） |

### 评测基线（FEAT-A7-06，2026-11-05）

| 项 | 结果 | 证据/工具 |
|---|---|---|
| 安静 1 h | 固件报警 **1 次/h**；`d` μ 1.90 / σ 0.53（CV 0.28） | `data/eval_quiet_hour_2026-11-05.log` |
| 活动 1 h（走动/说话） | **9 次/h**；`d` σ 1.00（CV 0.40） | `data/eval_quiet_2026-11-05.log` |
| 离线阈值扫描 | 活动段 `th=8.0` → 0 次/h（10 Hz 回放，只作**相对**比较） | `scripts/eval_gate.py` |
| 两级级联（`d>6` + 残差>4） | 离线：活动 35→**1** 次/h、安静 1→1 | `scripts/eval_residual.py` |
| 未完成 | S4 故障工况（不平衡/松动/声学）⇒ DR/TD 无数据 | — |

旁挂输出（**只读，不参与判决**，9 个 Modbus 寄存器不受影响）：

```
EVAL,<tick_ms>,<d_x100>,<th_x100>,<rms_x100>,<pk_x100>,<bs>,<ba>,<lv>,<res_x100>
ALARM on src=consec|mofn n=<帧数> conf=<1|0> res=<×100>
```

- `conf=1` 残差确认（比值 > 4.0）｜`0` 疑似（多为环境声）；`res` 为噪声底残差比
- 体积：`text 182640 / bss 115880`（S2 前基线 `181732/115504` ⇒ **+908 B / +376 B**，2 KB 预算内）

> ⚠️ **烧录注意**：IWDG 超时约 1 s，`reset halt` 时固件尚未执行 `__HAL_DBGMCU_FREEZE_IWDG()`，直接 `program` 会被复位打断 → 先冻结：
>
> ```
> openocd -f openocd.cfg -c "init; reset halt; mww 0xE0042008 0x00001000; program soundDog/build/soundDog.elf verify; reset run; shutdown"
> ```

评测脚本用法（仅标准库，无需硬件）：

```
python scripts/eval_gate.py --logs data/eval_quiet_hour_2026-11-05.log --hourly --events
python scripts/eval_residual.py
```

### 近期真实 Bug（均为真机实测发现）

| Bug | 等级 | 根因 | 修复 |
|-----|------|------|------|
| [BUG-20260816-001](docs/bugs/BUG-20260816-001-i2s-dma-size-overflow.md) | P0 | `HAL_I2S_Receive_DMA` 对 32-bit 格式把 Size 翻倍，而 DMA 为 32 位宽 → 搬运 8192B 超 4096B 缓冲，踩坏 `huart1` 和 FreeRTOS 全局区（任务创建时 HardFault） | Size 传 `I2S_HALF_BUF_SIZE`（512） |
| [BUG-20260816-002](docs/bugs/BUG-20260816-002-i2s-data-alignment.md) | P1 | I2S 实际为 **4 字周期**（每 4 个 32-bit 字仅第 1 个含音频，其余为悬空/高阻字 0xFFFF）；v9 全帧提取混入伪迹，max 被钉死 ~32000 | v10：`extract_frame` 步进 4 取 `word[4k]` 低 16 位，128 真采样/帧；采样率核准 **16000Hz 整** |
| **INC-20261016-001**（Flash 空片，⚠️ 记录待归档到 `docs/bugs/`） | **P0** | **主 Flash 1 MB 被整体擦除（全 `0xFF`）** ⇒ 空向量表 ⇒ `PC=0xFFFFFFFE`、`MSP=0xFFFFFFD8`、`CFSR=0x1`(`IACCVIOL`)、`HFSR=0x40000000`(`FORCED`) ⇒ **LOCKUP**。SWD 实测：8 点采样（0K/64K/256K/512K/768K/末1K）× 2 会话**全 `0xFF`**；`FLASH_OPTCR=0x0FFFAAED`（`RDP=0xAA`=Level 0，**排除"读保护导致读出假 `0xFF`"**）；`RCC_CR=0x00007583`（仅 `HSION/HSIRDY`）、`FLASH_ACR=0`、`CPUID=0x410FC241` ⇒ **用户代码一行没跑**。**现象**：两灯灭、OLED 黑、按 RESET 无效、断电重上也无效 | `openocd -f openocd.cfg -c "program soundDog/build/soundDog.elf verify reset exit"` ⇒ 退出码 0、`Programming Finished`、**`Verified OK`**、`Resetting Target`。⚠️ 烧录路径正常 ⇒ **反证"硬件坏了"**。寄存器复核 **10 项全绿**：`[0x08000004]=08009efd`（与 elf 逐字一致）、`RCC_CR=0f037583`（HSE/PLL 全就绪）、`GPIOB_MODER=ffffabb5`、`GPIOB_ODR=00000003`（两灯亮）、`ICSR/CFSR/HFSR/DFSR=0`、`DHCSR=01010001`（内核运行中） |

> 🔴 **INC-20261016-001 的教训（比 bug 本身重要）**
> 1. **它是"零应答一个月"的一半原因**：从站自 `2026-09-28 22:55`（最后一次被证实"在跑"）之后到 `2026-10-16 20:4x` 一直空片 ⇒ 这期间**任何**主站轮询都必然拿不到应答。排查时若先确认从站是否活着，可省整月。
> 2. **空片与"OLED 偶发黑屏 / 两灯不亮 / 按 RESET 无效"同形**：这些都是**程序没跑**的下位表现，⛔ **不能直接归因到 OLED 模块或灯本身**（本次即曾一度误记为 `H11` OLED 故障）。
> 3. **为什么会空**：`G1` 烧录"擦成功写失败" / `G2` 解除读保护 `RDP L1→L0` 触发硬件强制整片擦除（本项目有 ISP 救砖前科）—— 两者都留 `RDP=0xAA`，**读数无法区分**，仍待回忆。
> 4. ~~**零器材验收法**：盯绿灯 `PB1` 是否**闪**（探针 `MB_PROBE_ENABLE=1` 下每发一帧应答翻转一次）~~ → ⛔ **该方法已失效（2026-10-27 探针物理删除，提交 `fb2d0c0`）**：`PB1` 现在**只在 `main.c:214` 开机点亮一次，此后不再翻转**，常亮≠在应答。**现行判据**：① 串口有 `DMIN`/`EVAL` 行持续输出（说明主循环在跑）；② 用主站或 PC 发一帧读寄存器，能收到合法应答；③ SWD 读 `s_present` 等水位变量。⛔ 别再用"灯闪不闪"判断从站死活。

另修复：I2S OVR 溢出停摆（SPI3 最小错误中断只清 OVR、强制关 RXNEIE、回调内稳健清除循环）。

---

## 硬件平台

- 主控：STM32F407ZGT6 核心板（LXBF407ZG-P1），HSE 8MHz → SYSCLK 168MHz
- 调试/烧录：ST-Link SWD（openocd）
- 麦克风：INMP441（I2S 数字输出，24-bit @ 32-bit 帧）
- 接线：I2S3_WS=PA4、I2S3_CK=PB3、I2S3_SD=PB5（SD 需 4.7kΩ 上拉至 3.3V），详见 [`HANDOFF.md`](HANDOFF.md) §4.2

## 软件环境

- RTOS：FreeRTOS CMSIS-RTOS_v2（6 个任务：defaultTask / specTask / displayTask / mbSlave / wdt / sht30Task）
- 驱动库：STM32 HAL（CubeMX 生成，`soundDog.ioc`）
- 构建：Makefile + mingw32-make + arm-none-eabi-gcc（`C:\ST\STM32CubeCLT_1.21.0\`）
- 通信：USART1 调试口（115200，单写者=specTask）；USART3 RS485（MAX3485，Modbus RTU 从站）
- Modbus：FreeModbus port（CMSIS-RTOS2），地址 0x01、115200-8N1、保持寄存器 0 基址
- 中断优先级铁律：NVIC Group 4，`configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY=5`，0~4 禁调 FreeRTOS FromISR API

## 快速开始

```bat
:: 只编译
firmware\build.bat

:: 编译 + SWD 烧录（ST-Link 接 SWDIO/SWCLK/GND）
firmware\build_and_flash.bat

:: ISP 串口救砖（BOOT0 + USART1）
firmware\flash_isp.bat
```

烧录后串口（115200）应看到（串口示例，省略 I2C/FFT 自检中间行 `[ ... ]`）：

```
SoundDog boot OK, SYSCLK=168000000
I2C scan: (end)
OLED init OK (selftest grid)
A2-02: spectrum path ready
FFT selftest: peak bin=... (…Hz) mag=…
I2S_DRV_Init ret=0 (0=HAL_OK)
I2S DMA started (first frame idx=…)     ← 三行契约第 3 行
[31] max=xxxx        ← 随声音变化 = A1 通过
```

> 三行契约（规格 §3.4）：`SoundDog boot OK` / `I2S_DRV_Init ret=0` / **`I2S DMA started`** —— 三者**按序前缀匹配**齐见即 A1~A2 链路通。⚠️ 该字串被约 11 份后续 FEAT 文档用作验收判据，**勿改字串**（曾因改字串立 BUG-20260829-004）。若 `ret!=0` 或打印 `OLED init FAIL`，先查 I2S/OLED 接线。
> ⚠️ `[31] max` 是**数据观察行**（`freertos.c:243`，帧号 + 该帧峰值），**不是**契约行。

## 目录结构

```
firmware/soundDog/      STM32 工程（CubeMX 生成 + 手写驱动）
  ├── App/              应用任务层
  │     main.c          入口/外设初始化/开机打印；_write 收口（USART1 单写者临界区）
  │     freertos.c      FreeRTOS 任务定义（spec/display/mbSlave/wdt）+ 取证打印
  │     modbus_app.c    应用层 Modbus 参数接口
  │     rtos_hooks.c    malloc 失败/栈溢出钩子（P1-2）
  │     audio_pipe.h    音频管道胶水：`frame_msg_t` 帧队列类型（ISR→任务）
  ├── BSP/              板级驱动（CubeMX Src/ 之外的 手写驱动）
  │     i2s_drv.c       手写 I2S3 DMA 双缓冲采集驱动
  │     iwdg_drv.c      IWDG 独立看门狗驱动（A7-02；喂狗走独立任务，禁 ISR 喂）
  │     rs485_drv.c     RS485 方向/收发封装。**Modbus 收发主路径走 `portserial.c`**；
  │                     本文件 `RS485_Init()` 仍在役（`main.c:275` 启动调用）；
  │                     `RS485_Send()` / `RS485_Receive()` 目前**无调用者**（原 `#if 0` 回环自测块
  │                     已于 2026-11-06 删除；原探针调用点 10-27 已删）⇒ 由 `--gc-sections` 丢弃
  │     spectrum.c/fft.c/mfcc.c/mel.c/mfcc_dct.c/model_normal.c/dist.c/debounce.c ...  DSP 链
  │     oled_drv.c       OLED 显示驱动
  ├── Inc/pin_config.h  引脚映射权威来源（改引脚只改这一个文件）
  ├── Src/              CubeMX 生成的 HAL 启动/外设初始化（stm32f4xx_*.c、i2s.c、dma.c…）
  ├── Middlewares/Third_Party/FreeModbus/port/cmsis_rtos2/
  │     portserial.c    Modbus 串口 port（RX 帧缓冲 + 收发状态机；回环探针已于 2026-10-27 物理删除）
  │     portevent.c     Modbus 事件 flag 分发（P2-10）
  │     portcritical.c  全局临界区（0 基址注意见记忆 R34）
  │     porttimer.c     Modbus RTU T3.5 定时器 port
  └── soundDog.ioc      CubeMX 工程文件
scripts/                PC 侧离线工具
  eval_baselines.py     E1/E2/E3 对比基线（FAR/TD，μ+kσ 扫描，--selftest 无硬件可跑）
  eval_gate.py          频带门控 + 分小时 FAR + 事件明细（T1/T2/T3）
  eval_residual.py      噪声底残差与两级级联验证（§14/§15）
  collect_*.py          串口采集（MFCC/dmin/频谱）
data/                  采集数据与评测证据（只入库两份 1 h 日志，其余本地保留）
  eval_quiet_hour_2026-11-05.log   安静基线 60 min
  eval_quiet_2026-11-05.log        活动工况 60 min
docs/                   文档体系（features/ 两级FEAT、bugs/、troubleshooting/、tools/…）
  features/FEAT-A7-06-能力矩阵与对比基线实验.md   能力矩阵、静/动基线、残差级联、三源核对
HANDOFF.md              交接文档（接线、踩坑记录、新板跑通清单）
CLAUDE.md               项目全局上下文（会话自动继承）
PROJECT_PLAN.md         总体规划（A1→A7 阶段）
准备工作.md              开发流程 + 调试方法论
```

## 文档导航

新读者建议按此顺序：`HANDOFF.md` → `CLAUDE.md` → `docs/INDEX.md`（场景检索地图）→ `PROJECT_PLAN.md` / `准备工作.md`。

## 已知遗留

- ~~SCK"疑似 2 倍"~~ → **已证伪关闭**（2026-08-28 双重证据：v10 口径修正 125 帧/s×128 采样=16000Hz + A1-04 逻辑分析仪实测 WS=16kHz 精确/SCK≈1MHz）
- ~~麦克风本体损坏待更换~~ → 已换新并验证通过（2026-08-28，吹气 max 冲满量程）
- ~~max 统计负峰溢出~~ → **已修**（`freertos.c:234-243` `frame_max_stat` 改 `int32_t` 累加 + 显式负峰处理）
- ~~🔴🔴 **A5 当前状态（2026-10-16 更新）**~~ → 🟢 **已闭环（2026-10-27）**：232 轮全对、`0x108`/超时/丢帧全 0。以下为历史记录：**A→B（主站发→从站收）已通**；~~**B→A（从站发→主站收）未稳定**~~（5 轮观测仅 1 轮全对）。⛔ **已作废**："断点在网关模组 `RXD` 焊盘 → 芯片 `DI` 段"——该结论被 16:48 的 TTL 单变量实测直接反证（只改接线就收到 `0x2E`×151），**模组无板内断路**。真因是三件事叠加：① 主站 `RS2` 与从站 `RS1` 是**不同厂家**模块，**TTL 丝印约定相反**（主站要直连、从站要交叉）；② 总线 `A`/`B` 反相；③ **从站自身空片**（见 `INC-20261016-001`）
- 🔴 **从站曾空片 18 天**（`2026-09-28 22:55` ~ `2026-10-16 20:4x`）：期间所有"主站零应答"的排查都建立在"从站活着"的错误前提上。**已烧回并 10 项寄存器复核全绿**，但**该事件尚未归档为 `docs/bugs/` 正式记录**（见上文 `INC-20261016-001`）
- ~~⚠️ **从站运行状态自 2026-10-16 21:1x 后未再复核**~~ → 🟢 **已复核（10-27 / 10-29）**：10-27 连续 232 轮 Modbus 应答正常；10-29 SWD 读到 `s_present=1`、温湿度随吹气变化（24.1→29.9 ℃ 后回落），9 寄存器整块读 4,842 轮全对
- ~~RS485 回环自测 st=3 失败~~ → **已归档为 [BUG-20260902-001](docs/bugs/BUG-20260902-001-rs485-loopback-single-en-timeout.md)**（单 EN 模块自发自收不可行 + BASEPRI 屏蔽致超时）；**回环代码已于 2026-11-06 删除**（原以 `#if 0` 封存，属"只禁用未删除"）；机理与 GDB 证据链全在 bug 文档内，`main.c` 只留一句"⛔ 不要搬回这里"的警告
- **RS485 两项电气定性**：`−6.35V`（未端接振铃/共模？）**待复测**；`1.67V` 的「共地缺失」支**已于 2026-09-28 实测排除**（共地链 20~30 mΩ ⇒ 优良），余「适配器正常中间电平」一支待裁
  - ⚠️ **别与另一个 `1.7V` 混淆**：网关侧 `2026-10-19` 也出现过 `1.7 V`，那是 ESP32 `J1.21` 的**悬空（D3 漏电）电位**，与本条 RS485 侧的 `1.67 V` **不是同一件事**
- [BUG-20260927-001](docs/bugs/BUG-20260927-001-oled-intermittent-blank-despite-init-ok.md) OLED **偶发上电黑/灰屏**（P2，未修）：I2C init 恒 OK、CPU 活着，已锁定模块供电/电荷泵物理层
  - ⚠️ **与空片的区别**：本条是"CPU 活着但屏黑"；`INC-20261016-001` 是"CPU 根本没跑"⇒ 两者都会表现为黑屏，⛔ **判因前必须先确认 `PB1` 是否在闪**
- ~~`portserial.c:230` 探针注释基准写错~~ → ✅ **已销项**：探针已于 **2026-10-27 物理删除**（提交 `fb2d0c0`），全仓 `grep -r PROBE` 为 **0**。⚠️ 若将来重加探针：注释里的 CRC 基准仍是错的（`01 03 00 00 00 01` 的 CRC 应 `84 0A`，原误写 `C4 0B`），且该基准帧是旧的**单寄存器**读，现在是整块读 `01 03 00 00 00 09`
  - ⚠️ 另注：网关侧已改**整块读**（帧 `01 03 00 00 00 05`），本注释的基准帧仍是旧的**单寄存器**读，比 CRC 更早就过时了
- 🟡 **A7-06 未完成项（2026-11-05）**：① S4 故障工况（不平衡/松动/声学）未采 ⇒ **DR/TD 无数据**；② 残差确认阈值 `τ=4.0` 为暂定值（固件 62.5 Hz 更新底噪、离线用 4.8 Hz `BANDS`，需用含 `res` 的新日志重标）；③ 频带/残差门控目前**只做提示**（`conf`），未参与报警
- ⚠️ **评测口径**：活动段离线回放（10 Hz）比固件 `ALARM` 多计约 4 倍（35 vs 9），**FAR 结论以固件 `ALARM` 行为准**，离线扫描只用于阈值/门控的相对比较

---

## 配套仓库

本节点是**从站**，必须有一个 Modbus RTU 主站轮询它才能组网上云。主站侧在同账号的另一个仓库：

| 仓库 | 角色 |
|---|---|
| [`esp32-modbus-gateway`](https://github.com/wumu-dot/esp32-modbus-gateway) | **网关（主站）**：ESP32-S3-N16R8，Modbus RTU 主站（UART2，`RXD=GPIO16` / `TXD=GPIO17` / `RTS=GPIO10` 驱动 DE+~RE，115200-8N1 半双工），轮询本节点的 **9 个**保持寄存器并按 **ThingsCloud 物模型**上报；同时接受云端下发的参数写命令。✅ **当前状态（10-29）**：9 寄存器整块读 **4,842 轮零错误**、云端 10 属性全值、83.4 min 长跑无重启 |

**两侧的分工边界**：

| 侧 | 负责 |
|---|---|
| **本仓库（从站）** | 声学采集（INMP441 + I2S3 + DMA）→ FFT → MFCC → 距离比对 → 防抖判决；把 **9 个**保持寄存器暴露给总线；报警输出（A6，未实现） |
| **`esp32-modbus-gateway`（主站）** | 轮询寄存器 → 串口打印 + MQTT JSON 上行；MQTT 下行写命令 → ack；本地 SPI 屏（FEAT-03，未实施） |

**两侧的耦合点（改动必须同步）**：

| 项 | 约束 |
|---|---|
| **寄存器个数** | 主站 `MASTER_PARAM_CNT` = **9** ⇔ 从站 `MODBUS_REG_HOLDING_NUM` = **9**，**必须一致**（从站已扩到 9；改网关前先核对对端仓库，避免只轮询前 5 个） |
| **寄存器语义** | `0=TH / 1=ALARM_CNT / 2=RELEASE_CNT / 3=WIN_LEN(固定100拒写) / 4=WIN_ANOM`；`5=温度×10 / 6=湿度×10`（SHT30 只读）；`7~8=振动占位`（A7-04 未实施）。范围宏唯一事实源是本仓库 `BSP/param.h` |
| **地址 / 波特率** | 从站 `0x01`、115200-8N1，两侧一致 |
| **TTL 丝印约定** | 🔴 **2026-10-25 变更：两侧统一模块，约定已一致**。原状态为「两侧不同厂家、约定相反」（`RS1` 交叉 / `RS2` 直连）；现从站 `RS1` 也换成 `RS2` 同款 ⇒ **两侧统一为直连**：`TX→TXD`、`RX→RXD`。⚠️ 换模块后**必须先核对丝印再接线**，⛔ 不可沿用旧的「RS1 交叉」结论。详见 `docs/hardware/硬件配置-2026-09-16.md §2`、R42 |

> 📌 **状态口径**：README 中音频链路与评测基线以 **2026-11-05** 的实测为准；总线侧以 **2026-10-27 结案 + 10-29 九寄存器实测**为准（原 10-16 记录仅作历史）。网关侧的对应状态以该仓库 README 与 `docs/INDEX.md` 指向的台账为准。两侧描述若有冲突，先查各自仓库的最新提交，再回原始测量记录。
