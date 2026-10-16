# SoundDog — 工业异响音频检测节点

> *"Like a watchdog, but it listens."*

基于 **STM32F407** 的分布式工业音频异常检测节点：通过 **INMP441 数字麦克风 + I2S3 + DMA** 采集音频，后续用传统 DSP（FFT → Mel → MFCC → 距离比对 → 防抖判决）实时判断设备运行声音是否异常，并通过 RS485 工业总线组网上报。

技术路线详见 [`PROJECT_PLAN.md`](PROJECT_PLAN.md)（Phase A 传统 DSP → Phase B 数据积累 → Phase C 可选 CMSIS-NN）。

---

## 当前状态

| 阶段 | 状态 | 说明 |
|------|------|------|
| A1 音频采集链路 | 🟢 **全线收官（2026-08-28）** | A1-01 ✅ / A1-02 ✅（安静 max 160~1606、吹气 3441~32767）/ A1-03 ✅（左声道有效）/ A1-04 ✅（**逻辑分析仪实测**：WS=16kHz 精确、SCK≈1MHz，Fs=16000Hz 无误） |
| A2 FFT 频谱 | 🟢 **完工** | A2-01 ✅ CMSIS-DSP 集成；A2-02 ✅ FFT 幅度谱；A2-03 ✅ OLED 柱状显示；A2-04 ✅ BANDS 串口行 |
| A3 MFCC 特征 | 🟢 **完工** | A3-01 ✅ MFCC 前置（分帧/预加重/汉明）；A3-02 ✅ Mel 滤波（512 rfft + 32 维）；A3-03 ✅ Log+DCT-II（13 维 MFCC） |
| A4 距离比对/判决 | 🟢 **完工** | A4-02 ✅ d_min 实时比对；A4-03 ✅ 连续帧防抖判决（报警/解除）；A4-04 ✅ Modbus RTU 从站+参数下发 |
| A5 RS485 组网 | 🟢 **链路已打通（2026-09-21）** | A5-01 ✅ USART3+RS485 硬件就绪；**根因 = 模块 TTL 侧 `TXD`/`RXD` 接反**（应为 `TXD`→PD9、`RXD`→PD8），对调后主站收到合法应答 `01 03 02 02 58 B8 DE`（数据 600 = 阈值 6.0，CRC 独立复核通过）。⚠️ 待结：`−6.35V` / `1.67V` 两项电气定性、换 ESP32 主站后回环复测 |
| A6 报警输出 | 🔴 **未开始** | FEAT-A6-01/02/03 规格已立；固件中仅有引脚预留（`pin_config.h:54-55` BUZZER），无红灯/绿灯/蜂鸣器驱动代码 |
| A7 辅助功能 | 🟡 **A7-02 代码落地（未实测）** | A7-02 IWDG 看门狗已落地（`iwdg_drv.c` + `WDTPollTask`：200ms 喂狗 / 1s 超时）且编译通过，但**未烧录实测、FEAT 文档仍标"🔴未开始"待归档**；A7-01 SHT30 / A7-03 Flash 未开始 |

### 近期真实 Bug（均为真机实测发现）

| Bug | 等级 | 根因 | 修复 |
|-----|------|------|------|
| [BUG-20260816-001](docs/bugs/BUG-20260816-001-i2s-dma-size-overflow.md) | P0 | `HAL_I2S_Receive_DMA` 对 32-bit 格式把 Size 翻倍，而 DMA 为 32 位宽 → 搬运 8192B 超 4096B 缓冲，踩坏 `huart1` 和 FreeRTOS 全局区（任务创建时 HardFault） | Size 传 `I2S_HALF_BUF_SIZE`（512） |
| [BUG-20260816-002](docs/bugs/BUG-20260816-002-i2s-data-alignment.md) | P1 | I2S 实际为 **4 字周期**（每 4 个 32-bit 字仅第 1 个含音频，其余为悬空/高阻字 0xFFFF）；v9 全帧提取混入伪迹，max 被钉死 ~32000 | v10：`extract_frame` 步进 4 取 `word[4k]` 低 16 位，128 真采样/帧；采样率核准 **16000Hz 整** |

另修复：I2S OVR 溢出停摆（SPI3 最小错误中断只清 OVR、强制关 RXNEIE、回调内稳健清除循环）。

---

## 硬件平台

- 主控：STM32F407ZGT6 核心板（LXBF407ZG-P1），HSE 8MHz → SYSCLK 168MHz
- 调试/烧录：ST-Link SWD（openocd）
- 麦克风：INMP441（I2S 数字输出，24-bit @ 32-bit 帧）
- 接线：I2S3_WS=PA4、I2S3_CK=PB3、I2S3_SD=PB5（SD 需 4.7kΩ 上拉至 3.3V），详见 [`HANDOFF.md`](HANDOFF.md) §4.2

## 软件环境

- RTOS：FreeRTOS CMSIS-RTOS_v2（5 个任务：defaultTask / specTask / displayTask / mbSlave / wdt）
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
[31] max=xxxx        ← 随声音变化 = A1 通过
```

> 三行契约：`SoundDog boot OK` / `I2S_DRV_Init ret=0` / `[31] max` 三者齐见即 A1~A2 链路通。若 `ret!=0` 或调 `OLED init FAIL`，先查 I2S/OLED 接线。

## 目录结构

```
firmware/soundDog/      STM32 工程（CubeMX 生成 + 手写驱动）
  ├── App/              应用任务层
  │     main.c          入口/外设初始化/开机打印；_write 收口（USART1 单写者临界区）
  │     freertos.c      FreeRTOS 任务定义（spec/display/mbSlave）+ 取证打印
  │     modbus_app.c    应用层 Modbus 参数接口
  │     rtos_hooks.c    malloc 失败/栈溢出钩子（P1-2）
  ├── BSP/              板级驱动（CubeMX Src/ 之外的 手写驱动）
  │     i2s_drv.c       手写 I2S3 DMA 双缓冲采集驱动
  │     iwdg_drv.c      IWDG 独立看门狗驱动（A7-02；喂狗走独立任务，禁 ISR 喂）
  │     rs485_drv.c     ⚠️ **死代码**：仅被 `main.c` 的 `#if 0` 回环段引用，
  │                     在役 RS485 收发路径 = Middlewares 的 `portserial.c`
  │     spectrum.c/fft.c/mfcc.c/mel.c/mfcc_dct.c/model_normal.c/dist.c/debounce.c ...  DSP 链
  │     oled_drv.c       OLED 显示驱动
  ├── Inc/pin_config.h  引脚映射权威来源（改引脚只改这一个文件）
  ├── Src/              CubeMX 生成的 HAL 启动/外设初始化（stm32f4xx_*.c、i2s.c、dma.c…）
  ├── Middlewares/Third_Party/FreeModbus/port/cmsis_rtos2/
  │     portserial.c    Modbus 串口 port（RX 帧缓冲/回环探针，受 MB_PROBE_ENABLE 开关）
  │     portevent.c     Modbus 事件 flag 分发（P2-10）
  │     portcritical.c  全局临界区（0 基址注意见记忆 R34）
  └── soundDog.ioc      CubeMX 工程文件
docs/                   文档体系（features/ 两级FEAT、bugs/、troubleshooting/、tools/…）
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
- ~~max 统计负峰溢出~~ → **已修**（`freertos.c:206-217` `frame_max_stat` 改 `int32_t` 累加 + 显式负峰处理）
- ~~RS485 回环自测 st=3 失败~~ → **已解释**：根因是模块 TTL 侧 `TXD`/`RXD` 接反（见 A5 行），非回环逻辑缺陷；回环测试代码仍以 `#if 0` 封存
- **RS485 两项电气定性未结**（2026-09-21 链路打通**亦不销项**）：`−6.35V`（未端接振铃/共模？）与 `1.67V`（漂浮=共地缺失？vs 适配器正常中间电平？）——接 ESP32 长线前须补测
- [BUG-20260927-001](docs/bugs/BUG-20260927-001-oled-intermittent-blank-despite-init-ok.md) OLED **偶发上电黑/灰屏**（P2，未修）：I2C init 恒 OK、CPU 活着，已锁定模块供电/电荷泵物理层
- `portserial.c:230` 探针注释基准写错（`01 03 00 00 00 01` 的 CRC 应 `84 0A`，误写 `C4 0B`）：当前 `MB_PROBE_ENABLE=1` 探针开着，重开探针前必须改
