# SoundDog-IWDG看门狗交办纪要

> 日期：2026-09-21
> 分支：`feat/a4-04-modbus`（当前工作目录 `c:\Projects\SoundDog`）
> 任务：按 **方案 A** 补 IWDG 源 + 独立 FreeRTOS 喂狗任务 + 开发期 DBGMCU 冻结
> 状态：**编码已完成，编译验证被用户打断，尚未验证/烧录/归档**

---

## 一、本次交办目标（一句话）

使能 IWDG 独立看门狗（死机自动复位），喂狗放在**独立任务上下文**（禁 ISR 喂），开发期开 DBGMCU 冻结（调试 halt 不误复位）。

## 二、已完成（编码层面，均已落盘）

| # | 改动 | 文件 | 证据/备注 |
|---|------|------|-----------|
| 1 | 补官方 IWDG HAL 源（方案 A） | `firmware/soundDog/Drivers/STM32F4xx_HAL_Driver/{Src/stm32f4xx_hal_iwdg.c, Inc/stm32f4xx_hal_iwdg.h}` | 取自官方子模块 `STMicroelectronics/stm32f4xx_hal_driver` master，原样 TO 复制未改。来源 URL 见 FEAT 待写 |
| 2 | 使能 HAL_IWDG_MODULE_ENABLED | `firmware/soundDog/Inc/stm32f4xx_hal_conf.h:58` | 去注释；hal_conf.h 已有配套 include 块（:373-375，无需改） |
| 3 | Makefile 注册源文件 | `firmware/soundDog/Makefile` | 新增 `stm32f4xx_hal_iwdg.c`（:69）与 `BSP/iwdg_drv.c`（:48）到 C_SOURCES |
| 4 | 新建应用层 IWDG 驱动 | `firmware/soundDog/BSP/iwdg_drv.{h,c}` | 超时标称 1s（PR=64 → 500Hz，RLR=500），喂狗周期 200ms → 留 >30% 余量；`IWDG_DRV_DEV_FREEZE` 开关（默认 1=开发期冻结）；F4 写 DBGMCU->APB1FZ 无需开 DBGMCU 时钟 |
| 5 | 独立喂狗任务 | `firmware/soundDog/App/freertos.c` | `wdtTask`（osPriorityAboveNormal，栈 1KB），RTOS_THREADS 内创建；任务上下文惰性 `IWDG_DRV_Init` + 200ms 循环 `IWDG_DRV_Refresh` |

## 三、关键设计决策（含三源坑清单）

1. **惰性启动喂狗任务**：`IWDG_DRV_Init` 放喂狗任务上下文执行，规避"使能早于首喂 → 烧录即循环复位"坑（沿用 modbusSlaveTask 惰性启动惯例）。
2. **禁 ISR 喂狗**：`HAL_IWDG_Refresh`/`IWDG_DRV_Refresh` 唯一功能调用点是 `freertos.c:702`（任务上下文）；已 grep 全工程确认 0 处 ISR 喂。
3. **超时余量 >30%**：LSI 实测漂移约 ±7%（29.8~32.6kHz）；超时 1s 标称（最慢拉长 ~1.07s）对 200ms 喂狗周期仍余量充足，杜绝长时间阻塞误复位。
4. **开发期 DBGMCU 冻结**：调试 halt 冻结 IWDG 计数，单步不触发复位；release 置 `IWDG_DRV_DEV_FREEZE=0`。

## 四、未完成 → 收尾清单（建议优先级）

1. **🔴 编译验证（必需，R21）**：本机工具链可用（`arm-none-eabi-gcc` 14.3.1 / `build.bat`）。被打断，需重新执行 `firmware\build.bat`，确认 0 错误 0 新增告警（此前测试工具调用被拒/中断）。
2. 烧录实测（有板情况下）：
   - AC-01 正常观察无意外复位；
   - AC-02 模拟死机自动复位（复位间隔 ≈ 超时）；
   - AC-04 调试单步/暂停不应触发复位（DBGMCU 冻结生效）。
   - 提示：该分支此前 RS485 链路已打通，板上烧录条件应齐备。
3. 落 FEAT 文档：`docs/features/FEAT-A7-02-IWDG看门狗.md` 由"🔴未开始"改写为**真实施工记录**：填三源来源 URL（R32）、§5.1 影响文件、超时/喂狗周期定稿值、AC 勾选、阶段日志。
4. 更新 `docs/features/INDEX.md` + `维护地图.md` + 父 FEAT-A7 项目表回填。
5. 更新 `.claude/rules.md` 新 R 编号（R44+，本次"惰性启动+任务上下文喂狗+DBGMCU冻结"教训）。
6. 触发 `requesting-code-review`（R23 禁自我批准；本分支子阶段需独立审查后再合）。

## 五、风险与注意事项

- **未编译验证**是本任务最大不确定点（半成品），合入前必须先过编译。
- `IWDG_DRV_DEV_FREEZE=1` 只宜开发期；正式/量产固件必须置 0（此宏仅影响调试冻结，不影响量产运行）。
- IWDG 一旦使能运行中无法关闭，烧录后如需改超时需重编译重烧。

## 六、交接给后续会话的提示

- 若继续：直接从「四.1」编译验证开始；若编译报错，优先查 `stm32f4xx_hal_iwdg.h` 与工程现有 HAL 版本兼容性（本项目 HAL 为 CubeMX 4.7.0-B52 生成时代，官方管道 master 的 IWDG 徽标 API 稳定，理论无冲突）。
- 工具：`build.bat` 内部已自设 PATH 到 STM32CubeCLT+GCC，PowerShell 直接 `& 'c:\Projects\SoundDog\firmware\build.bat'` 即可（勿用 cmd /c）。