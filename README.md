# SoundDog — 工业异响音频检测节点

> *"Like a watchdog, but it listens."*

基于 **STM32F407** 的分布式工业音频异常检测节点：通过 **INMP441 数字麦克风 + I2S3 + DMA** 采集音频，后续用传统 DSP（FFT → MFCC → 阈值比对）实时判断设备运行声音是否异常，并通过 RS485 工业总线组网上报。

技术路线详见 [`PROJECT_PLAN.md`](PROJECT_PLAN.md)（Phase A 传统 DSP → Phase B 数据积累 → Phase C 可选 CMSIS-NN）。

---

## 当前状态

| 阶段 | 状态 | 说明 |
|------|------|------|
| A1 音频采集链路 | 🟢 **全线收官（2026-08-28）** | A1-01 ✅ / A1-02 ✅（安静 max 160~1606、吹气 3441~32767）/ A1-03 ✅（左声道有效）/ A1-04 ✅（**逻辑分析仪实测**：WS=16kHz 精确、SCK≈1MHz，Fs=16000Hz 无误） |
| A2 FFT 频谱 | ⬜ 未开始 | A2-01 CMSIS-DSP 集成，A2-02 FFT 幅度谱（下一步） |

### 今天修掉的真实 Bug（均为真机实测发现）

| Bug | 等级 | 根因 | 修复 |
|-----|------|------|------|
| [BUG-20260816-001](docs/bugs/BUG-20260816-001-i2s-dma-size-overflow.md) | P0 | `HAL_I2S_Receive_DMA` 对 32-bit 格式把 Size 翻倍，而 DMA 为 32 位宽 → 搬运 8192B 超 4096B 缓冲，踩坏 `huart1` 和 FreeRTOS 全局区（任务创建时 HardFault） | Size 传 `I2S_HALF_BUF_SIZE`（512） |
| [BUG-20260816-002](docs/bugs/BUG-20260816-002-i2s-data-alignment.md) | P1 | I2S 实际为 **4 字周期**（每 4 个 32-bit 字仅第 1 个含音频，其余为悬空/高阻字 0xFFFF）；v9 全帧提取混入伪迹，max 被钉死 ~32000 | v10：`extract_frame` 步进 4 取 `word[4k]` 低 16 位，128 真采样/帧；采样率核准 **16000Hz 整**（详见 [BUG-002 详解](docs/summary/BUG-002-I2S数据提取问题详解.md)） |

另修复：I2S OVR 溢出停摆（SPI3 最小错误中断只清 OVR、强制关 RXNEIE、回调内稳健清除循环）。

---

## 硬件平台

- 主控：STM32F407ZGT6 核心板（LXBF407ZG-P1），HSE 8MHz → SYSCLK 168MHz
- 调试/烧录：ST-Link SWD（openocd）
- 麦克风：INMP441（I2S 数字输出，24-bit @ 32-bit 帧）
- 接线：I2S3_WS=PA4、I2S3_CK=PB3、I2S3_SD=PB5（SD 需 4.7kΩ 上拉至 3.3V），详见 [`HANDOFF.md`](HANDOFF.md) §4.2

## 软件环境

- RTOS：FreeRTOS CMSIS-RTOS_v2（当前 1 个占位任务 defaultTask）
- 驱动库：STM32 HAL（CubeMX 生成，`soundDog.ioc`）
- 构建：Makefile + mingw32-make + arm-none-eabi-gcc（`C:\ST\STM32CubeCLT_1.21.0\`）
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

烧录后串口（115200）应看到：

```
SoundDog boot OK, SYSCLK=168000000
I2S_DRV_Init ret=0 (0=HAL_OK)
I2S DMA started! waiting data...
[31] max=xxxx        ← 随声音变化 = A1 通过
```

## 目录结构

```
firmware/soundDog/      STM32 工程（CubeMX 生成 + 手写驱动）
  ├── Src/i2s_drv.c     手写 I2S3 DMA 双缓冲采集驱动
  ├── Inc/pin_config.h  引脚映射权威来源（改引脚只改这一个文件）
  └── soundDog.ioc      CubeMX 工程文件
docs/                   文档体系（features/ 两级FEAT、bugs/、troubleshooting/、tools/…）
HANDOFF.md              交接文档（接线、踩坑记录、新板跑通清单）
CLAUDE.md               项目全局上下文（会话自动继承）
PROJECT_PLAN.md         总体规划（A1→A5 阶段）
准备工作.md              开发流程 + 调试方法论
```

## 文档导航

新读者建议按此顺序：`HANDOFF.md` → `CLAUDE.md` → `docs/INDEX.md`（场景检索地图）→ `PROJECT_PLAN.md` / `准备工作.md`。

## 已知遗留

- ~~SCK"疑似 2 倍"~~ → **已证伪关闭**（2026-08-28 双重证据：v10 口径修正 125 帧/s×128 采样=16000Hz + A1-04 逻辑分析仪实测 WS=16kHz 精确/SCK≈1MHz）
- ~~麦克风本体损坏待更换~~ → 已换新并验证通过（2026-08-28，吹气 max 冲满量程）
- RS485 回环自测 st=3 失败（归 A5-01 排查，代码已 `#if 0` 封存）
- max 统计负峰溢出（`abs(INT16_MIN)` → -32768 打印瑕疵）：A2 开工前改 int32 累加
