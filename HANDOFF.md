# SoundDog 项目交接文档

> **项目描述**：SoundDog 是一个 STM32F407ZGT6 音频异常检测项目。完整链路：INMP441 麦克风 + I2S3 + DMA 采集 16kHz 音频 → FreeRTOS 队列 → 256 点 FFT 频谱（OLED 柱状图 + BANDS 串口）→ MFCC 13 维特征（预加重/分帧/Mel/Log/DCT，100 帧/s）→ K-means 16 质心模版欧氏距离 d_min 实时比对 → 连续帧防抖判决（在建）。主控 HSE 8MHz → SYSCLK 168MHz，RTOS 为 FreeRTOS CMSIS-RTOS\_v2，工具链 Makefile + arm-none-eabi-gcc。

> 交接日期：2026-08-16（2026-09-01 A4 中期更新）
> 交接原因：切换 AI 助手
> **最重要的一句话（2026-09-01）**：A1/A2/A3 全线收官，A4-01（离线模版）+ A4-02（距离比对）已收官；**A4-03 连续帧防抖正处于阶段 2 设计已确认、阶段 3 编码待启动的停等点**（方案 B 双轨判据 + 阈值 6.0 已由人拍板）——接手第一件事是读 §零 的快速上手路径，然后从 A4-03 阶段 3 继续。
> ~~最重要的一句话（2026-08-28）：A1 音频采集链路已全面打通……~~（A1/A2 均已收官，见 §七）

***

## 零、新 Agent 快速上手（10 分钟路径，按顺序读）

1. **`.claude/rules.md` §0**（30s）——当前主线、最相关规则、架构格局约束。**v3.0 共 32 条规则（R0\~R31），冲突仲裁顺序：安全红线 > 验证证据 > 流程停等 > Token 优化**
2. **`docs/features/INDEX.md`**（1min）——全部 FEAT 状态表（A1~A8 及子项红绿灯）
3. **本文档 §零点五**（2min）——在途任务 A4-03 的精确状态与设计决策
4. **`docs/features/维护地图.md`**（2min）——每个源文件/脚本/数据文件的归属 FEAT 与状态
5. **`docs/features/FEAT-A4-03-连续帧防抖.md`**（4min）——在途 FEAT 全文（重点 §1.4 参数、§执行日志）
6. 开工前自检：`git status`（应干净）、`git log --oneline -5`（HEAD 应为 A4-02 收官提交）

**新会话第一条回复前**：按 rules.md §0 元规则输出热加载摘要块（当前阶段/重点规则/禁止触碰/已知约束）。

**红线速记（详见 rules.md §2）**：不碰 Core/Drivers/lvgl/Middlewares 核心逻辑（R19）；改引脚先看 hal\_msp.c（R20）；声称"完成"必贴证据（R21）；编码前先查三源（R31，2026-09-01 新增）。

## 零点五、当前在途任务：A4-03 连续帧防抖（阶段 2→3 停等点）

> 这是接手后唯一需要立即推进的工作。2026-09-01 状态快照（详细设计以 FEAT 文档与阶段 2 设计输出为准）：

* **进度**：阶段 1（依赖确认/影响范围）+ 阶段 2（状态机设计）✅ 已完成并经人确认；**下一步 = 阶段 3 编码**
* **已拍板决策**（人确认，不可推翻除非本人）：
  - **方案 B 双轨判据**：轨1 连续 30 帧异常（规格判据）+ 轨2 100 帧滑窗 m-of-n ≥30 帧比率判据（敲击瞬态用），OR 先到先报；解除单轨连续 60 帧正常
  - **阈值 TH=6.0**（A4-02 存证 CSV 定标：正常误判 1%/金属漏判 16%）
  - 新文件 `BSP/debounce.c/h` 纯状态机 + freertos.c mel\_frame\_cb 内挂接 + 串口 `ALARM on/off` 行（事件触发即时打印，不走 1s 节流）
* **设计要点**：100 bit 位图环形缓冲做滑窗（O(1)）；uint16 计数封顶 60000（AC-08）；静态初始化即看门狗复位清零；A6 灯/蜂鸣器仅预留 `debounce_get_alarm()` 接口
* **挂接链**（freertos.c `mel_frame_cb`）：mel\_process → mfcc\_dct\_process → dist\_process（s\_dmin）→ **debounce\_process（本 FEAT 新增）**
* **阶段 3 待办**：写 debounce.c/h → Makefile 登记（BUG-002 教训）→ build.bat → build\_and\_flash.bat → 阶段 4 按 §3.3 边界用例实测（29/30/59/60 帧）
* **R27 三源结论已备**（A4-02 阶段 4 已研，直接引用）：prmon m-of-n 比率判据 / Edge Impulse 窗均值 / ViolaWake PR#34（max 不得取自抽稀子集）/ AdaBEAM mean+max 双轨先例

***

## 一、项目介绍

### 1.1 一句话描述

**SoundDog — 工业异响音频检测节点**：一个可复用的、支持 RS485 工业总线组网的音频异常检测节点，通过 MFCC 特征提取 + 频谱比对，在 STM32F4 上实时判断设备运行声音是否异常并上报报警。

> *"Like a watchdog, but it listens."*

### 1.2 核心价值

| 维度   | 说明                              |
| ---- | ------------------------------- |
| 可复用  | 框架通用，换传感器/检测参数/通信协议，核心代码不改      |
| 工业级  | RS485 差分总线，抗干扰，支持 32+ 节点        |
| 边缘检测 | 全部运算在 STM32 端完成，不依赖云端，延迟 < 50ms |
| 低成本  | 单节点 BOM < 80 元                  |
| 确定性  | 传统 DSP 算法，行为可预测、可解释             |

### 1.3 技术路线（三阶段）

```
Phase A: 传统 DSP（当前）→ MFCC + 特征向量 → 模版比对/阈值 → 正常/异常
Phase B: 数据积累      → 用 Phase A 节点采集标注样本
Phase C: 模型升级(可选) → MFCC 特征 → CMSIS-NN 二分类
```

当前处于 **Phase A** 的模版比对检测段（A4：已完成 01 离线模版 + 02 距离比对，03 防抖在建），距离报警输出（A6）和 RS485 组网（A5）还有两个 FEAT 组。

### 1.3.1 固件音频链路现状（A4-02 收官时点，freertos.c 数据流）

```
ISR(DMA) → audio_queue → specTask:
  ├─ fft_run → spectrum → OLED 柱状图 + SPEC/BANDS 行（1s 节流）
  └─ mfcc_feed → [mel_frame_cb 回调, 100 帧/s]:
       mel_process(32 维 Mel 能量)
       → mfcc_dct_process(13 维 MFCC, s_mfcc13)
       → dist_process(16 质心欧氏 d_min, s_dmin + 1s 窗峰值)
       → [A4-03 待挂] debounce_process(双轨判决)
  串口行前缀：boot/I2S_DRV/SPEC/BANDS/MFCC13/MEL/DMIN(d=瞬时 m=峰值)
```

**架构铁律**：specTask 单消费者、USART1 单写者、fft\_run 不可重入、ISR 仅拷贝入队（新增消费方必须进 mel\_frame\_cb 回调链，不得另起任务）。

### 1.4 技术栈

* 主控：STM32F407ZGT6，HSE 8MHz，SYSCLK 168MHz

* RTOS：FreeRTOS CMSIS-RTOS\_v2（当前 1 个占位任务 defaultTask，规划 6 个）

* 工具链：Makefile + mingw32-make + arm-none-eabi-gcc（路径 `C:\ST\STM32CubeCLT_1.21.0\`）

* 外设规划：I2S3(INMP441) / I2C1(OLED+SHT30) / USART1(调试) / USART3(RS485) / TIM3(LED PWM) / SPI2(Flash)

> **CodeGraph 已索引**：`.codegraph/codegraph.db`（10.4MB）已覆盖全部代码（含 `firmware/soundDog/`），可直接用 `codegraph_explore` / `codegraph explore` 查代码，无需重新索引。偶发 watchdog 重启（日志 #850 报错）不影响索引数据，重启即恢复。

***

## 二、软件成果（全部完好，可直接复用）

### 2.1 代码位置

工程在 `firmware/soundDog/`（CubeMX 生成 + 手写驱动）：

| 文件                                | 说明                                           | 状态                                    |
| --------------------------------- | -------------------------------------------- | ------------------------------------- |
| `soundDog.ioc`                    | CubeMX 工程文件                                  | ✅ 已配好 I2S3 + USART1 + FreeRTOS + TIM7 |
| `Src/i2s.c`                       | I2S3 初始化（Master RX / 32bit / 16kHz / PLLI2S） | ✅                                     |
| `Src/i2s_drv.c` + `Inc/i2s_drv.h` | 手写 DMA 双缓冲驱动                                 | ✅                                     |
| `Src/main.c`                      | printf 重定向 + I2S DMA 启动 + 测试回调               | ✅                                     |
| `Src/stm32f4xx_hal_msp.c`         | 加了 JTAG 释放代码                                 | ✅                                     |
| `Inc/pin_config.h`                | 引脚映射权威来源                                     | ✅                                     |

### 2.2 关键配置（已验证正确）

* **I2S3 时钟**：HSE 8MHz → /PLLM(8) → ×PLLI2S\_N(256) → /PLLI2S\_R(5) → 51.2MHz → I2SDIV=25（SCK = 51.2M/(2×25) = 1.024MHz）→ /64 → Fs 16kHz（误差 0%）

* **I2S3 参数**：Master Receive / Philips / 32-bit frame / 16kHz / MCLK disable / PLL clock

* **引脚**：I2S3\_WS=PA4, I2S3\_CK=PB3, I2S3\_SD=PB5（从 PC10/PC12 换过来，因为 PC10 是焊盘洞不好接）

* **DMA**：DMA1\_Stream2\_Channel0，Circular，Word(32bit)，Priority High，NVIC 优先级 5

* **HAL 时基**：TIM7（独立于 FreeRTOS SysTick）

* **中断优先级铁律**：NVIC Group 4，`configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY=5`，0\~4 禁调 FromISR API

### 2.3 脚本（firmware/ 下）

| 脚本                                    | 用途                           |
| ------------------------------------- | ---------------------------- |
| `build.bat`                           | 只编译                          |
| `build_and_flash.bat`                 | 编译 + SWD 烧录                  |
| `flash_isp.bat`                       | ISP 串口烧录（救砖用，BOOT0 + USART1） |
| `build.sh` / `flash.sh`               | Git Bash 版                   |
| `check_doc.bat` / `check_doc_fix.bat` | DOC-STATE 漂移检查/修复            |

### 2.3.1 数据/工具链脚本（scripts/ 下，A4 时代新增）

| 脚本 | 用途 |
| ---- | ---- |
| `collect_mfcc.py` | 串口采 MFCC13 → CSV（模版重采入口，600 帧自停） |
| `kmeans_model.py` | K-means 聚类 + 导出 model\_normal.c/h（`--check` 复跑比对） |
| `collect_dmin.py` | DMIN d/m 序列采集 → CSV（**A4-03 阈值标定/实测数据入口**） |
| `analyze_dmin.py` | PC 复算 d\_min + 分维判别比（固件正确性离线闭环） |
| `serial_capture.py` | 通用串口日志抓取（boot 三行 + 各行节拍） |

### 2.4 文档成果

| 文件                                | 内容                                      |
| --------------------------------- | --------------------------------------- |
| `CubeMX_I2S3配置详解.md`              | I2S3 完整配置 + 时钟推导 + 校验清单                 |
| `CubeMX实操步骤.md`                   | 全部外设配置手顺                                |
| `docs/bugs/`                      | 5 个 bug 记录（见下；故障速查表 `bug_shturl` 共 8 条） |
| `docs/troubleshooting/bug_shturl` | 故障速查表                                   |
| `外部资源索引.md`                       | 数据手册/原理图/例程路径                           |
| `scripts/check-doc-drift.sh`      | DOC-STATE 漂移检测（支持 --fix）                |

***

## 三、⚠️ 硬件事故（本次交接的关键）

> ⚠️ **更正（2026-08-16）**：经复核确认，核心板未损坏、可正常使用；真正损坏的是 INMP441 麦克风（SD 高阻无输出）。下方事件记录为超压排查过程的历史参考，**结论以本更正为准**，勿据此换板。

### 3.1 发生了什么

核心板在排查"串口无数据"问题时，经历了多次异常电压：

1. 先是量到核心板 5V 脚只有 3.1V（本应 5V），怀疑 USB 线/口问题
2. 排查过程中接入了面包板电源（量到 3.7V，后掉到 2.4V），已断开
3. **最严重**：让用户用手机 Type-C 快充线给核心板供电，快充协议输出高压，量到 **5V 脚 8V**
4. 8V 超压**打穿了板载 3.3V 稳压芯片**，之后量 3.3V 脚 = 5V（输入直通输出，主芯片被 5V 灌着）

### 3.2 当前板子状态

* **板载稳压芯片（AMS1117-3.3 或同类）已损坏**，3.3V 输出变成直通（5V 进 → 5V 出）

* 3.3V 脚对 GND 电阻 = 23Ω（偏低，主芯片可能没烧，但不确定）

* **结论（已更正）：核心板经复核确认正常，无需换板**；损坏的是 INMP441 麦克风（SD 高阻无输出），更换麦克风即可继续

### 3.3 责任说明

板子超压是我（上一个 AI）指导失误——让用户用快充 Type-C 线试供电导致。软件层面无任何问题。

***

## 四、下一步（换新板后的 10 分钟跑通清单）

### 4.1 买新麦克风

INMP441 数字麦克风（淘宝/拼多多，8\~15 块），和原来同款（I2S 数字输出）。核心板已确认正常，无需再买。

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

***

## 五、踩过的坑（新 AI 必读）

| #  | 坑                                  | 教训                                                                                                                                                                                                                                                                                |
| -- | ---------------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| 1  | **SWD 锁死**（P0）                     | CubeMX 必须配 `SYS→Debug→Serial Wire`，否则 gpio.c 把 PA13/PA14 设成 Analog，首次烧录后 SWD 永久锁死，只能 ISP 救砖。详见 `docs/bugs/BUG-20260815-001`                                                                                                                                                       |
| 2  | **~~JTAG 引脚未释放~~**（已证伪 2026-08-28） | 历史代码在 `HAL_MspInit` 操作 SYSCFG->MEMRMP 释放 JTAG 是 **STM32F1 拷贝残留**，STM32F4 无 SWJ\_CFG 字段、无需释放；PB3/PB5 复位后默认模拟模式，HAL\_GPIO\_Init 配 AF6 后直接是 I2S3 引脚。残留代码已删除，详见 `stm32f4xx_hal_msp.c` 注释                                                                                              |
| 3  | **CubeMX 重新生成丢 include**           | 手动 include 必须放 `USER CODE BEGIN/END` 之间，否则重生成被删                                                                                                                                                                                                                                   |
| 4  | **Makefile 缺源文件**                  | 手加的 .c 文件必须手动加到 Makefile 的 C\_SOURCES                                                                                                                                                                                                                                             |
| 5  | **PLLI2S 灰色不可编辑**                  | 先配 I2S3 外设 + Clock Source 选 PLLI2S，时钟树才解锁                                                                                                                                                                                                                                         |
| 6  | **INMP441 供电不稳**                   | VDD 必须接核心板 3.3V 直接取电，别走面包板电源模块                                                                                                                                                                                                                                                    |
| 7  | **INMP441 SD 输出**（2026-08-28 实测修正） | SD **并非全程推挽**：仅在左槽前 25 SCK 驱动数据，其余时段高阻——高阻段被上拉拉成 0xFFFF/0x7F，正是"全 0xFF"的来源之一（非故障）。4.7kΩ 上拉建议保留（让高阻段电平确定）。读到大量 0xFFFF 先对照 4 字周期模式（坑 10），别只怪 L/R 与 WS                                                                                                                               |
| 8  | **L/R 引脚悬空**                       | INMP441 的 L/R 必须接 GND 或 VDD，悬空不出数据                                                                                                                                                                                                                                                |
| 9  | **ISP 进不了 bootloader**             | 用「RST+BOOT 组合」而非「按 BOOT 上电」，后者时序难把握                                                                                                                                                                                                                                               |
| 10 | **不查手册就写/调外设代码**（P0，BUG-002 根因）    | 外设（I2S/传感器/显示屏等）的**任何**配置和数据提取，编写前必读对应数据手册（`外部资源索引.md` 定位，INMP441=INMP441.pdf，主控=stm32f407参考手册）；调试时"手册公式失败"≠"公式错"，优先怀疑前提（位对齐/数据完整性），用 RAW dump 打印原始值验证。**第一版代码就是按手册写的** **`>>8`，公式没错，是 16 SCK 相位偏移让它"看起来错"——改掉正确公式去猜掩码，反而踩了 12 天坑（详见** **`docs/summary/BUG-002-I2S数据提取问题详解.md`）** |

***

## 六、外部资源路径

数据手册、原理图、例程都在：
`C:\Users\wumu2\OneDrive\桌面\stm32F407ZGT6\`

* 核心板原理图：`芯片stm32F407/1778826025286617/STM32F407ZG-P1核心板原理图.pdf`

* 核心板下载教程：`芯片stm32F407/【6】烧录程序教程/F407ZGT6黑色沉金核心板程序下载.docx`

* INMP441 手册：`INMP441全向麦克风资料/INMP441.pdf`

* 串口助手 SSCOM：`芯片stm32F407/【2】工具软件/SSCOM_v5.13.1/`

完整索引见 `外部资源索引.md`。

***

## 七、当前阻塞点和待办

> **2026-09-01 更新：无阻塞。A1/A2/A3 全线收官 + A4-01/A4-02 收官；A4-03 阶段 2 设计已确认待编码（见 §零点五）。**

### 7.1 活跃风险（2026-09-01 在案，新 Agent 必读）

| # | 风险 | 现状与对策 |
|---|------|-----------|
| 1 | **TRAE 编辑器过期缓冲区回写**（多次实锤：文档被静默回退/重排，含 rules.md 丢 R30 一次） | 编辑/恢复文档前后跑哈希静置验证；好版本及时 `git add` 入索引快照；发现内容回退先 `git restore` 再查编辑器打开的旧标签页（提示保存选"不保存"） |
| 2 | **R27 pre-commit 钩子中文文件名 bug** | `git diff --cached --name-status` 输出八进制转义路径 → 钩子收集不到中文文档新增行。绕过法：三源证据写进代码注释（dist.c 头注释先例）。根治：钩子改用 `git -c core.quotepath=false`（一行，待做） |
| 3 | **R31 时序缺口已补规则** | "看似机械"的小改动也必须先查三源再落盘（2026-09-01 峰值增强先写后研教训，规则已入 rules.md §3） |
| 4 | **模版分布偏移** | model\_normal 采自抖音模拟工厂声；正式部署前建议现场重采（A4-01 §5.2 在案） |
| 5 | **A4-02 遗留观察项** | DMIN/MFCC13 行无帧号对齐（串口丢行致 26% 配对错位）；吹气类异常与背景同分布不可分（异常类型边界交 A4-03 定义） |

### 7.2 待办（按优先级）

1. **A4-03 阶段 3 编码**（唯一在途，见 §零点五）→ 阶段 4 边界实测 → 阶段 5 评审收官
2. A4-04 参数可调（RS485 下发阈值/帧数，依赖 A5）
3. R27 钩子 quotepath 一行修复（随手项）
4. 补齐剩余外设（SHT30、RS485 收发、LED PWM；pin\_config.h 已预留）
5. RS485 回环自测失败排查（st=3，代码 `#if 0` 封存，归 A5-01）
6. A1-01 补录 C3/C4（可选，不阻塞）

### 7.3 关键提交锚点（证据链入口，`git log` 可查）

| 提交 | 内容 |
|------|------|
| `76419e4` | A4-02 距离实时比对收官（12AC 全勾） |
| `cf82345` | A4-02 中期成果（dist 模块 + R27 补研 + R31 入 rules） |
| `40502df` | A4-01 离线模版生成收官 |
| `27961dd` | rules.md v3.0（R30 风险分级固化） |

> 里程碑级证据（采集数据/复算脚本）：`data/mfcc_normal_600.csv`（模版复现）、`data/dmin_*.csv` ×3（A4-03 阈值标定输入）、`scripts/analyze_dmin.py`（固件正确性 PC 闭环）。

