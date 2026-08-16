# DOC-STATE 构建系统切换后漂移

- **Bug ID**：BUG-20260728-004
- **严重等级**：P2-文档
- **发现日期**：2026-07-28
- **修复日期**：2026-07-28

## 现象描述

项目从 STM32CubeIDE 切换到 Makefile 编译后，`CLAUDE.md` 的 DOC-STATE 行仍记录 `BUILD=STM32CubeIDE+arm-none-eabi-gcc`，与实际不符。手动改了几次还改错了（加了 `mingw32-make` 但实际上 check-doc-drift.sh 不检测这个细节）。

同时 TASKS 字段记录为 `6`（规划值），实际代码只有 1 个 `osThreadNew`（占位任务）。

## 复现步骤
1. CubeMX 从 STM32CubeIDE 切到 Makefile → 重新生成
2. 没有手动更新 CLAUDE.md
3. `bash scripts/check-doc-drift.sh firmware/soundDog CLAUDE.md` → 报 BUILD 和 TASKS 漂移

## 根因分析

DOC-STATE 是手动维护的，任何工具链/任务数变更后人容易忘记同步。SoundDog 项目同时存在 `.cproject`（CubeIDE 工程文件）和 `Makefile`，脚本优先检测 `.cproject` 判定为 CubeIDE。实际编译用 Makefile。

## 修复方案

运行自动修复：
```bash
bash scripts/check-doc-drift.sh --fix firmware/soundDog CLAUDE.md
```

脚本会：
- BUILD → 保留 CubeIDE（因 `.cproject` 存在），两个工程文件并存不矛盾
- TASKS → 6 → 1（`grep -c osThreadNew + xTaskCreate` 实际计数）

## 影响文件
- `CLAUDE.md`（DOC-STATE 行自动更新）

## 验证方式
```bash
bash scripts/check-doc-drift.sh firmware/soundDog CLAUDE.md
# 输出: ✅ 文档与源码一致
```

## 教训
> **每次改完 CubeMX 配置或加了新 FreeRTOS 任务后，跑一次检查漂移。** 不跑也行——但下次 AI 读到 DOC-STATE 会基于错误信息做判断。建议在 `build_and_flash.bat` 之前加一行漂移检查。
