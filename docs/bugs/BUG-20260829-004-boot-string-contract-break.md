# 修改 boot 三行契约字符串"I2S DMA started"——违反 .sop-agent规格.md §3.4

- **Bug ID**：BUG-20260829-004
- **严重等级**：P1-重要（破坏被 ~11 个下游文档引用的验收契约）
- **发现日期**：2026-08-29
- **修复日期**：2026-08-29
- **关联 FEAT**：FEAT-A2-02（串口修复过程中引入）

## 现象描述
把 ISR 内 printf 迁到 specTask 时，顺手把首帧提示串改成了新文案，导致 boot 契约三行之一的 `I2S DMA started` 前缀消失。串口契约是项目固定验收判据（.sop-agent规格.md §3.4），下游 ~11 个 FEAT 文档按该前缀匹配。

## 复现步骤
1. 检查 freertos.c specTask 首帧 printf 字符串
2. 与 .sop-agent规格.md §3.4 契约比对 → 前缀不匹配

## 根因分析
printf 字符串是**项目契约**而不是自由文本；"小改动"（迁移一行打印）没有先做字符串级影响审计。回溯性影响审计才发现。

## 修复方案
恢复前缀 `I2S DMA started`，允许后缀补充细节（前缀匹配判据不受影响）。

## 影响文件
- `firmware/soundDog/App/freertos.c`（specTask 首帧打印）

## 验证方式
烧录后串口 boot 输出按序包含三行契约前缀：
```
SoundDog boot OK, SYSCLK=...
I2S_DRV_Init ret=0 ...
I2S DMA started ...
```

**验证条件**（必填，R25）：
- 硬件/环境：本板 + 串口
- 验证的因果链：boot 输出前缀逐行匹配 §3.4 契约
- 结论级别：功能正常（A2-02 阶段 4 验收时确认）

## 通用教训（提炼至 lessons_summary）
1. 改任何 printf 字符串前，先 grep **代码 + docs/** 双侧（本教训已入 lessons：串口打印串是契约）。
2. R9 影响地图纪律对"一行级小改"同样适用——本轮正是跳过确认才漏掉契约面。
