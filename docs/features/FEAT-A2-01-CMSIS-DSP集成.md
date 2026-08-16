# FEAT-A2-01-CMSIS-DSP集成

> **AI 执行规则**：必须按阶段顺序执行，每个阶段开头声明角色。严禁跳跃、越界、自审自判。
> 阶段间停等：每完成一个阶段，停下来，输出结果，等人说"继续"。
> 阶段5通过后：回填父FEAT项目表该行 + 更新 `docs/features/INDEX.md` + `docs/features/维护地图.md`。
> 历史记录只更新状态字段，严禁删除/篡改已完成记录。
> 事实来源：`.sop-agent规格.md` §3（禁止自造数值）。

## 0. 元数据
- **父FEAT**：FEAT-A2-FFT频谱可视化
- **优先级**：P0
- **预估总耗时**：2h
- **当前状态**：🔴未开始（傻瓜式操作单已备好，依赖 A1 通过后开跑）

## 1. 总体目标与硬边界

### 1.1 核心目标（一句话）
> 集成 CMSIS-DSP 库，能编译并调用 `arm_rfft_fast_f32` 跑通一个 1kHz 正弦示例 FFT，且不破坏 A1 采集链路。

### 1.2 地基触碰前置自检
- [x] 否 → 继续（第三方算法库经链接/适配接入，不改其源码）

### 1.3 任务边界
| 类型 | 具体内容 |
|------|----------|
| 范围内（可改） | `firmware/soundDog/Makefile`（源/头路径、链接参数）、`firmware/soundDog/Src/dsp/fft.c`（新增 FFT 封装+示例） |
| 超出范围（禁止碰） | `firmware/Drivers/` HAL 底层库、CMSIS-DSP 库源码 |
| 外部依赖 | A1 音频链路（回归基准）、CMSIS-DSP（STM32CubeCLT 自带或从 CMSIS 包取，路径待确认） |

### 1.4 关键参数与接口（权威：事实包 §3.6 A2-01 + §3.3）
- 链接 `libarm_cortexM4lf_math.a`（Cortex-M4F，硬件 FPU）**或**直编 DSP 源（二选一，方案见 §3.1）。
- 编译参数：`-mfpu=fpv4-sp-d16 -mfloat-abi=hard`（CubeMX 默认已配，需在 Makefile 确认）。
- Makefile 手动加源文件到 C_SOURCES（BUG-002 教训：漏加导致链接失败）。
- 编译：`firmware\build.bat`（mingw32-make + arm-none-eabi-gcc）；烧录：`firmware\build_and_flash.bat`（期望 `wrote ...` / `Verified OK`）。
- 回归基准（A1）：串口 boot 三行 + 吹气 `[n] max=xxxx` 明显变化（§3.4）。

## 2. 执行路线图（5 阶段）

### 🟢 阶段 1：准备（角色：Dev Agent）
- **阶段目标**：定位 CMSIS-DSP 库与集成点，输出影响范围
- [ ] 步骤1：读 `CLAUDE.md`（硬边界）+ `PROJECT_PLAN.md` §5.1（任务规划）+ 本 FEAT §1.4（关键参数）
- [ ] 步骤2：确认 CMSIS-DSP 库来源（STM32CubeCLT 自带 / CMSIS 包），**记录库路径：___**
- [ ] 步骤3：读 `firmware/soundDog/Makefile`，记录 C_SOURCES、头文件路径、链接参数现状（记录：___）
- [ ] 步骤4：输出影响范围（修改：Makefile；新增：`dsp/fft.c`；接口：fft 封装 API；兼容性：A1 链路；风险：链接失败/FPU 未使能）
- **判定标准**：库路径与 Makefile 现状已记录、影响范围清单完整
- **⏸️ 停等：人确认后进入阶段 2**

### 🔵 阶段 2：设计（角色：Dev Agent）
- **阶段目标**：确定集成方案与 FFT 封装接口，产出 §3 傻瓜式操作单
- [ ] 步骤1：方案二选一——A. 链接 `libarm_cortexM4lf_math.a`（需确认库路径）；B. 直编 DSP 源（无外部库依赖）；**记录选择与理由：___**
- [ ] 步骤2：设计 fft 封装 API（如 `fft_init` / `fft_run`）与示例（1kHz 正弦、256 点）
- [ ] 步骤3：产出 §3 操作单（3.0 物品清单 / 3.1 Makefile 集成 / 3.2 示例验证 / 3.3 A1 回归 / 3.4 故障表）
- **判定标准**：方案与 API 签名确定；§3 操作单无缺项
- **⏸️ 停等：人确认后进入阶段 3**

### 🟠 阶段 3：实现（角色：Dev Agent）
- **阶段目标**：按 §3.1~§3.2 执行（改 Makefile → 新增 `dsp/fft.c` → 编译），每改完一个文件对照 AC 自检
- [ ] 步骤1：按 §3.1 修改 Makefile（加库/源、确认 FPU 参数）
- [ ] 步骤2：按 §3.2 新增 `firmware/soundDog/Src/dsp/fft.c`（封装 + 1kHz 正弦示例）
- [ ] 步骤3：运行 `firmware\build.bat`，期望无错误退出、无新增警告
- **判定标准**：编译链接成功（记录输出）；失败 → 查 §3.4 故障表，修复后重跑
- **⏸️ 停等：人确认后进入阶段 4**

===== 切换至 Test Agent =====

### 🟣 阶段 4：测试（角色：Test Agent）
- **阶段目标**：独立验证——只按 §3 操作单与 §4 AC 复核，不看阶段 3 的实现细节
- [ ] 步骤1：烧录 `firmware\build_and_flash.bat`，期望 OpenOCD 输出 `wrote ...` / `Verified OK`
- [ ] 步骤2：串口确认示例 FFT 输出：无 NaN/溢出（AC-01，记录输出样本）
- [ ] 步骤3：A1 回归：boot 三行 + 吹气 max 变化（AC-02，记录读数）
- [ ] 若失败 → 查 §3.4 故障表，退回阶段 3
- **判定标准**：AC-01/02 证据齐全（记录实际值）；无证据的"看起来正常"一律视为未通过
- **⏸️ 停等：人确认后进入阶段 5**

===== 切换至 Review Agent =====

### 🔴 阶段 5：审查（角色：Review Agent）
- **阶段目标**：独立审查——安全检查、代码质量、规范合规、AC 逐条打勾
- 全部通过 → ✅ 回填「维护与调试」+ 父FEAT项目表 + INDEX + 维护地图
- 未通过 → 退回对应阶段并附意见

## 3. 傻瓜式操作单（照做即可）

### 3.0 物品/工具清单（动手前先打勾，缺一不可）
- [ ] 源码工程 `firmware/`（含 Makefile）
- [ ] CMSIS-DSP 库：STM32CubeCLT 自带或 CMSIS 包（路径记录见阶段1步骤2）
- [ ] mingw32-make + arm-none-eabi-gcc（`firmware\build.bat` 已封装）
- [ ] ST-Link（SWD 四线）+ `firmware\build_and_flash.bat`
- [ ] USB-TTL（CH340）+ SSCOM 串口助手（115200，A1 回归用）

### 3.1 Makefile 集成（阶段 3 执行）
1. 打开 `firmware/soundDog/Makefile`，定位 C_SOURCES、头文件路径段与链接段。
2. 方案选择（二选一，**记录选择：___**）：
   - **方案 A**（链接库）：确认 `libarm_cortexM4lf_math.a` 实际路径，Makefile 链接段加 `-L<库目录>` 与 `-larm_cortexM4lf_math`。**记录库路径：___**
   - **方案 B**（直编源）：把 DSP 源文件路径逐条加入 C_SOURCES（BUG-002 教训：漏加 → 链接失败）。**记录加入的文件数：___**
3. 检查编译参数含 `-mfpu=fpv4-sp-d16 -mfloat-abi=hard`（存在：是/否；若无 → 加上，此为硬性参数，规格 §3.6 A2-01）。
4. 运行 `firmware\build.bat`：期望**无错误退出、无新增警告**。**记录编译结果：___**

### 3.2 FFT 示例验证（阶段 3/4 执行）
1. 新增 `firmware/soundDog/Src/dsp/fft.c`：FFT 封装（`arm_rfft_fast_init_f32(&S, 256)` 初始化）+ 1kHz 正弦 256 点示例。**记录初始化调用：___**
2. 编译烧录后，串口打印示例 FFT 输出：期望**无 NaN/Inf、无溢出**，幅度为有限正常值。**记录打印样本：___**（峰值 bin 精确验证属于 A2-02，本阶段只确认函数跑通）
3. 若输出 NaN/Inf → 查 §3.4 故障表。

### 3.3 A1 回归（阶段 4 执行，AC-02 证据）
1. 烧录后 SSCOM（115200）观察，期望依次出现：
   ```
   SoundDog boot OK
   I2S_DRV_Init ret=0
   I2S DMA started
   ```
   **记录实际输出：___**
2. 对麦克风吹气：`[n] max=xxxx` 仍明显变化。**记录读数范围：___ ~ ___**

### 3.4 故障速查表
| 现象 | 可能原因 | 处理 |
|------|---------|------|
| 链接失败：找不到 `-larm_cortexM4lf_math` | 库路径/库名错误 | 核对实际路径与 `libarm_cortexM4lf_math.a` 文件名；或改方案 B 直编源 |
| 链接失败：`arm_*` 未定义引用 | 库未加入链接 / C_SOURCES 漏源文件（BUG-002 教训） | 检查 Makefile 链接段与 C_SOURCES，重跑 `build.bat` |
| 编译报错：找不到 CMSIS-DSP 头文件 | 头文件路径未加 `-I` | Makefile 加头文件路径，**记录路径：___** |
| 示例输出 NaN/Inf | FPU 未使能 / 输入缓冲含非法值 | 确认 `-mfpu=fpv4-sp-d16 -mfloat-abi=hard`；检查示例输入初始化 |
| 烧录失败：OpenOCD 连不上 | ST-Link 接线/驱动 | 查 SWDIO/SWCLK/GND/3.3V；兜底 `firmware\flash_isp.bat`（见 A1-02 §3.1） |
| 串口无输出/乱码 | TX/RX 未交叉 / COM 口错 / 波特率错 | 交叉接、看设备管理器、改为 115200 |

## 4. 验收标准（12 条）

### 功能验收
- [ ] AC-01 `arm_rfft_fast_f32` 示例 FFT 运行：编译链接成功、串口输出无 NaN/Inf/溢出（记录输出样本）
- [ ] AC-02 向后兼容：A1 采集链路回归——boot 三行依次出现 + 吹气 max 明显变化（记录读数）

### 代码质量
- [ ] AC-03 编译/Lint 通过：`firmware\build.bat` 无错误退出
- [ ] AC-04 无新增警告（记录编译警告数：___）
- [ ] AC-05 文档已更新：本 FEAT「维护与调试」回填 + 父 FEAT 项目表该行状态更新

### 安全红线
- [ ] AC-06 无硬编码密钥：**N/A**（嵌入式工程无密钥/凭据，代码内无任何账号/密钥字符串）
- [ ] AC-07 错误处理完善：链接失败/库缺失时，报错可直接定位到 Makefile 相应行（记录一次失败定位过程或说明）
- [ ] AC-08 用户输入已校验：**N/A**（本 FEAT 无用户输入接口；示例信号为代码内合成 1kHz 正弦）

### 测试与边界
- [ ] AC-09 新增测试自动运行：1kHz 正弦示例随 `build.bat` 产物可重复运行（对应 AC-01）
- [ ] AC-10 全部回归测试通过：A1 链路回归（AC-02 证据）+ 示例 FFT 输出正确
- [ ] AC-11 测试覆盖率 ≥ 80%：**N/A**（嵌入式示例验证，无覆盖率工具链；以 AC-01 实测输出代替）
- [ ] AC-12 按 FEAT 模板执行：5 阶段停等、日志追加不删除、状态回填均按 `.sop-agent规格.md` 执行

## 5. 维护与调试（阶段5通过后回填）

### 5.1 影响文件
| 文件 | 改动类型 | 说明 |
|------|---------|------|
| `firmware/soundDog/Makefile` | 修改 | 加 DSP 库/源、确认 FPU 参数 |
| `firmware/soundDog/Src/dsp/fft.c` | 新增 | FFT 封装 + 1kHz 正弦示例 |

### 5.2 调试要点
- 链接错误 → 检查 Makefile 路径与库名（§3.4）
- 数值异常（NaN） → 检查 FPU 使能与 FFT 初始化

### 5.3 测试入口
- `firmware\build.bat` 编译 + 烧录后 SSCOM 观察示例 FFT 输出（对应 AC-01）；A1 回归同 §3.3（对应 AC-02）

## 6. 执行日志

| 时间 | 阶段 | 角色 | 摘要 | 遇阻 |
|------|------|------|------|------|
| | | | | |
