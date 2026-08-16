# CubeMX I2S3 配置详解 — SoundDog 第一步

> 目标：在 STM32CubeMX 中完成 I2S3 + DMA 的全部配置，使 STM32F407 能正确接收 INMP441 的 I2S PCM 音频数据。

---

## 0. 前置知识：INMP441 和 STM32 I2S 如何配合

### 0.1 INMP441 输出的是什么？

根据 **INMP441 官方数据手册第 1 页框图**：

```
INMP441 内部结构（来自 InvenSense 数据手册）：
┌──────────────────────────────────────────────────────────┐
│ MEMS 麦克风元件 → ΔΣ ADC → 数字抽取滤波器 → I2S 接口      │
│                                                          │
│ 关键规格（数据手册 §TABLE 1）：                            │
│  • 信噪比 SNR: 61 dBA                                    │
│  • 灵敏度: -26 dBFS @ 1kHz, 94dB SPL                     │
│  • 频率响应: 60 Hz ~ 15 kHz                              │
│  • 供电: 1.8V ~ 3.3V                                    │
│  • 功耗: ~1.4 mA (正常模式)                               │
│  • I2S 输出: 标准 Philips I2S, 24-bit, SCK max 4 MHz     │
└──────────────────────────────────────────────────────────┘
```

INMP441 **内部已经完成 PDM→PCM 转换**（数据手册框图明确包含 "Decimation Filter"），STM32 侧只需标准 I2S 接收。**不需要写软件 CIC 滤波器。**

### 0.2 关键约束：SCK 频率决定采样率

根据 INMP441 数据手册，内部抽取比固定为 64：

```
采样率 Fs = SCK / 64

目标 Fs = 16 kHz → SCK 必须是 1.024 MHz ✅
目标 Fs = 8 kHz  → SCK 必须是 0.512 MHz ✅
```

INMP441 的 SCK 最高支持 **4.0 MHz**（对应 Fs=62.5kHz），所以我们用 1.024 MHz 完全没问题。

### 0.3 STM32 I2S 帧格式选择 — ⚠️ ESP32 已验证：必须用 32-bit 模式！

**来自 ESP32 + INMP441 的成功案例（你的 `测试资料` 目录）**：

> *"I could only get it to work with 32 bit sampling."* — ESP32 INMP441 开源项目作者

ESP32 三份示例代码（InputSerialPlotter、NoiseLevel、VUMeterDemo）**全部使用 `I2S_BITS_PER_SAMPLE_32BIT`**。

| CubeMX 选项 | 每通道位数 | 一帧总位数 | SCK 频率 | 适合 INMP441? |
|------------|-----------|-----------|---------|--------------|
| 16 Bits | 16 | 32 | Fs × 32 | ❌ SCK 只有 512kHz，INMP441 内部抽取比不对 |
| 24 Bits | 24 | 48 | Fs × 48 | ❌ SCK = 768kHz，不匹配 |
| 24 Bits Data on 32 Bits Frame | 24 (对齐到 32) | 64 | **Fs × 64 = 1.024 MHz** | ⚠️ SCK 频率对，但 STM32 HAL 的 24-bit 模式可能截断数据 |
| **32 Bits Data on 32 Bits Frame** | 32 | 64 | **Fs × 64 = 1.024 MHz** | ✅ ESP32 已验证可行！推荐 |

**结论：为了保险（和 ESP32 的成功经验一致），选择 "32 Bits Data on 32 Bits Frame"。**

SCK = Fs × 64 = 1.024 MHz，INMP441 内部抽取正确运行在 16kHz。DMA 收 32-bit 完整帧，然后在软件中提取有效数据。

### 0.4 数据对齐方式 — 来自 INMP441 数据手册 §I2S INTERFACE

INMP441 输出 **24-bit 补码数据**，在 I2S Philips 标准下，MSB 对齐到 WS 边沿后的第 2 个 SCK：

```
INMP441 每通道 I2S 时序（数据手册 FIGURE 6）：
SCK:  _─_─_─_─_─_─_─_─_ ... _─_─_─_─_  (64 个时钟 = L+R 一帧)
WS:   ‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾\____________/  (低=右声道, 高=左声道)
                         ‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾
SD:   zz MSB ........................ LSB zz zz  (24-bit 有效 + 8-bit 空)
      ← 左声道 32-bit 槽 →← 右声道 32-bit 槽 →

关键点：
  • 24-bit 数据在 32-bit 槽内的 [31:8] 位置（MSB 对齐）
  • 低 8 位 [7:0] 是 0 填充
  • 数据是二进制补码（two's complement），范围 -2^23 ~ +2^23-1
```

#### 从 ESP32 VUMeter 示例学到的数据提取方法

```c
// ESP32 VUMeterDemo 的实际代码（已运行验证通过）：
mean += (samples[i] >> 14);   // 右移 14 位后用于 VU 电平计算

// InputSerialPlotter 直接打印 int32_t 原始值：
Serial.println(sample);       // 打印原始 32-bit 值，观察波形
```

> **`>> 14` 的含义分析**：`>> 8` 去掉低 8 位 0 填充得到 24-bit PCM，`>> 14` 再额外去掉 6 位（总共丢弃低 14 位），保留高 18 位有效值用于幅度计算。这是 VU 表的做法——不需要全部 24-bit 精度。

#### STM32 上的数据提取（参考 ESP32 验证经验）

```c
// 方案 A：提取 24-bit → 16-bit PCM（适合后续 FFT/MFCC 处理）
int32_t pcm_24bit = (int32_t)(dma_buffer[i] >> 8);   // 24-bit 有符号
int16_t pcm_16bit = (int16_t)(pcm_24bit >> 8);        // 取高 16 位

// 方案 B：直接取 32-bit 高位（如果只想粗略看波形）
int32_t raw = (int32_t)dma_buffer[i];
int16_t pcm = (int16_t)(raw >> 16);                   // 取高 16 位
```

> ⚠️ **最终数据提取方案需要上板实测验证**。ESP32 的经验表明 `bits_per_sample=32BIT` 能收到正确数据，但不同 MCU 的 I2S 外设对数据的字节序处理可能不同。建议第一版先用 `printf("%08lX", raw)` 打印原始 16 进制值，确认实际数据分布后再确定移位方案。

---

## 1. 创建 CubeMX 项目

### 1.1 新建 Project

```
File → New Project → 搜索 "STM32F407ZGT6"
→ 选中 LQFP144 封装 → Start Project
```

### 1.2 初始 Pinout 视图

刚创建完的 Pinout 视图，很多引脚是默认功能。先不要急，按下面顺序来。

---

## 2. RCC 时钟源配置

### 2.1 使能外部晶振

进入 **Pinout & Configuration → System Core → RCC**：

| 参数 | 值 | 说明 |
|------|-----|------|
| High Speed Clock (HSE) | **Crystal/Ceramic Resonator** | 使用外部 8MHz 晶振（核心板自带） |
| Low Speed Clock (LSE) | Disable | 不需要 RTC，不接 32.768kHz |

> F407 核心板一般板载 8MHz HSE + 32.768kHz LSE。但 LSE 我们不需要，可以不使能省电。

---

## 3. Clock Configuration（时钟树）— 最关键的部分

### 3.1 整体策略

```
HSE (8MHz)
  │
  ├─→ /PLLM ──→ ×PLLN ──→ /PLLP → SYSCLK = 168 MHz（系统主频）
  │                        /PLLQ → 48 MHz（USB/SDIO 等）
  │
  └─→ /PLLM ──→ ×PLLI2S_N ──→ /PLLI2S_R → I2SxCLK → I2S3 时钟源
```

> ⚠️ **PLLM 是共享的**，主 PLL 和 PLLI2S 用同一个 PLLM 分频。

### 3.2 时钟树页面操作

打开 **Clock Configuration** 选项卡，按顺序输入：

#### Step 1: 系统时钟（168 MHz，F407 最大主频）

| 参数 | 输入值 | 说明 |
|------|--------|------|
| HSE | **8** MHz | 外部晶振频率，不可改 |
| PLL Source Mux | **HSE** | 选 HSE 做 PLL 源 |
| **PLLM** | **/8** | HSE/8=1MHz 进 VCO |
| **PLLN** | **×336** | VCO=1MHz×336=336MHz |
| **PLLP** | **/2** | SYSCLK=336/2=168MHz ✅ |
| PLLQ | /7 | 336/7=48MHz（给 USB） |
| **SYSCLK** | **168 MHz** | ✅ 目标达成 |
| AHB Prescaler | /1 | HCLK=168MHz |
| APB1 Prescaler | **/4** | PCLK1=42MHz（F407 APB1 最大 42MHz） |
| APB2 Prescaler | /2 | PCLK2=84MHz |

#### Step 2: I2S 时钟 — 需要手动计算

**核心目标：让 PLLI2S 输出一个能量化成 1.024MHz SCK 的频率。**

公式：
```
SCK = I2SxCLK / (2 × I2SDIV)      （F4 I2S 硬件分频：SCK = I2SxCLK / [(2×I2SDIV)+ODD]，此处 ODD=0）
Fs  = SCK / 64 = I2SxCLK / (2 × I2SDIV × 64) = 16 kHz   （Philips 32bit：一帧 64 SCK = 左右各 32）

要让 SCK = 1.024 MHz：
I2SDIV = I2SxCLK / (2 × 1,024,000) = I2SxCLK / 2,048,000 （需要是整数，且 ODD=0 时为偶数）
```

**推荐配置：**

| 参数 | 输入值 | 计算结果 |
|------|--------|---------|
| I2S Clock Source | **PLLI2S_R** | 使用专用 I2S PLL |
| **PLLI2S_N** | **×256** | VCO = 1MHz × 256 = 256 MHz |
| **PLLI2S_R** | **/5** | I2SxCLK = 256 / 5 = **51.2 MHz** |

验证：
```
I2SDIV = 51,200,000 / 2,048,000 = 25（整数！）
SCK = 51,200,000 / (2 × 25) = 1,024,000 Hz = 1.024 MHz ✅
Fs  = 1,024,000 / 64 = 16,000 Hz ✅（精确 16kHz）
```

**备选配置（如果不喜欢这个）：**

| PLLI2S_N | PLLI2S_R | I2SxCLK | I2SDIV | SCK |
|----------|----------|---------|--------|-----|
| 256 | 5 | 51.2 MHz | 25 | 1.024 MHz ✅ |
| 200 | 5 | 40.0 MHz | - | 非整数 ❌ |
| 192 | 4 | 48.0 MHz | - | 非整数 ❌ |
| 200 | 4 | 50.0 MHz | - | 非整数 ❌ |
| 320 | 5 | 64.0 MHz | - | 非整数 ❌ |
| 256 | 5 | 51.2 MHz | 25 | 1.024 MHz ← 就这个 |

> **CubeMX 会帮你自动计算 I2SDIV**，配置完 I2S3 后切回时钟树页面可以验证 SCK 是否准确。

### 3.3 时钟树配置完成后的关键值总览

```
SYSCLK:    168 MHz
HCLK:      168 MHz
PCLK1:      42 MHz   (APB1, 定时器除外)
PCLK2:      84 MHz   (APB2, 定时器除外)
PLLI2S_R:   51.2 MHz (给 I2S3)
```

### 3.4 I2S3 参数校验清单

> 配完后对照此表逐项确认，确保一步到位不出错。

#### Mode 面板

| 参数 | 正确值 | 说明 |
|------|--------|------|
| Mode | **Half-Duplex Master** | 主机半双工，INMP441 只有单向 SD |
| Master Clock Output | **☐ 不勾选** | INMP441 三线制 (SCK/WS/SD)，不需要 MCK |

#### Parameter Settings

| 参数 | 正确值 | 说明 |
|------|--------|------|
| Transmission Mode | **Master Receive** | ← 关键！麦克风是接收，不是发送 |
| Communication Standard | **I2S Philips** | INMP441 数据手册明确标注 |
| Data and Frame Format | **32 Bits Data on 32 Bits Frame** | ESP32 社区验证 |
| Selected Audio Frequency | **16 KHz** | 目标采样率 |
| Real Audio Frequency | **16.0 KHz** | 确认显示 |
| Error | **0.0 %** | 必须 0% |

#### Clock Parameters

| 参数 | 正确值 | 说明 |
|------|--------|------|
| Clock Source | **I2S PLL Clock** | 使用 PLLI2S 专用时钟 |
| Clock Polarity | **Low** | 标准 I2S 极性 |

#### Clock Configuration 验证

| 节点 | 值 | 计算结果 |
|------|-----|---------|
| PLLI2S_N | **×256** | VCO = 1MHz × 256 = 256 MHz |
| PLLI2S_R | **/5** | I2SxCLK = 256 / 5 = **51.2 MHz** |
| I2SDIV (自动) | **25** | 51,200,000 / 2,048,000 = 25 |
| SCK | **1.024 MHz** | 51.2 MHz / (2×25) = 1.024 MHz |
| Fs | **16,000 Hz** | 1.024 MHz / 64 = 16 kHz |

> 链路：`HSE 8MHz → /PLLM(8) → ×PLLI2S_N(256) → /PLLI2S_R(5) → I2SxCLK 51.2MHz → I2SDIV=25 → SCK 1.024MHz（=51.2M/(2×25)）→ /64 → Fs 16kHz`

---

## 4. I2S3 外设配置

### 4.1 Pinout 引脚映射

在 **Pinout view** 标签页，找到以下引脚并配置：

| 信号 | 芯片引脚 | 搜索方式 | CubeMX 功能 |
|------|---------|---------|-------------|
| I2S3_CK | PC10 | 搜 `I2S3_CK` | I2S3 Clock |
| I2S3_WS | PA4 | 搜 `I2S3_WS` | I2S3 Word Select |
| I2S3_SD | PC12 | 搜 `I2S3_SD` | I2S3 Serial Data |

操作：Pinout 视图左侧搜索框输入 `I2S3`，把这三个信号分别拖到芯片上。

> ⚠️ **确认一下**：这三个引脚默认可能是 SPI3 功能。I2S 和 SPI 在 F4 上是共享外设，选择 `I2S3` 功能时会自动覆盖。如果 CubeMX 提示冲突，选 I2S3 优先。

### 4.2 I2S3 Mode 和参数

进入 **Pinout & Configuration → Connectivity → I2S3**：

#### Mode 面板

| 参数 | 值 | 说明 |
|------|-----|------|
| Mode | **Master Receive** | STM32 产生 SCK/WS，INMP441 串行输出数据 |
| I2S Standard | **Philips Standard** | INMP441 数据手册明确标注此格式 |
| Data Format | **32 Bits Data on 32 Bits Frame** | ⭐ ESP32 已验证！24-bit 模式可能不兼容 |
| Audio Frequency | **16 kHz** | 目标采样率，适合语音频带分析 |
| Clock Source | **PLLI2S** | 已在时钟树配置 |
| Clock Polarity (CKP) | **Low** | 标准 I2S 时钟极性 |
| Master Clock Output | **Disabled** | INMP441 只需要 SCK/WS/SD 三线，不需要 MCLK |

> ⚠️ **为什么不用 "24 Bits Data on 32 Bits Frame"**：虽然 SCK 频率相同（都是 1.024 MHz），但 ESP32 社区的经验显示，INMP441 只能稳定工作在 32-bit 数据宽度模式。STM32 的 24-bit 模式可能在 HAL 层做意外的数据截断或对齐。**直接用 32-bit 模式，软件提取数据，最安全。**

> ⚠️ 如果 CubeMX 的 I2S3 没有 "32 Bits" 选项（某些版本只显示 16/24/32），确认选的是 **"32 Bits Data on 32 Bits Frame"**。

### 4.3 INMP441 硬件接线 + L/R 通道选择

#### 引脚接线对照

```
STM32F407 核心板         INMP441 模块
┌──────────────┐      ┌──────────────┐
│ PC10 (I2S_CK)│─────→│ SCK (时钟)    │  ← I2S 位时钟
│ PA4  (I2S_WS)│─────→│ WS  (字选)    │  ← I2S 左右时钟
│ PC12 (I2S_SD)│←─────│ SD  (数据)    │  ← I2S 串行数据输出
│ 3.3V         │─────→│ VDD           │  ← 供电 1.8~3.3V
│ GND          │─────→│ GND           │  ← 地
│ GND          │─────→│ L/R (SEL)     │  ← 接地 = 数据在 WS=高 的槽位输出
└──────────────┘      └──────────────┘
```

> ⚠️ **SD 上拉电阻**：INMP441 的 SD 引脚是**开漏输出（open-drain）**，必须外接上拉电阻才能正确输出高电平。在 PC12 ← SD 这根线上，加一个 **4.7kΩ 电阻接到 3.3V**：
> ```
> 3.3V ──[4.7kΩ]──┬── PC12 (I2S3_SD)
>                  │
>                  └── INMP441 SD
> ```
> 如果不接：读到数据可能全是 `0xFF`（一直高）或全是 `0x00`（一直低），看起来"有数据"但实际不随声音变化。

#### ⚠️ L/R 引脚的重要发现（来自 ESP32 实测）

根据你的 `测试资料/esp32-i2s-mems-master/Readme.md` 作者的实际踩坑记录：

> *"Either the SEL config is wrong, or the ESP32 I2S channel handling. I had to use `I2S_CHANNEL_FMT_ONLY_RIGHT` whenever SEL pin was unconnected/ground, and `I2S_CHANNEL_FMT_ONLY_RIGHT` when SEL is high."*

INMP441 数据手册 §CHANNEL SELECT 原文：

| L/R 引脚电平 | WS 时序 | 数据输出时机 | INMP441 通道位置 |
|-------------|---------|-------------|-----------------|
| **GND (低)** | WS = **高** 期间 | SD 输出数据 | 数据在 **左声道槽**（WS=高） |
| **VDD (高)** | WS = **低** 期间 | SD 输出数据 | 数据在 **右声道槽**（WS=低） |

数据手册说 L/R=GND → 左声道，但 ESP32 实测发现需要配置成 `ONLY_RIGHT` 才能收到。这可能是 I2S 协议中 "左/右" 定义和 WS 极性的对应关系在不同厂商之间有差异。

**对于我们 STM32 的建议**：
- 先把 L/R **接地**（左声道模式）
- 在处理 DMA 数据时，分别尝试取左声道样本 `buffer[0], buffer[2], buffer[4]...`（偶数下标）和右声道样本 `buffer[1], buffer[3], buffer[5]...`（奇数下标）
- **哪个通道有变化的数据（不是恒定值），就用哪个**。这是最稳妥的做法。

```c
// 上板后首先跑这段验证：看 L 和 R 哪个通道有有效音频数据
int32_t sample_L = (int32_t)(dma_buf[0] >> 8);  // 偶数下标 = 左声道
int32_t sample_R = (int32_t)(dma_buf[1] >> 8);  // 奇数下标 = 右声道
printf("L=%08lX R=%08lX\r\n", dma_buf[0], dma_buf[1]);
// 有效通道的值会随声音变化；无效通道通常是固定值或噪声
```

#### 面包板高频信号注意事项

| 信号 | 频率 | 杜邦线建议 |
|------|------|-----------|
| SCK | 1.024 MHz | < 15cm，尽量短 |
| WS | 16 kHz | 随便接 |
| SD | ~1 MHz | < 15cm，与 SCK 平行走线 |

> SCK=1.024MHz 虽然不高，但面包板的寄生电容 + 长杜邦线可能导致上升沿变缓。如果数据不稳定，第一步就是换更短的线。

---

## 5. DMA 配置 — 双缓冲（Ping-Pong）

### 5.1 为什么需要双缓冲？

```
时间线：
┌─────────┐  ┌─────────┐  ┌─────────┐
│DMA→buf0 │  │DMA→buf1 │  │DMA→buf0 │ ...
│处理buf1 │  │处理buf0 │  │处理buf1 │
└─────────┘  └─────────┘  └─────────┘
```

DMA 填 buffer0 的同时，CPU 处理 buffer1，永不阻塞、零丢帧。

### 5.2 DMA 配置步骤

进入 **Pinout & Configuration → Connectivity → I2S3 → DMA Settings** 标签：

点 **Add** 添加：

| 参数 | 值 | 说明 |
|------|-----|------|
| DMA Request | **SPI3_RX** | I2S3 就是复用 SPI3 的 DMA 通道 |
| Stream | **DMA1 Stream 2** | 随便选，但要避开冲突 |
| Direction | **Peripheral to Memory** | 外设→内存 |
| Priority | **High** | 音频实时性要求高 |

配置 DMA Stream 的具体参数（点击刚添加的行）：

| 参数 | 值 | 说明 |
|------|-----|------|
| Mode | **Circular** | ⭐ 循环模式，不停歇采集 |
| Increment Address → Memory | ✅ 勾选 | 缓冲区地址递增 |
| Increment Address → Peripheral | ❌ 不勾选 | 外设数据寄存器地址固定 |
| Data Width → Peripheral | **Word** (32-bit) | 与 I2S 32-bit 帧对齐 |
| Data Width → Memory | **Word** (32-bit) | 缓冲区用 uint32_t |

> ⭐ **Data Width 必须用 Word (32-bit)**，不要用 Half Word。原因：
> 1. ESP32 验证：INMP441 只用 32-bit 模式才能正常工作
> 2. I2S 每个帧槽是 32-bit，HAL 用 32-bit 搬运不会做任何截断
> 3. 缓冲区用 `uint32_t[]`，收到完整 32-bit 帧后软件提取 24→16 bit PCM

### 5.3 FIFO 和 Burst（保持默认即可）

| 参数 | 值 |
|------|-----|
| Use Fifo | **Disabled**（或默认） |
| Burst Size | Single |

I2S 数据流连续均匀，不需要 FIFO。

### 5.4 双缓冲内存分配

DMA 的 Circular 模式 + HAL 的双缓冲 API：

```c
#define AUDIO_BUF_SIZE  512   // 每缓冲 512 个采样点

// 双缓冲（在 main.c 中定义，放在 SRAM 里不占栈）
static uint32_t audio_buf0[AUDIO_BUF_SIZE];
static uint32_t audio_buf1[AUDIO_BUF_SIZE];

// 使用 HAL_I2SEx_TransmitReceive_DMA() 配合 HAL_I2SEx_TransmitReceive_DMA() 
// 的双缓冲回调来切换

// HAL 提供了 HAL_I2S_RxHalfCpltCallback (buf0满) 和 HAL_I2S_RxCpltCallback (buf1满)
```

> ⚠️ CubeMX 生成的代码骨架只有基础 HAL 初始化。双缓冲的启用需要在用户代码中调用 `HAL_I2S_Receive_DMA()` 启动，然后 HAL 会在半完成/完成中断回调。

---

## 6. NVIC 中断优先级

进入 **Pinout & Configuration → System Core → NVIC**：

| 中断 | 是否使能 | 抢占优先级 | 子优先级 |
|------|---------|-----------|---------|
| **DMA1 Stream2 global interrupt** | ✅ 使能 | 1 | 0 |
| USART3 global interrupt | ✅ 使能 | 5 | 0 |
| SysTick | ✅ 使能 | 0 | 0 |

> **DMA 中断优先级设为最高非零优先级（1,0）**，确保音频缓冲区切换不被其他中断延迟。SysTick (0,0) 是 FreeRTOS 的时钟节拍，需要最高优先级。

---

## 7. 调试串口配置（USART1 / 或你习惯的串口）

### 7.1 PA9 (TX) + PA10 (RX) → USART1

在 Pinout view 搜 `USART1_TX` 和 `USART1_RX`，分别映射到 PA9 和 PA10。

| 参数 | 值 |
|------|-----|
| Mode | **Asynchronous** |
| Baud Rate | **921600**（高速打印） 或 **115200**（兼容） |
| Word Length | 8 Bits |
| Parity | None |
| Stop Bits | 1 |

### 7.2 重定向 printf（生成代码后）

在 `main.c` 或 `usart.c` 中添加：

```c
// 重定向 printf 到 USART1
int __io_putchar(int ch) {
    HAL_UART_Transmit(&huart1, (uint8_t *)&ch, 1, HAL_MAX_DELAY);
    return ch;
}
```

---

## 8. LED + 蜂鸣器 GPIO 配置（顺手配了）

在 Pinout view 配置这些 GPIO 为输出：

| 信号 | 引脚 | Mode | 初始电平 |
|------|------|------|---------|
| LED_ALARM | PB0 | Output Push-Pull | Low |
| LED_STATUS | PB1 | Output Push-Pull | Low |
| BUZZER | PE6 | Output Push-Pull | Low |

> 后续 A6 阶段会改为 TIM3 PWM 驱动 LED，但目前先用 GPIO 输出即可。

---

## 9. FreeRTOS 启用（可选：先不勾，采集跑通后再加）

如果打算直接用 FreeRTOS，CubeMX 中勾选：

**Pinout & Configuration → Middleware → FREERTOS → Interface: CMSIS_V2**

然后分配任务栈和优先级。**建议第一版不带 RTOS**，裸机跑通 I2S 采集后再加，降低调试复杂度。

> 裸机方案：`main()` 超级循环 + DMA 中断驱动。FreeRTOS 方案：把 I2S 中断改成通知 AudioTask 的方式。**先裸机，后 RTOS**。

---

## 10. 生成代码

### 10.1 Project Manager 设置

| 参数 | 值 |
|------|-----|
| Project Name | `sounddog_firmware` |
| Project Location | `C:\Projects\SoundDog\firmware\` |
| Application Structure | Basic |
| Toolchain / IDE | **STM32CubeIDE** |
| Minimum Heap Size | 0x400 |
| Minimum Stack Size | 0x800 |

### 10.2 Code Generator 设置

| 参数 | 值 |
|------|-----|
| Copy all used libraries into the project folder | ✅ |
| Generate peripheral initialization as a pair of `.c/.h` files | ✅ |
| Set all free pins as analog (低功耗) | ✅ |

### 10.3 生成

点击右上角 **GENERATE CODE**。完成后用 STM32CubeIDE 打开项目。

---

## 11. 生成的代码结构总览

```
firmware/
├── sounddog_firmware.ioc         ← CubeMX 配置文件
├── Core/
│   ├── Inc/
│   │   ├── main.h
│   │   ├── stm32f4xx_hal_conf.h
│   │   ├── stm32f4xx_it.h
│   │   ├── i2s.h                  ← 我们会添加的
│   │   └── gpio.h
│   └── Src/
│       ├── main.c
│       ├── stm32f4xx_it.c
│       ├── stm32f4xx_hal_msp.c    ← I2S3+DMA 引脚和时钟使能
│       ├── i2s.c                  ← 我们会添加的
│       └── gpio.c
└── Drivers/
    ├── CMSIS/
    │   └── DSP/                   ← CMSIS-DSP 库（后续添加）
    └── STM32F4xx_HAL_Driver/
```

CubeMX 自动生成的 I2S3 初始化代码在 `stm32f4xx_hal_msp.c` 的 `HAL_I2S_MspInit()` 中，包括：
- `__HAL_RCC_I2S3_CLK_ENABLE()` — 使能 I2S3 时钟
- `__HAL_RCC_GPIOC_CLK_ENABLE()` — PC10/PC12 时钟
- `__HAL_RCC_GPIOA_CLK_ENABLE()` — PA4 时钟
- `HAL_DMA_Init()` — DMA 初始化

---

## 12. 生成后的第一步验证代码

打开 `Core/Src/main.c`，在 `while(1)` 之前添加验证逻辑：

```c
/* USER CODE BEGIN 2 */

// ==================== SoundDog I2S3 + INMP441 采集验证 ====================
// 基于 ESP32 成功案例：32-bit 模式 + 软件提取 PCM
#define TEST_BUF_SIZE  1024
static uint32_t i2s_rx_buf[TEST_BUF_SIZE];  // 32-bit 缓冲区

printf("\r\n========================================\r\n");
printf("  SoundDog I2S3 + INMP441 Test\r\n");
printf("========================================\r\n");
printf("SYSCLK: %lu Hz\r\n", HAL_RCC_GetSysClockFreq());

// 启动 I2S3 DMA 循环接收
HAL_StatusTypeDef ret = HAL_I2S_Receive_DMA(&hi2s3, i2s_rx_buf, TEST_BUF_SIZE);
if (ret != HAL_OK) {
    printf("ERROR: I2S3 DMA start failed! ret=%d\r\n", ret);
    HAL_GPIO_WritePin(LED_ALARM_GPIO_Port, LED_ALARM_Pin, GPIO_PIN_SET);
    while(1);
}
printf("I2S3 DMA started OK. Waiting for data...\r\n");

HAL_Delay(100);  // 等约 2560 帧先填满缓冲区

// ====== 测试1：检查 L 和 R 哪个通道有数据 ======
printf("\r\n--- Test 1: L/R Channel Check ---\r\n");
printf("L_CH (even idx)  R_CH (odd idx)\r\n");
printf("--------         --------\r\n");
for (int i = 0; i < 8; i++) {
    uint32_t sample_L = i2s_rx_buf[i * 2];      // 偶数下标 = 左声道
    uint32_t sample_R = i2s_rx_buf[i * 2 + 1];  // 奇数下标 = 右声道
    printf("%08lX         %08lX\r\n", sample_L, sample_R);
}
// 有效通道：16 进制值会随声音变化（如 0x00XX_XXXX）
// 无效通道：固定值（如全 0x00000000 或全是 0x00XX_0000）

// ====== 测试2：提取 PCM 并观察波形 ======
printf("\r\n--- Test 2: Extracted 16-bit PCM ---\r\n");
printf("假设数据在 [31:8] 位置（INMP441 24-bit MSB 对齐）\r\n");

int which_channel = 0;  // 根据测试1的结果：0=左声道, 1=右声道
for (int i = which_channel; i < 64; i += 2) {  // 每隔一个取有效通道
    int32_t raw = (int32_t)i2s_rx_buf[i];
    int32_t pcm_24 = raw >> 8;      // 去掉低8位0填充 → 24-bit 有符号
    int16_t pcm_16 = (int16_t)(pcm_24 >> 8);  // 截取高16位
    printf("%6d ", pcm_16);
    if ((i + 1) % 16 == 0) printf("\r\n");
}

// ====== 测试3：持续打印，对着麦克风说话观察变化 ======
printf("\r\n--- Test 3: Live audio check (speak into mic!) ---\r\n");
printf("Max amplitude in each 64-sample block:\r\n");
HAL_Delay(500);
for (int block = 0; block < 10; block++) {
    int32_t max_val = 0;
    for (int i = 0; i < 64; i++) {
        int32_t val = abs((int32_t)(i2s_rx_buf[i] >> 8));
        if (val > max_val) max_val = val;
    }
    printf("Block %d: max=%ld\r\n", block, max_val);
    HAL_Delay(50);
}

HAL_GPIO_WritePin(LED_STATUS_GPIO_Port, LED_STATUS_Pin, GPIO_PIN_SET);
printf("\r\n=== Test Complete ===\r\n");

/* USER CODE END 2 */
```

## 13. 验证标准

| 检查项 | 通过标准 |
|--------|---------|
| I2S3 DMA 启动 | `HAL_I2S_Receive_DMA()` 返回 `HAL_OK` |
| 有数据进来 | `i2s_rx_buf` 不全为 0 |
| 数据在变化 | 连续两个 buffer 的值不完全相同 |
| PCM 值范围合理 | 安静时 ~±500，说话/敲击时 ~±5000~30000 |
| 无丢帧 | 打印两个 buffer 的序号，看是否连续 |

---

## 14. 常见坑和排查

### 14.1 整个 buffer 全是 0

| 可能原因 | 排查方法 |
|---------|---------|
| INMP441 供电没接 | 万用表量 VDD=3.3V |
| L/R 引脚悬空 | L/R 必须接 GND 或 VDD，不能悬空 |
| I2S 根本没输出时钟 | 逻辑分析仪/示波器量 PC10(SCK) 有没有方波 |
| DMA 方向配反了 | CubeMX 检查是 Peripheral→Memory |

### 14.2 SCK 没有方波

```
PC10 无波形 = I2S3 没有时钟输出
└── 检查：I2S3 时钟是否使能（HAL_I2S_MspInit 中）
└── 检查：时钟树 PLLI2S 是否正确输出
```

### 14.3 SCK 有波形但数据全是 0xFF 或 0x00

| 可能原因 | 说明 |
|---------|------|
| SD 引脚接错 | PC12 是否接到 INMP441 的 SD |
| 引脚 AF 没配 | PC12 必须是 AF6 |
| L/R 引脚悬空 | **L/R 必须接 GND 或 VDD**，悬空 = 不确定状态 |
| 通道取错了 | L/R=GND 可能在左或右通道（见 4.3 节通道检测方法），尝试分别取偶数/奇数下标 |
| WS 极性反了 | 尝试在 CubeMX 中勾选 I2S3 的 CKP=High 试试 |

### 14.4 数据值恒为固定值（不随声音变化）

这是最常见的 INMP441 问题。按以下顺序排查：

| 步骤 | 排查项 | 判断标准 |
|------|--------|---------|
| 1 | 检查 L 和 R 两个通道 | 其中一个通道会有变化的数据（见验证代码测试1） |
| 2 | 对麦克风吹气/说话 | 有效通道的值应有明显变化（测试3） |
| 3 | 万用表量 L/R 引脚 | 确认确实是 0V（GND）或 3.3V（VDD），不是悬空 |
| 4 | 换一根短的杜邦线（<10cm） | 面包板的长线可能引入反射 |

### 14.5 数据有变化但值域不对（太小或太偏）

| 现象 | 可能原因 |
|------|---------|
| 全是 `0x00000XXX` 小值 | INMP441 24-bit 数据在 32-bit 槽中的位置可能不在 [31:8] |
| 全是 `0xFFFFFXXX` 负值 | MSB 对齐方式不对，尝试不同移位量 |
| 高 16 位和低 16 位颠倒了 | HAL I2S 可能存在字节序问题（大小端） |

> **终极调试法（来自 ESP32 社区经验）**：直接用 `printf("%08lX\r\n", raw)` 打印原始 32-bit 值，对着麦克风吹气，观察：
> - 有效的 24-bit PCM 数据应该在吹气时 BIT[23:8] 有明显变化
> - 低 8 位 BIT[7:0] 理论上全是 0（INMP441 填充）
> - 高 8 位 BIT[31:24] 在 24-bit 补码下：负数 = 0xFF，正数 = 0x00
> 找到数据实际所在的位域后，再确定位移量。

### 14.6 时钟精度问题

CubeMX Clock Configuration → 切到 I2S3 → 看 `Actual Audio Frequency`：
- 目标 16,000 Hz
- 实际应该在 15,995 ~ 16,005 Hz 之间
- 偏差 > 50 Hz → 时钟树计算有问题，回到第 3 节检查

---

## 15. 做完这步之后

一旦 I2S3 有数据且 PCM 值合理：

1. ✅ **把这条链路固化成文件**：`Core/Src/audio/i2s_drv.c` + `Core/Inc/i2s_drv.h`
2. ✅ **实现 DMA 双缓冲回调**：`HAL_I2S_RxHalfCpltCallback()` 和 `HAL_I2S_RxCpltCallback()`
3. ✅ **用 Python 画波形验证**（见 `准备工作.md` 中的脚本）
4. ✅ 进入 **A2：FFT + 频谱可视化**

---

> **总结**：
>
> ```
> CubeMX 配置核心 5 条（来自 INMP441 数据手册 + ESP32 社区验证）：
>
> ① 时钟树：PLLM=8, PLLI2S_N=256, PLLI2S_R=5 → I2SxCLK=51.2MHz
> ② 数据格式：32 Bits Data on 32 Bits Frame（ESP32 已验证通过）
> ③ DMA：Circular + Word(32-bit) × Word(32-bit)
> ④ INMP441 L/R：接地 → 数据在左声道槽（WS=高时输出）
> ⑤ 接线：杜邦线三根 < 15cm
>
> 只要这 5 条做对，SCK=1.024MHz，Fs=16kHz 精确，音频链路一定能通。
> ```


## 附录 A：INMP441 数据手册关键参数速查

> 来源：InvenSense INMP441 数据手册（你提供的 PDF）

### A.1 模拟性能（TABLE 1. ELECTRICAL CHARACTERISTICS）

| 参数 | 值 | 测试条件 |
|------|-----|---------|
| 信噪比 SNR | **61 dBA** | 1kHz, 94dB SPL |
| 动态范围 | 61 dB | 1kHz, 94dB SPL |
| 灵敏度 | **-26 dBFS** | 1kHz, 94dB SPL |
| 灵敏度公差 | ±1 dB | 典型 |
| 频率响应 | **60 Hz ~ 15 kHz** | -3dB 点 |
| 总谐波失真 THD | < 0.1% | 94dB SPL |
| 最大声压级 | 126 dB SPL | THD < 10% |
| 直流偏移 | ±0.5% FS | 最大 |

### A.2 数字接口时序（TABLE 3. I2S CHARACTERISTICS）

| 参数 | 最小值 | 典型值 | 最大值 | 单位 |
|------|--------|--------|--------|------|
| SCK 频率 | — | — | **4.0** | MHz |
| SCK 占空比 | 40 | 50 | 60 | % |
| Fs (采样率) | — | — | 62.5 | kHz |
| 内部抽取比 | — | **64** | — | — |
| 信号带宽 | — | — | 0.45 × Fs | Hz |
| t<sub>setup</sub> (SD→SCK) | 20 | — | — | ns |
| t<sub>hold</sub> (SCK→SD) | 20 | — | — | ns |

> ⭐ 关键：SCK max = 4.0 MHz，我们 1.024 MHz 远在安全范围内。

### A.3 I2S 数据格式（FIGURE 6. I2S PHILIPS FORMAT）

```
WS (LRCK):  高 = 左声道, 低 = 右声道
SD:         MSB 先出, 24-bit 补码, 在 WS 边沿后的第 2 个 SCK 开始
帧格式:     64 SCK/帧 = 32 SCK/左 + 32 SCK/右
空白位:     每个通道 32-bit 槽中，仅前 24-bit 有效，低 8-bit 为 0
```

### A.4 L/R 通道选择（CHANNEL SELECT）

| L/R 引脚电平 | 有效数据槽 | WS 极性 | 数据手册说明 |
|-------------|-----------|---------|-------------|
| **低 (GND)** | 左声道槽 | WS = **高** | 数据在 WS 跳变到高后输出 |
| **高 (VDD)** | 右声道槽 | WS = **低** | 数据在 WS 跳变到低后输出 |

> ⚠️ ESP32 社区实测发现：L/R=GND 时，配置成 `ONLY_RIGHT` 才能收到数据（与数据手册文档定义相反）。STM32 上板后按测试1方法验证实际数据出现在左通道还是右通道。

### A.5 功耗

| 模式 | 电流 | 说明 |
|------|------|------|
| 正常模式 | **1.4 mA** | 持续采集 |
| 睡眠模式 | < 1 μA | L/R 引脚拉高进入睡眠 |
| 启动时间 | ~8 ms | 从睡眠到正常输出稳定 |

> F407 3.3V 引脚最大输出 800mA，INMP441 只需 1.4mA，轻松带动。

---

## 附录 B：ESP32 参考代码关键配置对照

> 来源：你提供的 `INMP441全向麦克风资料/测试资料/esp32-i2s-mems-master/`

| ESP32 参数 | 三个示例统一值 | 对应的 STM32 CubeMX 配置 |
|-----------|---------------|------------------------|
| `.bits_per_sample` | `I2S_BITS_PER_SAMPLE_32BIT` | Data Format: **32 Bits** |
| `.sample_rate` | `16000` | Audio Frequency: **16 kHz** |
| `.channel_format` | `I2S_CHANNEL_FMT_ONLY_RIGHT` | 取奇数下标数组元素 |
| `.communication_format` | `I2S_COMM_FORMAT_I2S \| I2S_COMM_FORMAT_I2S_MSB` | Standard: **Philips** |
| `.mode` | `I2S_MODE_MASTER \| I2S_MODE_RX` | Mode: **Master Receive** |
| `.dma_buf_count` | 4~8 | Circular DMA 双缓冲 |
| 数据提取 | `samples[i] >> 14` (VU计) | `raw >> 8` (24-bit PCM) |

## 附录 C：外部资料路径

```
桌面\stm32F407ZGT6\INMP441全向麦克风资料\
├── INMP441.pdf                          ← 官方数据手册
└── 测试资料\
    ├── 测试说明.doc                      ← 测试说明文档
    └── esp32-i2s-mems-master\           ← ESP32 开源驱动（已验证）
        ├── Readme.md                    ← 关键踩坑记录
        └── examples\
            ├── InputSerialPlotter\      ← 串口波形打印
            ├── NoiseLevel\              ← 噪声电平计算
            └── VUMeterDemo\             ← VU 表显示
```
