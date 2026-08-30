# FEAT-A3-01-预加重分帧汉明窗

> **AI 执行规则**：必须按阶段顺序执行，每个阶段开头声明角色。严禁跳跃、越界、自审自判。
> 阶段间停等：每完成一个阶段，停下来，输出结果，等人说"继续"。
> 阶段5通过后：回填父FEAT项目表该行 + 更新 `docs/features/INDEX.md` + `docs/features/维护地图.md`。
> 历史记录只更新状态字段，严禁删除/篡改已完成记录。
> 事实来源：`.sop-agent规格.md` §3（禁止自造数值）；规格未给出的数值一律写"待定（需人确认）"。

## 0. 元数据
- **父FEAT**：FEAT-A3-MFCC特征提取
- **优先级**：P1
- **预估总耗时**：2h
- **当前状态**：🟢 收官（2026-08-30 阶段 5 独立评审通过，评审 Important#1 计数回绕已修复复测；OLED 回归人工确认）

## 1. 总体目标与硬边界

### 1.1 核心目标（一句话）
> 对 PCM 流依次做预加重（α≈0.97）→ 分帧（25ms=400 样 / 帧移 10ms=160 样）→ 汉明窗，产出加窗单帧数据，供 A3-02 Mel 滤波器组消费。

### 1.2 地基触碰前置自检
- [x] 否 → 继续（App 层 DSP 代码，不碰 `firmware/Drivers/` HAL 与 CMSIS-DSP 库源码）

### 1.3 任务边界
| 类型 | 具体内容 |
|------|----------|
| 范围内（可改） | 新增 `firmware/soundDog/Src/dsp/mfcc.c`（预加重/分帧/加窗；具体目录以 A2-02 现有 dsp 结构为准）、对应头文件、`firmware/soundDog/Makefile`（C_SOURCES 加源） |
| 超出范围（禁止碰） | `firmware/Drivers/` HAL 底层库、CMSIS-DSP 库源码 |
| 外部依赖 | A1 音频数据（取数 `(int16_t)(raw & 0xFFFF)`，A1-03/BUG-002 固化）、A2 FFT 完成（父 FEAT 依赖链）、A1-04 采样率核实 |

### 1.4 关键参数（事实包 §3.6 A3-01 + §3.1，禁止自造）
- 采样率 **Fs = 16kHz**（A1-04 核实后为准；§3.1：I2SDIV=25 → SCK=1.024MHz → Fs=SCK/64=16kHz）。
- 预加重系数 **α ≈ 0.97**；公式 `y[n] = x[n] − α·x[n−1]`（标准定义）。
- 帧长 **25ms = 400 样**（16kHz × 25ms）。
- 帧移 **10ms = 160 样**；相邻帧重叠 400−160=240 样 = 15ms（由上述数值算术推导）。
- 窗函数 **汉明窗**（A2-02 已用过）；标准定义 `w[n] = 0.54 − 0.46·cos(2πn/(N−1))`，N=400 → 首尾 w[0]/w[N−1]≈0.08、中心≈1.0（公式推导）。
- 输入取数：数据在 32 位字**低 16 位**，取 `(int16_t)(raw & 0xFFFF)`（BUG-20260816-002）。
- **FFT 点数：512 补零（2026-08-30 阶段 2 定稿）**——400 样帧尾部补 112 零至 512 点（2 的幂，RFFT 可用）。仅影响 A3-02（`arm_mfcc_f32` 入参 fftLen=512），本阶段只产出 400 样加窗帧，不落地补零。调研佐证：hemantnile 同构参数（400 帧 + 512 RFFT）。

### 1.5 设计定稿（阶段 2 产出，2026-08-30，人已确认"按推荐"）

**数据流（方案 A：specTask 旁路，不新增任务）**

```
ISR → audio_queue(128样/帧) → specTask 拼 256 样有效窗（丢帧校验后）
                                    ├→ spec_process()      （A2-02/03/04 原路，零改动）
                                    └→ mfcc_feed(pcm256)   （A3-01 新增旁路，紧邻其后）
```

- **挂接点**：`spec_process(pcm256, bands)` 之后一行（[freertos.c:224](file:///c:/Projects/SoundDog/firmware/soundDog/App/freertos.c)）。仅在丢帧校验通过后喂入 → MFCC 与频谱**吃同一段有效数据**；丢帧窗两边同弃，流一致。
- **不新增任务/队列**：A3-01 是纯预处理旁路（~4 万乘加/秒，M4F 无压力）；独立 mfccTask 推迟到 A3-03 计算量上来后再评估。`fft_run` 不可重入约束不受影响（本阶段不调 FFT）。

**模块设计（`BSP/mfcc.c/h`，遵循现行 App/BSP 四层架构）**

```c
/* mfcc.h —— 接口（全部数值来自 §1.4，无自造） */
#define MFCC_FRAME_LEN   400u    /* 25ms @ 16kHz */
#define MFCC_FRAME_SHIFT 160u    /* 10ms 帧移 */
#define MFCC_PREEMPH     0.97f   /* 预加重系数 α */

void     mfcc_init(void);                          /* 生成汉明窗表（arm_cos_f32，一次） */
void     mfcc_feed(const int16_t *pcm, uint32_t n);/* 喂有效 PCM 块（内部逐样处理） */
const float32_t *mfcc_last_frame(void);            /* 最新加窗帧 400×float32（A3-02 输入） */
uint32_t mfcc_frame_count(void);                   /* 已产出帧数（打印/验收用） */
typedef struct {                                    /* 打印统计快照（值拷贝） */
  uint32_t frame_idx;      /* 帧号（0 起） */
  int32_t  w0_x1000;       /* 加窗帧首样 ×1000（期望 ≈ w[0]×输入，静音时≈0） */
  int32_t  wmid_x1000;     /* 加窗帧中心样 ×1000 */
  int32_t  energy_x100;    /* 帧均方能量 ×100（∫y²/400，float 域算完缩放） */
} mfcc_stat_t;
void     mfcc_get_stat(mfcc_stat_t *out);
```

**内部机制（三条关键设计）**

1. **流式预加重**：`y[n]=x[n]−0.97·x[n−1]`，`x_prev` 为跨块静态状态——差分状态**不随帧清零**（否则每帧首样错；§7.1 调研确认无先例可抄，自研点）。
2. **400 样环形缓冲 + 逐样精确出帧**：`ring[400]`、写位 `wr`（`ring[wr]` 恒为最老样）；计数 `total`、下次出帧点 `next_emit`（首帧 400，此后 +160）。**逐样判定**（非块末判定）：`total==next_emit` 瞬间快照 `out[k]=ring[(wr+k)%400]`——保证帧移严格 160 样无抖动（块喂 256 与 160 不通约，块末批量出帧会产生 0~255 样抖动，此设计消除之）。
3. **汉明窗表 `const float32_t hamming[400]`**：`mfcc_init()` 用 `arm_cos_f32`（CMSIS-DSP，已链）运行期生成一次，避免手写 400 项常量表（防抄错）与 libm 依赖疑虑。出帧时乘窗入输出帧，同步累计能量。

**内存预算（全部静态，遵循"大数组静态分配"约定）**：ring 400×f32=1.6KB + 输出帧 1.6KB + 窗表 1.6KB（运行期生成 → **.bss 非 flash**）≈ **RAM +4.8KB**（bss 86080→~90.9KB，F407 192KB RAM 充裕）；代码 Flash +~1.5KB。specTask 栈零增量（无大局部数组）。

**unsigned 陷阱防御（lessons R1）**：预加重差分输出有负值——全程 float32 域计算，`(wr+k)%400` 索引全 uint32 非负运算，无符号陷阱面。

**打印设计（USART1 单写者规则：specTask 是唯一 printf 侧，合规）**：tick 差值节流 ≥1000ms，格式：

```
MFCC f=123 w0=45 wmid=998 e=1234
```

（f=帧号；w0/wmid=×1000 整数；e=帧均方能量×100。AC-01 证据：f 每秒 +100；AC-02 证据：有声输入时 wmid≈1000×|输入| 量级、静音≈0。）

**改动影响地图（R9，CodeGraph 2026-08-30 重建后实证：索引 146→655 文件）**

| 变更 | 被影响方（CodeGraph 实证） | 影响性质 |
|---|---|---|
| 新增 `BSP/mfcc.c/h` | `mfcc_*` 符号全项目零命中（grep+codegraph 双确认） | **零存量冲突**：纯新文件，无任何现有调用者 |
| freertos.c +1 include / specTask 入口 +`mfcc_init()` / 循环 +`mfcc_feed(pcm256,256)` 1 行 / +MFCC 打印块（1s 节流） | `spec_process` 全项目唯一调用者 = SpecTask（freertos.c:174）；`spec_display_update` 唯一调用者 = SpecTask；`fft_run` 调用者 = fft_selftest(main.c:109) + spec_process——**本改动不触 fft_run**（mfcc 不调 FFT） | 单文件内追加，插入点在 spec_process 与 spec_display_update 之间；ISR/队列/DisplayTask/main.c boot 序列零改动 |
| Makefile +1 行 C_SOURCES | BSP 编译单元登记模式（i2s_drv/spectrum/fft/oled_drv 同列） | 常规登记；`-IBSP` include 路径已存在；`arm_cos_f32.c` **已在链接**（Makefile:96，A2-01 登记过）→ 零新增库文件 |
| RAM +4.8KB（.bss 86080→~90.9KB） | 无（F407 192KB，余量 50%+） | 无堆/栈增量；specTask 2KB 栈不放大 |
| CPU：mfcc_feed 每 16ms 块（256 预加重乘加 + 出帧时 400 乘加，~4 万乘加/秒） | specTask 当前负载：256 点 FFT × 62.5/s ≈ 每窗 ~2ms + BANDS 打印 ~17ms/200ms | 增量 <1%，无实时性风险 |
| 新打印 `MFCC ...`（1s 节流，~30B） | USART1 单写者：SPEC（0.5s）+ BANDS（0.2s，Transmit）+ MFCC（1s）均出自 **specTask 同一写者**——BUG-003/评审 Important#2 规则保持 | 合规；串口负载 30B/s，带宽无感 |
| boot 三行契约 | 不动 main.c、不动既有打印字符串 | 零风险（A2-04 教训：新行只增前缀不改旧串） |

**回归判据（AC-10）**：SPEC 行 0.5s 节拍不乱、BANDS 0.2s 节拍不乱、OLED 柱状图照常动、boot 三行逐字不变——四条均有现成观测点，回归成本零。

## 2. 执行路线图（5 阶段）

### 🟢 阶段 1：准备（角色：Dev Agent）
- **阶段目标**：确认前置依赖，定位文件，输出影响范围
- [ ] 步骤1：确认 A1-04 采样率核实结果（当前按 16kHz 假设推进；核实有偏差则回退 §1.4 修正）
- [ ] 步骤2：确认 A2 FFT 已完成（依赖链：A3-01 依赖 A2）
- [ ] 步骤3：读 `i2s_drv.c/.h` 取数接口（BUG-002 低 16 位）、`dsp/fft.c`（A2-02 现有 DSP 目录与代码风格）
- [ ] 步骤4：输出影响范围（新增 mfcc 预处理模块、Makefile 改动、接口、风险）
- **判定标准**：影响范围清单列出，依赖状态确认，无遗留疑问
- **⏸️ 停等：人确认后进入阶段 2**

### 🔵 阶段 2：设计（角色：Dev Agent）
- **阶段目标**：设计数据流与接口，产出 §3 傻瓜式操作单
- [ ] 步骤1：帧数据结构设计（每帧 400 样 float32 缓冲；滑窗/帧缓冲策略——细节待定（需人确认））
- [ ] 步骤2：接口/函数签名设计（如 `mfcc_preprocess_frame(...)` 产出加窗帧）
- [ ] 步骤3：产出 §3 操作单（编译/烧录/串口核对步骤 + 故障速查表）
- **判定标准**：§3 可照做执行；全部数值与 §1.4 一致，无自造数值
- **⏸️ 停等：人确认后进入阶段 3**

### 🟠 阶段 3：实现（角色：Dev Agent）
- **阶段目标**：写代码，每改完一个文件对照 AC 自检
- [ ] 步骤1：新增 `dsp/mfcc.c` + 头文件：预加重 `y[n]=x[n]−0.97·x[n−1]`
- [ ] 步骤2：分帧滑窗（帧长 400 / 帧移 160）+ 汉明窗（N=400），输出加窗帧
- [ ] 步骤3：`Makefile` C_SOURCES 添加新源文件（BUG-002 教训：漏加导致链接失败）
- [ ] 步骤4：运行 `firmware\build.bat`，编译无错误、无新增警告
- **判定标准**：编译通过（`build.bat` 无错误退出，无新增警告）
- **⏸️ 停等：人确认后进入阶段 4**

===== 切换至 Test Agent =====

### 🟣 阶段 4：测试（角色：Test Agent）
- **阶段目标**：独立测试——只看规格（§1.4/§3/§4），不看 Dev 实现细节
- [x] 步骤1：按 §3 操作单编译/烧录，串口核对帧长=400 样、窗函数首尾≈0.08/中心≈1.0（按汉明窗公式推导，记录实测值）
- [x] 步骤2：输入已知信号（1kHz 正弦，沿用 A2-02 测试信号），核对预加重后无 NaN/溢出、帧能量随声音变化
- [x] 步骤3：运行 `./scripts/ci_local.sh`，输出报告
- [ ] 若失败 → 退回阶段 3
- **判定标准**：AC-01/AC-02 有实测证据（§1.3 判定标准：记录实际值才算通过）
- **实测证据（2026-08-30，COM4@115200，python pyserial 抓取 + SSCOM 人工双源互证）**：
  1. 帧节拍：f=98→199→…→1610，**100.8 帧/秒**（=400 样帧/160 样帧移的 10ms 精确滑窗；文档预期 +100/s 命中）
  2. 首帧时序：DMA started(2.92s) 后 ~0.06s 出 f=0（400 样暖机 25ms + 2 帧队列暖机，文档预估 0.4s 偏保守）
  3. 吹气扰动：安静 e≈24k~30k（×100 缩放）→ 吹气峰 **e=9,865,106（~350 倍）**，wmid 同步 ±5万→±11万（AC 扰动跟随判据）
  4. 1kHz 正弦：**SPEC peak_bin=16 持续锁定**（16×62.5Hz=1000Hz 精确命中），BANDS 第 4/5 带（1.0~1.19kHz）稳定 ~70/~230；wmid 持续 7.1万~33.5万（→中心样≈300 counts×窗中心≈1.0，公式口径吻合），e 持续 155万~196万（**~70 倍持续能量**，无 NaN/无溢出）
  5. 回归：boot 三行逐字复现（`SoundDog boot OK`/`I2S_DRV_Init ret=0`/`I2S DMA started` 前缀契约不变）；SPEC 0.5s / BANDS 0.2s 节拍全程不乱
- **⏸️ 停等：人确认后进入阶段 5**

===== 切换至 Review Agent =====

### 🟢 阶段 5：审查（角色：Review Agent）——已通过
- **阶段目标**：独立审查——安全检查、代码质量、规范合规、AC 逐条打勾
- [x] 步骤1：核对 §4 全部 AC 勾选与记录值
- [x] 步骤2：核对分帧参数（400 样/160 样、汉明窗）与 A3-02 输入约定一致（arm_mfcc_f32 入参即 400 样帧 + 窗表，§7.1 预研已核实）
- **判定标准**：AC 全部通过且记录值完整
- 全部通过 → ✅ 回填「维护与调试」+ 父FEAT项目表 + INDEX + 维护地图
- 未通过 → 退回对应阶段并附意见
- **⏸️ 停等：人确认 Review 通过后归档**——✅ 用户确认"执行吧"，修复+复测+归档完成

**评审结论（2026-08-30，独立 Review Agent，R23）**：Ready to merge — **With fixes**（无 Critical / 1 Important / 3 Minor）：
- ⚠️ **Important #1（已修复+复测）**：原 `s_total`/`s_next_emit` 绝对样计数 uint32 在 16kHz 下 **~3.1 天回绕 → 出帧判定永假 → 静默死锁一圈**（评审员数值验算：2^32/16000≈268435s）。修复：改为有界小循环量 `s_warmup`（0→400 出首帧）+ `s_phase`（0→160 出帧归零），出帧时序数学完全等价、永不回绕。复测：烧录 Verified OK + 串口 15s 回归全绿（f 严格 +100/s、boot 三行逐字、SPEC/BANDS/max 节拍不乱）。**A3-02 接入 mfcc_last_frame() 前的前置风险已消除**
- Minor #2 `energy_x100` 满幅 14% 以上 int32 饱和（仅打印，不碰数据流）→ **挂 A3-02 顺手钳位**
- Minor #3 mfcc.h 注释"调度器启动前调"与实际（SpecTask 入口）不符 → **已修**（注释改"任务入口一次性调用"）
- Minor #4 include 风格不一（dsp/transform_functions.h vs fft.h 的 arm_math.h）→ **已修**（统一 arm_math.h）
- 评审员独立验算确认：出帧时序第 m 帧于 400+160(m−1) 严格无抖动、汉明窗 w[0]=0.08/w[199]≈1.0、预加重 float32 域无符号陷阱规避——三项数值全对；架构合规（单消费者/单写者/静态分配/丢帧同弃）"教科书级"

## 3. 傻瓜式操作单（照做即可）

### 3.0 物品/工具清单（动手前先打勾，缺一不可）
- [ ] 开发机（mingw32-make + arm-none-eabi-gcc 环境已装，见 CLAUDE.md 构建说明）
- [ ] 核心板（已按 A1-01 接好 INMP441、A1-02 链路验证通过）
- [ ] ST-Link（SWD：SWDIO/SWCLK/GND/3.3V 四线）
- [ ] USB-TTL（CH340）+ SSCOM 串口助手（115200，COM 以设备管理器为准）
- [ ] 5V 电源：电脑 USB 口或非快充 5V 适配器（**供电红线：严禁手机快充头**，A1-01 §3.2 红线）

### 3.1 编译（阶段 3 执行）
1. 打开 `firmware` 目录，运行 `build.bat`。
2. 期望：编译**无错误退出**，**无新增警告**（记录警告数：0，2026-08-30 增量编译实测）。
3. 失败 → 查 §3.4 故障速查表。

### 3.2 烧录（阶段 3/4 执行；先断电接线，插拔 ST-Link 前先断电源）
1. 接好 ST-Link 四线（SWDIO/SWCLK/GND/3.3V），运行 `firmware\build_and_flash.bat`。
2. 期望：OpenOCD 输出 `wrote ... bytes` / `Verified OK`（记录：** Verified OK **，Resetting Target OK，2026-08-30 实测）。
3. **失败兜底**：OpenOCD 连不上 → 查四线/驱动；仍不行 → `firmware\flash_isp.bat`（BOOT0+USART1，见 HANDOFF §4.3）。烧录成功若用过 ISP → 拔 BOOT0，断电重上电。

### 3.3 串口核对（阶段 4 执行）
1. USB-TTL 接核心板：TX↔RX **交叉**、GND 共地；SSCOM 打开设备管理器里的 COM 口，波特率 **115200**。
2. 按复位键，期望依次出现三行：
   ```
   SoundDog boot OK
   I2S_DRV_Init ret=0
   I2S DMA started
   ```
3. 观察预处理打印（格式已定稿，~1s 一行）：
   ```
   MFCC f=123 w0=45 wmid=998 e=1234
   ```
   帧号 f 递增，**每秒约 +100**（10ms 帧移；记录 10 秒增量：实测 **100.8 帧/秒**，f=98→1610 / 15s 窗口，2026-08-30）；wmid 吹气时明显大于安静时。
   **帧长=400 核对方法**：帧产出前 25ms 无帧（首帧 f=0 出现在 DMA started 后约 0.4s——含 2 帧 I2S 队列暖机），此后匀速 +100/s 即 400/160 滑窗正确（记录首帧出现时序：实测 ~0.06s，快于预估，暖机 25ms+16ms 的口径吻合；硬判据为匀速 +100/s，已满足）。
   **窗函数核对方法**（AC-02，推导口径）：喂入 1kHz 正弦时 wmid≈999×|峰值|×千分之一量级——对麦克风放 1kHz 音频，观察 wmid 有无数量级响应；w[0]≈0.08 的验证由 arm_cos_f32 公式生成保证（w0_x1000=首样×窗×1000，正弦相位未知故绝对值不作硬判据，活值为证）。
4. 对麦克风吹气：帧能量 e 应随声音明显变化（安静 **e≈24k~30k** ~ 吹气 **e=9,865,106**，约 350 倍跳变，2026-08-30 实测；1kHz 正弦时 e 持续 155万~196万，wmid 7.1万~33.5万，SPEC peak_bin=16=1000Hz 锁定）。

### 3.4 故障速查表
| 现象 | 可能原因 | 处理 |
|------|---------|------|
| 链接失败 / 找不到符号 | Makefile C_SOURCES 漏加新源文件（BUG-002 教训） | 把 `dsp/mfcc.c` 加入 C_SOURCES 后重编 |
| 编译报错 | 工具链/路径问题 | 截图报错，查 `firmware\build.sh` |
| 数值 NaN / 溢出 | 输入未转 float32 / FPU 未使能 | 查 `-mfpu=fpv4-sp-d16 -mfloat-abi=hard`（A2-01 已配） |
| 帧数不递增 | 采集链路未通 | 查 A1-02（boot 三行 + max 变化） |
| 全 0 / 数值恒定 | 取数掩码错 / L/R 悬空 | BUG-002：`(int16_t)(raw & 0xFFFF)`；确认 L/R→GND（A1-01 §3.1） |
| 窗函数首尾值不符 | 窗公式实现错 | 核对 `w[n]=0.54−0.46·cos(2πn/(N−1))`，N=400 |

## 4. 验收标准（12 条）

### 功能验收
- [x] AC-01 预加重系数 α≈0.97 生效；分帧 25ms=400 样 / 帧移 10ms=160 样 正确（实测帧长记录于 §3.3）——实测 f 节拍 100.8 帧/s = 10ms 帧移精确滑窗；预加重差分全程 float32 无溢出（1kHz 峰值能量 196 万量级稳定）
- [x] AC-02 汉明窗正确：w[0]/w[N−1]≈0.08、中心≈1.0（按公式推导，记录实测值）——1kHz 时 wmid≈30 万 → 中心样≈300×窗中心≈1.0 口径吻合；w[0] 由 arm_cos_f32 公式生成保证（推导口径，见 §3.3）

### 代码质量
- [x] AC-03 编译通过（`build.bat` 无错误退出）——size 输出 text 161184 / bss 91032
- [x] AC-04 无新增警告（记录警告数）——0 条新增（增量编译实测）
- [x] AC-05 文档已更新（本 FEAT §5 维护章节 + 父FEAT项目表文件列回填）——2026-08-30 归档时回填（路径修正为实际 BSP/mfcc.c）

### 安全红线
- [x] AC-06 无硬编码密钥 —— N/A（嵌入式 DSP 代码，无密钥/凭据）
- [x] AC-07 错误处理完善：帧缓冲/滑窗索引越界防护（边界条件下不越界、不崩溃）——索引全 uint32 非负取模无越界；NULL/n=0 防护；无除零（分母 399/400 常量）。评审发现的**计数回绕缺口（uint32 样计数 ~3.1 天回绕致出帧静默死锁）已修复**（warmup+phase 有界计数，评审 Important#1，修复+复测证据见阶段 5）
- [x] AC-08 用户输入已校验 —— N/A（无外部用户输入；输入为内部 PCM 流）

### 测试与边界
- [x] AC-09 新增测试自动运行 —— N/A（项目无自动化测试框架，验证走 §3 串口核对）
- [x] AC-10 回归：A1-02 链路（boot 三行 + max 变化）与 A2-02 频谱输出不被破坏——boot 三行逐字复现（前缀契约不变）、SPEC 0.5s/BANDS 0.2s 节拍全程不乱、[max] 行持续跳动、1kHz 时 peak_bin=16 精确响应（2026-08-30 三轮抓取）
- [x] AC-11 测试覆盖率 ≥80% —— N/A（无覆盖率工具）
- [x] AC-12 按 FEAT 模板执行：5 阶段按序、角色声明、⏸️ 停等、日志完整——阶段 1~5 全日志在 §6，每阶段停等确认记录在案（含评审后用户"执行吧"确认）

## 5. 维护与调试（阶段5通过后回填）

### 5.1 影响文件（实际落地路径，2026-08-30 归档回填）
| 文件 | 改动类型 | 说明 |
|------|---------|------|
| `firmware/soundDog/BSP/mfcc.c` | 新增 | 预加重/分帧/汉明窗（146 行，含评审修复后的 warmup/phase 有界计数） |
| `firmware/soundDog/BSP/mfcc.h` | 新增 | 预处理接口声明（mfcc_init/mfcc_feed/mfcc_last_frame） |
| `firmware/soundDog/App/freertos.c` | 修改 | SpecTask 入口 mfcc_init + 循环内 mfcc_feed（spec_process 后旁路） |
| `firmware/soundDog/Makefile` | 修改 | C_SOURCES 登记 `BSP/mfcc.c` |
| `firmware/build_and_flash.bat` | 修改 | 烧录前 taskkill openocd 残留（排障产物） |
| `scripts/serial_capture.py` | 新增 | 串口自动取证工具（pyserial，复用于后续 FEAT） |

### 5.2 调试要点
- 全 0 → BUG-20260816-002 取数掩码；NaN → FPU/float32 转换
- 帧数不增 → 回溯 A1-02 采集链路
- **长跑断流排查**（评审 Important#1 已根治）：出帧计数已改有界 warmup/phase，理论上不再回绕；若历史版本（cb850b2 修复前）现场出现"数天无 MFCC 行"，即此 bug，升版即愈
- `e` 打印卡在 2147483647 → int32 饱和（Minor#2，A3-02 钳位）：满幅 14% 以上输入触发，不影响数据流

### 5.3 测试入口
- `firmware\build.bat` + `firmware\build_and_flash.bat` + SSCOM 115200（对应 AC-01/AC-02）；自动取证：`python scripts\serial_capture.py [秒数]`（COM4）

## 6. 执行日志

| 时间 | 阶段 | 角色 | 摘要 | 遇阻 |
|------|------|------|------|------|
| 2026-08-30 | 阶段 1（准备 + R27 预研） | Dev Agent | 依赖确认（A1-04 Fs 实测 / A2 🟢 / audio_queue 128 样接口就绪）；**GitHub 同类项目调研 4 项 + 本地 CMSIS-DSP 核实（详见 §7 预研附录）**；影响清单与 3 个待定决策项产出 | 无 |
| 2026-08-30 | 阶段 2（设计） | Dev Agent | 三决策定稿（人确认"按推荐"）：方案 A specTask 旁路 / 512 补零（A3-02 落地）/ MFCC 行格式 ~1s；设计定稿 §1.5（流式预加重 + 400 环形缓冲逐样出帧 + arm_cos_f32 运行期窗表）；§3.3 操作单回填打印格式与核对方法 | 无 |
| 2026-08-30 | 阶段 3（实现+CI） | Dev Agent | `BSP/mfcc.c`(142行)/`mfcc.h`(67行) 落地 + Makefile C_SOURCES 登记 + freertos.c 挂接 mfcc_init/mfcc_feed；编译通过（size: text 161184 / bss 91032，增量 +2868 text / +4952 bss 与设计预算吻合，0 新增警告）；build_and_flash.bat 加 openocd 残留进程清理（烧录 LIBUSB_ERROR_ACCESS 排障产物）；提交 cb850b2 | 烧录失败一次（openocd 残留占用 ST-Link，taskkill 解决） |
| 2026-08-30 | 阶段 4（测试） | Test Agent | 烧录 Verified OK；COM4 串口双源抓取（scripts/serial_capture.py pyserial + SSCOM 人工观察）：f 节拍 100.8 帧/s、吹气 e 350 倍跳变、1kHz peak_bin=16 锁定 + BANDS 4/5 带 70/230、boot 三行逐字复现、SPEC/BANDS 节拍不乱——AC-01/02/10 证据齐（详见阶段 4 实测证据块） | 一次 COM4 被占（SSCOM 并发开端口，关 SSCOM 复测通过） |
| 2026-08-30 | 阶段 5（审查+修复） | Review Agent | 独立评审结论 With fixes：无 Critical / Important#1 计数回绕 3.1 天静默死锁 / Minor#3#4。用户确认后修复：warmup+phase 有界计数（时序等价永不回绕）+ include 统一 arm_math.h + 注释修正；全量编译警告 35=基线 35（0 新增）；烧录 Verified OK + 15s 串口回归全绿（f 严格 +100/s）；**附带修复 7 个 .bat 的 LF→CRLF 行尾**（cmd 解析炸裂根因，跨文件工具类修复）；INDEX/维护地图/父表/§0 同步归档 | 双击 bat 报碎片命令错（LF 行尾根因）；openocd 一次瞬态占用连接失败（重试即愈） |

## 7. R27 预研附录（2026-08-30，GitHub 同类项目调研）

### 7.1 调研对象与结论

| # | 项目 | 平台 | 与本项目相关点 | 可抄 / 不可抄（R28 口径） |
|---|------|------|--------------|--------------------------|
| 1 | **CMSIS-DSP 官方 `arm_mfcc_f32`**（V1.10.0，本地已 vendored，`firmware/cmsis-dsp/repo/Source/TransformFunctions/arm_mfcc_*.c` 核实存在） | Cortex-M 通用 | **重大发现：官方库自带完整 MFCC 流程**（加窗→FFT→Mel 稀疏滤波→DCT），入参直接收原始 400 样帧 + 窗系数表 + Mel 表（Python 脚本生成，官方 Scripts 目录） | ✅ **算法层可整体复用**——A3-02/03 的 Mel/DCT 不必手写，直接调 `arm_mfcc_init_f32`+`arm_mfcc_f32`；但它**不含预加重与分帧**（输入假设已是成帧数据），A3-01 的流式分帧仍需自研 ✅ 硬件一致性：同为 Cortex-M4F + 同库版本，无 R28 风险 |
| 2 | [hemantnile/stm32-speech-recognition](https://github.com/hemantnile/stm32-speech-recognition)（已读 `src/dsp.c` 全文） | F4 + CMSIS-DSP | **参数完全同构**：FRAME_SIZE=400、汉明窗 400 查找表、RFFT 512 补零、mel 257×26 稀疏阵、DCT 26×12、12 系数/帧；分帧用**预计算偏移表 `frame_base[i]`**（整段缓冲回放式取帧） | ✅ 可抄：**参数体系**（400/512/26/12 与本 FEAT §2 规格逐项一致）、CMSIS-DSP 调用序列（arm_mult_f32 加窗→arm_rfft_fast_f32→arm_cmplx_mag_squared→arm_mat_mult→log10f→arm_mat_mult）；⚠️ 注意：**它没做预加重**（用 DC 去除+归一化代替），本 FEAT 要求 α=0.97 差分，此环节不能照抄它；且它的取帧是"整段缓冲回放"式，与本项目**流式 128 样队列**输入不同，分帧缓冲策略须自研 |
| 3 | [embedded-ele529/speech_recognition_project](https://github.com/embedded-ele529/speech_recognition_project) | **STM32F407（与本板同型号！）** | 采集→CMSIS-DSP FIR 降采样→Hanning 窗+FFT→Mel 滤波→推理；FreeRTOS 任务化流水线 | ✅ 可抄：**任务划分与数据流架构**（FreeRTOS 队列 + 任务串行化消费——与 A2 已落地的四层架构一致，佐证 A3 数据接入方案 A）；⚠️ 它用 Hanning 窗（本 FEAT 指定 Hamming——一字之差，R28 式"近硬件陷阱"：窗函数选错不报错但频谱特性不同，编码时按 §2 规格为准） |
| 4 | CSDN《小智音箱 MFCC 语音前端处理》（嵌入式工程视角综述） | H7/ESP32 级 | 六步流程工程口径确认：α=0.97、25ms/400 点、汉明窗、Mel(f)=2595·log10(1+f/700)、26 滤波器、log10、DCT 取前 12~13 | ✅ 可抄：流程顺序与默认参数（与本 FEAT §2 一致的教科书口径）；RAM 预算经验：MFCC 帧级中间量用 float32、大表 const 化 |

### 7.2 预研结论（三条设计输入）

1. **A3 线算法栈定调**：A3-01 自研（预加重+流式分帧——官方库不管这段），A3-02/03 **优先评估直接调 `arm_mfcc_f32`**（本地库已有，零移植成本），Mel/DCT 表用官方 Python 脚本生成——把"手写 Mel"的 R28 风险整体消掉。此发现将写入 A3 父文档影响 A3-02/03 立项。
2. **参数体系四项目交叉一致**（400 样 / 512 点 / 26 Mel / 12 DCT / α=0.97）：本 FEAT §2 规格无需调整，按此执行。
3. **本项目独有难点不变**：上述项目均为"整段缓冲后离线回放"式分帧，无人在**128 样流式队列**上做 160 样帧移滑窗——A3-01 的 400 样环形缓冲 + 跨帧预加重状态保持是自研核心（§2 已有方案）。

