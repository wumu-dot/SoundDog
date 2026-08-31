# CubeMX 实操步骤 — SoundDog STM32F407 完整外设树

> 跟着这份指南，在 CubeMX 里一步步点，30 分钟内完成全部外设配置。
>
> 外部参考 IOC：`桌面\stm32F407ZGT6\芯片stm32F407\【1】参考例程\HAL库\0.CubeMX配置参考\0.CubeMX配置参考\F407ZG.ioc`
> ⚠️ 参考 IOC 中 PC10/PC12 被 SDIO 占用，I2S3 需要释放这两个引脚。

***

## 总览：SoundDog 需要配置的外设一览

```
I2S3   — PC10(CK) + PA4(WS) + PC12(SD)     ← INMP441 音频输入
DMA1   — Stream2 (SPI3_RX / I2S3_RX)        ← 音频 DMA 搬运
I2C1   — PB6(SCL) + PB7(SDA)                ← OLED + SHT30 温湿度
USART1 — PA9(TX) + PA10(RX)                 ← 调试串口 printf
USART3 — PD8(TX) + PD9(RX)                  ← RS485 通信
GPIO   — PD11(Output)                       ← MAX3485 DE/RE 方向控制
GPIO   — PB0 + PB1 + PE6(Output)            ← LED红 + LED绿 + Buzzer
TIM3   — CH3(PB0) + CH4(PB1)                ← PWM 驱动 LED
HSE    — PH0 + PH1                          ← 8MHz 外部晶振
SWD    — PA13 + PA14                        ← 调试接口
```

***

## Step 1：新建项目

```
File → New Project → 搜索框输入 "STM32F407ZGT6"
→ 选中 STM32F407ZGTx (LQFP144) → Start Project
```

> CubeMX 版本建议 6.7+（参考 IOC 用的是 6.7.0）

***

## Step 2：System Core 配置

### 2.1 RCC 时钟源

`Pinout & Configuration → System Core → RCC`

| 参数                     | 值                             |
| ---------------------- | ----------------------------- |
| High Speed Clock (HSE) | **Crystal/Ceramic Resonator** |
| Low Speed Clock (LSE)  | **Disable**（不用 RTC）           |

> 两个外部晶振引脚会自动绑定：PH0-OSC\_IN、PH1-OSC\_OUT

### 2.2 SYS 调试接口

`Pinout & Configuration → System Core → SYS`

| 参数    | 值               |
| ----- | --------------- |
| Debug | **Serial Wire** |

> 绑定引脚：PA13(SWDIO)、PA14(SWCLK)。千万别禁用，禁了就烧不进程序了。

### 2.3 NVIC 中断优先级（先默认，后面再回来调）

暂时不动。生成代码后优先级在代码里手动设也行。

***

## Step 3：时钟树 Clock Configuration

切换到 `Clock Configuration` 标签页。

### 3.1 HSE 输入

确认 HSE 输入 = **8 MHz**（F407 核心板板载 8MHz 晶振）。

### 3.2 PLL 配置（系统主频 168MHz）

| 节点             | 输入值      |
| -------------- | -------- |
| PLL Source Mux | **HSE**  |
| PLLM           | **/8**   |
| PLLN           | **×336** |
| PLLP           | **/2**   |

此时右上角 `HCLK` 应该显示 **168 MHz**。如果 CubeMX 自动切成了 HSI，手动拉回 HSE → PLL。

### 3.3 总线分频

| 节点             | 值      | 频率                    |
| -------------- | ------ | --------------------- |
| AHB Prescaler  | /1     | 168 MHz               |
| APB1 Prescaler | **/4** | 42 MHz（APB1 最大 42MHz） |
| APB2 Prescaler | /2     | 84 MHz                |

> CubeMX 一般会自动设好，检查一下 APB1 不是 /2（那样 PCLK1=84MHz 超出 F407 规格），必须是 /4。

### 3.4 PLLI2S 音频时钟

| 节点               | 输入值           |
| ---------------- | ------------- |
| I2S Clock Source | **PLLI2S\_R** |
| PLLI2S\_N        | **×256**      |
| PLLI2S\_R        | **/5**        |

验证：切回 Pinout 页面，配完 I2S3 后再回来确认 SCK 是 1.024 MHz。

### 3.5 验证

```
SYSCLK      168 MHz ✅
HCLK        168 MHz ✅
APB1 Timer   84 MHz ✅ (×2 of PCLK1)
APB2 Timer  168 MHz ✅ (×2 of PCLK2)
PLLI2S_R    51.2 MHz ✅ (稍后验证 SCK=1.024MHz)
```

***

## Step 4：I2S3 外设配置

### 4.1 释放冲突引脚

> ⚠️ **关键操作**：参考 IOC 中 PC10/PC12 被 SDIO 占用了。你需要确保它们没有被分配给任何其他外设。

在 Pinout view 中：

* 点击 PC10 → 确认功能列表中没有其他勾选

* 点击 PC12 → 确认功能列表中没有其他勾选

如果 Pinout view 中 PC10/PC12 显示为黄色/橙色（已被 SDIO 或其他外设使用），右键该引脚 → `Reset Pin` 清除。

### 4.2 分配 I2S3 引脚

在 Pinout view 左侧搜索框输入 `I2S3`：

| 信号           | 搜索        | 映射到芯片的哪个位置      |
| ------------ | --------- | --------------- |
| **I2S3\_CK** | `I2S3_CK` | 点选芯片上的 **PC10** |
| **I2S3\_WS** | `I2S3_WS` | 点选芯片上的 **PA4**  |
| **I2S3\_SD** | `I2S3_SD` | 点选芯片上的 **PC12** |

操作方式：在左侧 IP 面板展开 `I2S3`，把三个信号分别拖到对应引脚上；或者直接在芯片图上右键引脚 → 选择对应功能。

> ⚠️ 如果 CubeMX 提示 PC10/PC12 被 SDIO 占用，选择 Reset SDIO 或者不启用 SDIO。SoundDog 不需要 SD 卡（Phase B 可能加，但目前先不做）。

### 4.3 配置 I2S3 参数

`Pinout & Configuration → Connectivity → I2S3`

在 Mode 面板中：

| 参数                   | 值                                 |
| -------------------- | --------------------------------- |
| Mode                 | **Master Receive**                |
| I2S Standard         | **Philips Standard**              |
| Data Format          | **32 Bits Data on 32 Bits Frame** |
| Audio Frequency      | **16 kHz**                        |
| Clock Source         | **PLLI2S**                        |
| Clock Polarity (CKP) | **Low**                           |
| Master Clock Output  | **Disabled**                      |

配完后：

1. 切回 **Clock Configuration** 页签
2. 找到 I2S3 相关显示
3. 确认 SCK（也叫 I2S3 bit clock）是 **1.024 MHz**
4. 确认 Audio Frequency Actual = **16,000 Hz**（偏差 ±5Hz 内）

***

## Step 5：DMA 配置（I2S3\_RX）

`Pinout & Configuration → Connectivity → I2S3 → DMA Settings`

点 **Add** 添加一条 DMA 请求：

| 参数          | 值                        |
| ----------- | ------------------------ |
| DMA Request | **SPI3\_RX**             |
| Channel     | DMA1 Channel 0           |
| Stream      | **DMA1 Stream 2**（选不冲突的） |
| Direction   | **Peripheral to Memory** |
| Priority    | **High**                 |

然后点击刚添加的这一行，进入配置：

| 参数                             | 值                 |
| ------------------------------ | ----------------- |
| Mode                           | **Circular**      |
| Increment Address → Memory     | ✅ 勾选              |
| Increment Address → Peripheral | ❌ 不勾选             |
| Data Width → Peripheral        | **Word (32-bit)** |
| Data Width → Memory            | **Word (32-bit)** |
| FIFO Mode                      | **Disable**（默认）   |

***

## Step 6：调试串口 USART1

### 6.1 引脚分配

搜索 `USART1`，分别分配：

| 信号         | 引脚       |
| ---------- | -------- |
| USART1\_TX | **PA9**  |
| USART1\_RX | **PA10** |

### 6.2 参数配置

`Pinout & Configuration → Connectivity → USART1`

| 参数          | 值                    |
| ----------- | -------------------- |
| Mode        | **Asynchronous**     |
| Baud Rate   | **921600**（或 115200） |
| Word Length | 8 Bits               |
| Parity      | None                 |
| Stop Bits   | 1                    |

***

## Step 7：RS485 串口 USART3 + 方向控制

### 7.1 引脚分配

搜索 `USART3`：

| 信号         | 引脚      |
| ---------- | ------- |
| USART3\_TX | **PD8** |
| USART3\_RX | **PD9** |

### 7.2 配置（与 USART1 一样）

| 参数          | 值                      |
| ----------- | ---------------------- |
| Mode        | **Asynchronous**       |
| Baud Rate   | **115200**（RS485 先测低速） |
| Word Length | 8 Bits                 |
| Parity      | None                   |
| Stop Bits   | 1                      |

### 7.3 MAX3485 DE/RE 方向控制 GPIO

搜索 `PD11`，右键 → **GPIO\_Output**：

| 参数                     | 值                 |
| ---------------------- | ----------------- |
| GPIO Mode              | Output Push Pull  |
| GPIO Pull-up/Pull-down | Pull-down（默认接收模式） |
| Maximum output speed   | Low               |
| User Label             | **RS485\_DE**     |

***

## Step 8：I2C1（OLED + SHT30 共用）

### 8.1 引脚分配

搜索 `I2C1`：

| 信号        | 引脚      |
| --------- | ------- |
| I2C1\_SCL | **PB6** |
| I2C1\_SDA | **PB7** |

### 8.2 参数

`Pinout & Configuration → Connectivity → I2C1`

| 参数             | 值                      |
| -------------- | ---------------------- |
| I2C Speed Mode | **Fast Mode** (400kHz) |
| Clock Speed    | 400000                 |

> SSD1306 OLED 和 SHT30 都支持 400kHz Fast Mode，挂同一条 I2C1 总线没问题。

***

## Step 9：LED + 蜂鸣器 GPIO

在 Pinout view 搜索以下引脚，设为 **GPIO\_Output**：

| 引脚      | User Label  | 用途               |
| ------- | ----------- | ---------------- |
| **PB0** | LED\_ALARM  | 红灯（后续改 TIM3 PWM） |
| **PB1** | LED\_STATUS | 绿灯（后续改 TIM3 PWM） |
| **PE6** | BUZZER      | 蜂鸣器              |

配置：

| 参数                     | 值                        |
| ---------------------- | ------------------------ |
| GPIO Mode              | Output Push Pull         |
| GPIO Pull-up/Pull-down | No pull-up, no pull-down |
| Maximum output speed   | Low                      |
| GPIO output level      | Low（初始灭）                 |

> 后续 A6 阶段想把 PB0/PB1 改成 TIM3 PWM 的话，回来改即可。目前 GPIO 输出够用。

***

## Step 10（可选）：TIM3 PWM 预分配

如果你一次性想配好 PWM：

搜索 `TIM3`，把 CH3 和 CH4 分别分配到 PB0 和 PB1，Mode 选 **PWM Generation CH3 / CH4**。

***

## Step 11：NVIC 中断优先级（最后回来设）

`Pinout & Configuration → System Core → NVIC`

| 中断                            | Enable | Preemption Priority | Sub Priority |
| ----------------------------- | ------ | ------------------- | ------------ |
| DMA1 Stream2 global interrupt | ✅      | 1                   | 0            |
| USART3 global interrupt       | ✅      | 5                   | 0            |
| SysTick (Cortex)              | ✅      | 0                   | 0            |

> Priority Group 选 **4 bits for pre-emption priority**，这样最多 16 级抢占。

***

## Step 12：Project Manager

### 12.1 Project 标签

| 参数                    | 值                                            |
| --------------------- | -------------------------------------------- |
| Project Name          | `sounddog_firmware`                          |
| Project Location      | `C:\Projects\SoundDog\firmware\`             |
| Application Structure | **Basic**                                    |
| Toolchain / IDE       | **MDK-ARM** (Keil) 或 **STM32CubeIDE**（看你用哪个） |

### 12.2 Code Generator 标签

| 参数                                                            | 值 |
| ------------------------------------------------------------- | - |
| Copy all used libraries into the project folder               | ✅ |
| Generate peripheral initialization as a pair of `.c/.h` files | ✅ |
| Set all free pins as analog                                   | ✅ |
| Keep User Code when re-generating                             | ✅ |
| Delete previously generated files when not re-generated       | ✅ |

***

## Step 13：生成代码

点击右上角 **GENERATE CODE**。

生成完成后弹出的窗口选 **Open Project**，IDE 自动打开项目。

***

## Step 14：生成后立即做的事

### 14.1 验证 stm32f4xx\_hal\_conf.h

确认以下宏都启用了（不加注释）：

```c
#define HAL_MODULE_ENABLED
#define HAL_I2S_MODULE_ENABLED
#define HAL_I2C_MODULE_ENABLED
#define HAL_UART_MODULE_ENABLED
#define HAL_DMA_MODULE_ENABLED
#define HAL_TIM_MODULE_ENABLED
```

### 14.2 快速编译检查

先不写任何用户代码，直接编译一次。如果报错，检查是不是 HAL 的 Clock Configuration 输出有问题。

### 14.3 验证引脚映射

打开 `Core/Src/gpio.c`，确认 PB0、PB1、PE6、PD11 的初始化代码都在。

***

## 配完后，你的外设树应该长这样

```
STM32F407ZGT6
  ├── RCC: HSE Crystal, LSE Disabled
  ├── SYS: Serial Wire (PA13/PA14)
  ├── NVIC: DMA1_S2=1/0, USART3=5/0, SysTick=0/0
  ├── I2S3: Master RX, Philips, 32bit, 16kHz
  │   ├── PC10 = CK
  │   ├── PA4  = WS
  │   └── PC12 = SD
  ├── DMA1 Stream2: SPI3_RX, Circular, 32-bit Wide
  ├── USART1: Async, 921600, PA9/PA10
  ├── USART3: Async, 115200, PD8/PD9
  ├── I2C1: Fast Mode, PB6/PB7
  ├── GPIO_Out: PD11(RS485_DE), PB0(LED_ALARM), PB1(LED_STATUS), PE6(BUZZER)
  └── Clock:
      ├── SYSCLK = 168 MHz (HSE → PLL)
      ├── PLLI2S = 51.2 MHz → I2S3 → SCK=1.024MHz
      └── APB1=42MHz, APB2=84MHz
```

***

## 常见操作问题

### Q: 搜 I2S3\_SD 找不到？

A: I2S3\_SD 在 CubeMX 某些版本中复用名为 `SPI3_MOSI/I2S3_SD`，搜 `SPI3` 也能找到 PC12。

### Q: PC10 显示橙色被占用？

A: 被 SDIO\_D2 占用了。在 Pinout view 中找到 SDIO，把所有 SDIO 引脚 Reset（我们不需要 SD 卡）。

### Q: 时钟树红色/黄色警告？

A: 点一下 `Resolve Clock Issues` 按钮（灯泡图标），CubeMX 会自动修正。如果还不绿，检查 PLLM/PLLN/PLLP 的值是否正确。

### Q: I2S3 Audio Frequency 不是精确 16kHz？

A: 偏差在 ±5Hz 以内完全够用。如果偏差很大（比如 8kHz），说明 Data Format 选错了或者 PLLI2S 算错了。回到第 3.4 和第 4.3 节检查。

***

> 配完后跟我说一声，咱们进入下一步：**生成代码 + 编写 I2S3 验证代码**。

