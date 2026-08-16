# SoundDog — 工业异响音频检测节点

> *"Like a watchdog, but it listens."*

基于 **STM32F407** 的分布式工业音频异常检测节点：通过 **INMP441 数字麦克风 + I2S3 + DMA** 采集音频，后续用传统 DSP（FFT → MFCC → 阈值比对）实时判断设备运行声音是否异常，并通过 RS485 工业总线组网上报。

技术路线详见 [`PROJECT_PLAN.md`](PROJECT_PLAN.md)（Phase A 传统 DSP → Phase B 数据积累 → Phase C 可选 CMSIS-NN）。

---

## 当前状态

| 阶段 | 状态 | 说明 |
|------|------|------|
| A1 音频采集链路 | 🟡 **固件就绪，待换麦克风** | I2S3+DMA 采集已打通（16kHz/32-bit 帧），串口打印 `[n] max=xxx` 验证；**当前唯一阻塞：INMP441 麦克风本体损坏**（疑似上次 5V 超压事故所致，SD 高阻无输出） |
| A2 FFT 频谱 | ⬜ 未开始 | 待 A1 验证通过后开始 |

### 今天修掉的真实 Bug（均为真机实测发现）

| Bug | 等级 | 根因 | 修复 |
|-----|------|------|------|
| [BUG-20260816-001](docs/bugs/BUG-20260816-001-i2s-dma-size-overflow.md) | P0 | `HAL_I2S_Receive_DMA` 对 32-bit 格式把 Size 翻倍，而 DMA 为 32 位宽 → 搬运 8192B 超 4096B 缓冲，踩坏 `huart1` 和 FreeRTOS 全局区（任务创建时 HardFault） | Size 传 `I2S_HALF_BUF_SIZE`（512） |
| [BUG-20260816-002](docs/bugs/BUG-20260816-002-i2s-data-alignment.md) | P1 | I2S 帧与 INMP441 WS 槽差 16 SCK，音频在 32 位字**低 16 位**；原取数跨在错误位置恒为 0 | `I2S_DRV_ExtractPCM` 取 `(int16_t)(raw & 0xFFFF)` |

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

- **SCK 实际频率疑似为设计的 2 倍**（帧节奏 31 帧/250ms 指向 ~2.048MHz 而非 1.024MHz）——A2 FFT 前需用逻辑分析仪核实并修正时钟配置
- 麦克风本体损坏待更换（超压事故），换新后按 `HANDOFF.md` §4.2 接线即可
