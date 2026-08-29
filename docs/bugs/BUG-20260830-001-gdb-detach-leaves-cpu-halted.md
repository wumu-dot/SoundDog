# GDB/OpenOCD 会话遗留停机状态——误诊为固件"挂死/自发复位"

- **Bug ID**：BUG-20260830-001（序列内又称 BUG-007）
- **严重等级**：P0-致命（非固件缺陷，调试流程事故；曾两次制造"整机挂死"假象并污染排查方向）
- **发现日期**：2026-08-30
- **修复日期**：2026-08-30
- **关联 FEAT**：FEAT-A2-04（串口频谱输出，BANDS 行验证期间）

## 现象描述

A2-04 烧录后串口输出 BANDS/max/SPEC 正常运行数分钟后**突然静默**；用户报告"刚刚突然复位了，然后就不动了，串口没数据"。此前麦克风测试日志中亦有两次"自发复位"假象（实为烧录动作）。

## 复现步骤（事故链）

1. Dev Agent 用 `arm-none-eabi-gdb -batch ... -ex detach` 做烧录后状态检查（读 err_count/uwTick）
2. gdb `target extended-remote` connect 时 OpenOCD 自动 halt CPU；`detach` **不恢复运行**
3. 板子从此停在 idle 任务，串口静默 → 用户以为固件挂死
4. 排查期间又在 SysTick_Handler 设硬件断点跑 `continue`，batch 结束再次 detach 留下断点 + 停机 → "uwTick 冻结/中断挂起进不来"等假证据

## 根因分析

OpenOCD gdb server 的连接语义：**connect 即 halt，detach 不断电不恢复**。`-batch` 脚本若不以 `monitor resume` 收尾，CPU 遗留 C_HALT=1（DHCSR=0x...03）状态。后续所有"读寄存器/变量"读到的都是停机冻结值，形成系统性误诊链：
- uwTick/xTickCount 不变 → 误判"SysTick 死"
- ICSR ISRPENDING=1 → 误判"中断被屏蔽"（实际是被 halt 挡住）
- GDB 读 SysTick CTRL 得 0 → 寄存器同步假象（OpenOCD `mdw` 直读实为 0x7 正常）
- 麦克风测试日志里 2 次"自发复位"实为烧录脚本的 `monitor reset run`，非掉电/看门狗

## 修复方案

1. **规范 gdb 会话收尾**：任何 gdb 会话（烧录/检查/断点）结束前必须 `monitor resume` 再 detach（或显式 `monitor reset run`）
2. **运行态监控改用非侵入通道**：新增 [.skills/oocd_probe.ps1](../../../.skills/oocd_probe.ps1)——telnet 直连 OpenOCD:4444 发 `mdw`，**不停机**读取内存变量（DHCSR 判 S_HALT、uwTick 判走时、xTickCount 判调度器）
3. 断点用完立即 `monitor rbp all` + `delete`

## 影响文件

- `.skills/oocd_probe.ps1`（新增，非侵入探针工具）
- 无固件代码改动（本 bug 为流程缺陷，固件无辜）

## 验证方式

恢复 `monitor resume` 后非侵入采样（间隔 2s）：
```
DHCSR=0x01010001   S_HALT=0 CPU 运行中
uwTick:   416023   （恢复后连续跑 ~7 分钟，与墙上时钟吻合）
xTickCount: 414106 （差值 1917ms ≈ OLED 白屏诊断期，自洽）
```
串口 BANDS/max/SPEC 行恢复刷屏（用户终端确认）。

**验证条件**（必填，R25）：
- 硬件/环境：STM32F407 + ST-Link V2 + OpenOCD 0.12.0 xpack + GDB batch 脚本
- 因果链：停机状态（DHCSR C_HALT=1）→ resume → 同一探针读到 S_HALT=0 且 uwTick 与墙钟同步递增
- 结论级别：因果级（复现-修复-恢复三段证据齐全）

## 通用教训（提炼至 lessons_summary）

1. **gdb detach ≠ resume**：OpenOCD 后端下 detach 遗留停机态；凡 batch 会话必须以 `monitor resume` 或 `reset run` 收尾。
2. **"挂死"先查调试器再怀疑固件**：读 DHCSR 确认 S_HALT——被外部 halt 的 CPU 表现与固件死锁一模一样（tick 冻结、中断挂起、串口静默）。
3. **运行态监控禁用 gdb connect**（connect 即 halt）；用 telnet `mdw` 直读（oocd_probe.ps1）。
4. **GDB 读外设寄存器可能同步失真**（SysTick CTRL 读 0 实为 7）；关键寄存器用 OpenOCD `mdw` 直读交叉验证。
5. 用户报"复位"先问"是否有人烧录/复位"再排查 brown-out——本次及麦克风日志的 3 次"自发复位"全部是调试动作。
