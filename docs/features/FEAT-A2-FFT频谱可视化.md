# FEAT-A2-FFT频谱可视化（父FEAT · 统筹规划）

> **AI 执行规则**：父FEAT 只做统筹规划，禁止编写实现代码；实现一律拆成子FEAT。
> 创建/修改项目表后必须停等，输出结果等人类确认方向后才允许创建子FEAT。
> 禁止自审自判完成：父FEAT 置 🟢 仅当项目表全部子FEAT 🟢 且每个都经过 Review。
> 历史记录只更新状态字段，严禁删除/篡改已完成记录。
> 事实来源：`.sop-agent规格.md` §3（禁止自造数值）。

## 0. 元数据
- **角色**：Planner（统筹规划）
- **优先级**：P1
- **预估总耗时**：8h（4 子FEAT 合计约 7h + 集成余量）
- **当前状态**：✅已完成（2026-08-30 四子FEAT 全 🟢 收官：CMSIS-DSP 集成 + FFT 幅度谱（实麦 1kHz→bin=16 零漂移）+ OLED 柱状图（F1 实测电气参数点亮）+ 串口 BANDS 行；期间闭环 8 个 bug，沉淀 R28 规则与 oocd_probe 工具）

## 1. 总体目标与方向

### 1.1 核心目标（一句话）
> 在 STM32 上跑通 256 点 FFT 幅度谱，并可视化到 OLED 与调试串口，支撑 A3 MFCC。

### 1.2 大方向策略
> 先集成 CMSIS-DSP 库（确定性 DSP），再算幅度谱，最后做 OLED/串口两种可视化。

### 1.3 硬边界（继承 CLAUDE.md）
- [x] 是否涉及底层库 / 第三方库？→ CMSIS-DSP 属第三方算法库，集成经适配层，不改其源码
- [x] 否 → 继续

### 1.4 关键参数（权威：事实包 §3.6 固化，子FEAT 直接引用，禁止自造）
| 子FEAT | 关键参数 |
|--------|---------|
| A2-01 | 链接 `libarm_cortexM4lf_math.a` 或直编 DSP 源；`-mfpu=fpv4-sp-d16 -mfloat-abi=hard` |
| A2-02 | 256 点 FFT；分辨率 16kHz/256≈62.5Hz/bin；1kHz→bin≈16；幅度 `arm_cmplx_mag_f32`；汉明窗 |
| A2-03 | OLED 32 频带柱状图；128 列每柱 4px；刷新 ~200ms（TaskDisplay） |
| A2-04 | 串口一行 32 个整数（能量）；节流 ~200ms |

> 通用事实（规格 §3.2/§3.4）：OLED=I2C1（PB6 SCL / PB7 SDA），SSD1306 0.96"，地址 0x3C/0x3D；I2S3 采样率 16kHz（SCK 1.024MHz）；调试串口 USART1 115200；boot 三行 `SoundDog boot OK` → `I2S_DRV_Init ret=0` → `I2S DMA started`；A1-03 结论：取数 `(int16_t)(raw & 0xFFFF)`，L/R→GND=左声道。

## 2. 项目表（子FEAT清单）

> 每个子FEAT 按子模板（`.template-child.md`）+ 傻瓜式操作单规格（`.sop-agent规格.md` §2）创建，独立执行 5 阶段（准备→设计→实现→测试→审查）。

| 子FEAT编号 | 功能 | 验收要点 | 优先级 | 依赖 | 状态 | 文件 |
|-----------|------|----------|--------|------|------|------|
| FEAT-A2-01 | CMSIS-DSP 库集成 | 编译通过，`arm_rfft_fast_f32` 示例跑通（无 NaN/溢出） | P0 | 依赖 A1 | 🟢已完成（bin=16/mag=64 实测命中） | `FEAT-A2-01-CMSIS-DSP集成.md` |
| FEAT-A2-02 | 256点 FFT + 幅度谱 | 1kHz 测试信号 → 峰值落在 bin 15~17 | P0 | 依赖 01 | 🟢已完成（2026-08-29 实测 bin=16 零漂移，ratio 40~55；含 unsigned 除法陷阱修复） | `FEAT-A2-02-FFT幅度谱.md` |
| FEAT-A2-03 | OLED 频谱柱状图 | OLED 实时刷新 32 频带柱状图（~200ms TaskDisplay） | P1 | 依赖 02 | 🟢已完成（2026-08-29 五轮修复 12 AC 全绿；F1 实测电气参数 + Mem_Write 分笔传输） | `FEAT-A2-03-OLED频谱显示.md` |
| FEAT-A2-04 | 调试串口频谱输出 | 串口周期输出一行 32 整数（能量，节流 ~200ms） | P1 | 依赖 02 | 🟢已完成（2026-08-30 BANDS 行 32 整数/200-240ms/吹气跟随 12 AC 全绿） | `FEAT-A2-04-串口频谱输出.md` |

> 子FEAT 完成后回填本行状态与文件链接。

## 3. 依赖与执行顺序

1. A2-01 库集成 → A2-02 FFT 幅度谱（串行）
2. A2-03 OLED 显示 与 A2-04 串口输出 可并行（均依赖 02）

## 4. 阻塞与下一步

- **阻塞**：无（A2 全线收官 2026-08-30）
- **下一步**：FEAT-A3（MFCC 特征提取）——输入接口已就绪（spec_process 的 32 频带 + getter；A2-02 §5.1；fft_run 不可重入，A3 新增消费方须串行化）

## 5. 完成定义（DoD）

- [x] 项目表全部子FEAT 🟢 且每个经 Review（A2-01 bin=16/mag=64；A2-02 bin=16 零漂移；A2-03 12 AC；A2-04 12 AC，均经阶段 5 Review）
- [x] 本表状态与 `docs/features/INDEX.md` 一致（父项 🟢 + 四子 🟢）
- [x] 经验教训已提炼（lessons_summary 5 组：unsigned 除法/串口单写者+契约字符串/BASEPRI 预调度器期/R28 跨硬件抄代码分层核对/gdb detach 停机事故）

## 6. 执行日志

| 时间 | 动作 | 角色 | 摘要 | 遇阻 |
|------|------|------|------|------|
| 2026-08-29 | A2-02 收官 | Dev/Test/Review | FFT 幅度谱实麦 1kHz→bin=16 零漂移；unsigned 除法陷阱修复（BUG-001） | 无 |
| 2026-08-29 | A2-03 收官 | Dev/Test/Review | OLED 柱状图 12 AC 全绿；五轮修复（I2C 控制字节/电气参数/BASEPRI 黑屏）；8 bug 中 5 个在本线闭环 | OLED 黑屏（BUG-005/006，已闭环） |
| 2026-08-30 | A2-04 收官 | Dev/Test/Review | 串口 BANDS 行 12 AC 全绿；调试器停机事故立案（BUG-20260830-001，非固件缺陷） | 误诊链（已定性与沉淀工具） |
| 2026-08-30 | 父FEAT 收官 | Planner | 四子全 🟢，DoD 三条全勾；A2 主线（麦克风→I2S DMA→队列→FFT 32 频带→OLED+串口双输出）完整打通 | 无 |
