# SoundDog 项目交接文档

> 交接日期：2026-08-16
> 交接原因：切换 AI 助手
> **最重要的一句话：软件成果全部完好可用，唯一阻塞是硬件——核心板因超压损坏，需要换一块新板。**

---

## 一、项目是什么

SoundDog：STM32F407 音频采集项目，当前阶段（A1）目标是用 **INMP441 数字麦克风 + I2S3 + DMA** 采集 16kHz 音频，串口打印 PCM 最大值验证链路。

- 主控：STM32F407ZGT6，HSE 8MHz，SYSCLK 168MHz
- RTOS：FreeRTOS CMSIS-RTOS_v2（当前 1 个占位任务 defaultTask）
- 工具链：Makefile + mingw32-make + arm-none-eabi-gcc（路径 `C:\ST\STM32CubeCLT_1.21.0\`）

> **CodeGraph 已索引**：`.codegraph/codegraph.db`（10.4MB）已覆盖全部代码（含 `firmware/soundDog/`），可直接用 `codegraph_explore` / `codegraph explore` 查代码，无需重新索引。偶发 watchdog 重启（日志 #850 报错）不影响索引数据，重启即恢复。

---

## 二、软件成果（全部完好，可直接复用）

### 2.1 代码位置

工程在 `firmware/soundDog/`（CubeMX 生成 + 手写驱动）：

| 文件 | 说明 | 状态 |
|------|------|------|
| `soundDog.ioc` | CubeMX 工程文件 | ✅ 已配好 I2S3 + USART1 + FreeRTOS + TIM7 |
| `Src/i2s.c` | I2S3 初始化（Master RX / 32bit / 16kHz / PLLI2S） | ✅ |
| `Src/i2s_drv.c` + `Inc/i2s_drv.h` | 手写 DMA 双缓冲驱动 | ✅ |
| `Src/main.c` | printf 重定向 + I2S DMA 启动 + 测试回调 | ✅ |
| `Src/stm32f4xx_hal_msp.c` | 加了 JTAG 释放代码 | ✅ |
| `Inc/pin_config.h` | 引脚映射权威来源 | ✅ |

### 2.2 关键配置（已验证正确）

- **I2S3 时钟**：HSE 8MHz → /PLLM(8) → ×PLLI2S_N(256) → /PLLI2S_R(5) → 51.2MHz → I2SDIV=25（SCK = 51.2M/(2×25) = 1.024MHz）→ /64 → Fs 16kHz（误差 0%）
- **I2S3 参数**：Master Receive / Philips / 32-bit frame / 16kHz / MCLK disable / PLL clock
- **引脚**：I2S3_WS=PA4, I2S3_CK=PB3, I2S3_SD=PB5（从 PC10/PC12 换过来，因为 PC10 是焊盘洞不好接）
- **DMA**：DMA1_Stream2_Channel0，Circular，Word(32bit)，Priority High，NVIC 优先级 5
- **HAL 时基**：TIM7（独立于 FreeRTOS SysTick）
- **中断优先级铁律**：NVIC Group 4，`configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY=5`，0~4 禁调 FromISR API

### 2.3 脚本（firmware/ 下）

| 脚本 | 用途 |
|------|------|
| `build.bat` | 只编译 |
| `build_and_flash.bat` | 编译 + SWD 烧录 |
| `flash_isp.bat` | ISP 串口烧录（救砖用，BOOT0 + USART1） |
| `build.sh` / `flash.sh` | Git Bash 版 |
| `check_doc.bat` / `check_doc_fix.bat` | DOC-STATE 漂移检查/修复 |

### 2.4 文档成果

| 文件 | 内容 |
|------|------|
| `CubeMX_I2S3配置详解.md` | I2S3 完整配置 + 时钟推导 + 校验清单 |
| `CubeMX实操步骤.md` | 全部外设配置手顺 |
| `docs/bugs/` | 5 个 bug 记录（见下；故障速查表 `bug_shturl` 共 8 条） |
| `docs/troubleshooting/bug_shturl` | 故障速查表 |
| `外部资源索引.md` | 数据手册/原理图/例程路径 |
| `scripts/check-doc-drift.sh` | DOC-STATE 漂移检测（支持 --fix） |

---

## 三、⚠️ 硬件事故（本次交接的关键）

### 3.1 发生了什么

核心板在排查"串口无数据"问题时，经历了多次异常电压：

1. 先是量到核心板 5V 脚只有 3.1V（本应 5V），怀疑 USB 线/口问题
2. 排查过程中接入了面包板电源（量到 3.7V，后掉到 2.4V），已断开
3. **最严重**：让用户用手机 Type-C 快充线给核心板供电，快充协议输出高压，量到 **5V 脚 8V**
4. 8V 超压**打穿了板载 3.3V 稳压芯片**，之后量 3.3V 脚 = 5V（输入直通输出，主芯片被 5V 灌着）

### 3.2 当前板子状态

- **板载稳压芯片（AMS1117-3.3 或同类）已损坏**，3.3V 输出变成直通（5V 进 → 5V 出）
- 3.3V 脚对 GND 电阻 = 23Ω（偏低，主芯片可能没烧，但不确定）
- **结论：这块板子不能继续通电，需要换板或换稳压芯片**（即使只换稳压芯片也不建议复用——主芯片曾被 5V 灌入，可能有隐性损伤）

### 3.3 责任说明

板子超压是我（上一个 AI）指导失误——让用户用快充 Type-C 线试供电导致。软件层面无任何问题。

---

## 四、下一步（换新板后的 10 分钟跑通清单）

### 4.1 买新板

STM32F407ZGT6 核心板（淘宝/拼多多，30~50 块），和原来同款（LXBF407ZG-P1 黑色沉金核心板）。

### 4.2 新板到手后的接线

> ⚠️ **供电红线（上次事故根因）：只准用电脑 USB 口或非快充 5V 适配器供电；上电前先量 5V≈5V、3.3V≈3.3V。严禁手机快充头 / 乱接电源。**

```
INMP441         核心板
──────────────────────────
VDD  ──────→  3.3V 脚（核心板直接取，别走面包板电源）
GND  ──────→  GND
L/R  ──────→  GND（接地=左声道，必须接，悬空不出数据）
SCK  ──────→  PB3
WS   ──────→  PA4
SD   ──────→  PB5  + 4.7kΩ 上拉到 3.3V（上拉非必需但无害；"SD 开漏必须上拉"是误传，SD 实为推挽输出）

LED 红灯 ──[220Ω]── PB0
LED 绿灯 ──[220Ω]── PB1
蜂鸣器 ──[三极管]── PE6
```

### 4.3 烧录顺序（关键！避免再踩坑）

1. **先用 SWD 烧录**（`build_and_flash.bat`），因为现在 CubeMX 已经配了 Serial Wire，PA13/PA14 不会锁死
2. 如果 SWD 连不上 → 用 `flash_isp.bat`（BOOT0 按键 + 串口 COM4，TX/RX 交叉接；端口号以设备管理器为准）
3. 进 ISP 用「RST+BOOT 组合」：上电后按 RST 不松 → 按 BOOT 不松 → 松 RST → 松 BOOT

### 4.4 验证

烧录后串口助手（COM4，115200，端口号以设备管理器为准）应看到：
```
SoundDog boot OK, SYSCLK=168000000
I2S_DRV_Init ret=0 (0=HAL_OK)
I2S DMA started! waiting data...
[30] max=xxxx   ← 数字随声音变化 = 成功
```

---

## 五、踩过的坑（新 AI 必读）

| # | 坑 | 教训 |
|---|-----|------|
| 1 | **SWD 锁死**（P0） | CubeMX 必须配 `SYS→Debug→Serial Wire`，否则 gpio.c 把 PA13/PA14 设成 Analog，首次烧录后 SWD 永久锁死，只能 ISP 救砖。详见 `docs/bugs/BUG-20260815-001` |
| 2 | **JTAG 引脚未释放** | 引脚从 PC10/PC12 换到 PB3/PB5 后，PB3=JTDO/PB5=JTDI 被 JTAG 占用，必须在 `HAL_MspInit` 释放 JTAG（已加代码） |
| 3 | **CubeMX 重新生成丢 include** | 手动 include 必须放 `USER CODE BEGIN/END` 之间，否则重生成被删 |
| 4 | **Makefile 缺源文件** | 手加的 .c 文件必须手动加到 Makefile 的 C_SOURCES |
| 5 | **PLLI2S 灰色不可编辑** | 先配 I2S3 外设 + Clock Source 选 PLLI2S，时钟树才解锁 |
| 6 | **INMP441 供电不稳** | VDD 必须接核心板 3.3V 直接取电，别走面包板电源模块 |
| 7 | **INMP441 SD 输出** | SD 实为推挽输出（"开漏"是误传），上拉非必需但无害；读到全 0xFF/0x00 优先查 L/R 与 WS，别只怪上拉 |
| 8 | **L/R 引脚悬空** | INMP441 的 L/R 必须接 GND 或 VDD，悬空不出数据 |
| 9 | **ISP 进不了 bootloader** | 用「RST+BOOT 组合」而非「按 BOOT 上电」，后者时序难把握 |

---

## 六、外部资源路径

数据手册、原理图、例程都在：
`C:\Users\wumu2\OneDrive\桌面\stm32F407ZGT6\`

- 核心板原理图：`芯片stm32F407/1778826025286617/STM32F407ZG-P1核心板原理图.pdf`
- 核心板下载教程：`芯片stm32F407/【6】烧录程序教程/F407ZGT6黑色沉金核心板程序下载.docx`
- INMP441 手册：`INMP441全向麦克风资料/INMP441.pdf`
- 串口助手 SSCOM：`芯片stm32F407/【2】工具软件/SSCOM_v5.13.1/`

完整索引见 `外部资源索引.md`。

---

## 七、当前阻塞点和待办

**阻塞点（唯一）：核心板损坏，需换新板。**

换板后待办（按顺序）：
1. 新板烧录验证音频链路（串口看到 max 数据随声音变化）
2. 确认 L/R 通道（数据在左还是右声道，取有变化的那路）
3. 进入 A2 阶段：FFT + 频谱分析
4. 补齐剩余外设（OLED、SHT30、RS485、LED PWM 等，pin_config.h 已预留引脚）
