# 项目文档索引

> 单次会话只读1次。做X前先查对应的Y。

## 场景检索地图

| 你要做什么 | 先读 |
|-----------|------|
| 改引脚/外设配置 | `firmware/Core/Inc/` + `CubeMX实操步骤.md` |
| 查外部资料（原理图/手册/例程） | `外部资源索引.md` |
| 音频采集/DSP调试 | `准备工作.md` |
| I2S/INMP441配置问题 | `CubeMX_I2S3配置详解.md` |
| 加/改FreeRTOS任务 | `PROJECT_PLAN.md` §5.1 |
| 调试HardFault死机 | `docs/tools/gdb_debug.md` |
| 编译构建报错 | `docs/tools/skills.md` → `/check` |
| 功能开发流程 | `docs/features/.template.md`（5阶段模板） |
| Bug登记 | `docs/bugs/.template.md` |
| 历史故障查询 | `docs/troubleshooting/bug_shturl` |
| 经验教训提炼 | `docs/summary/lessons_summary.md` |
| 搭建开发环境 | `docs/tools/dev-setup.md`（CodeGraph + Ponytail + OpenCLI） |

## 文档清单

| 文件 | 摘要 |
|------|------|
| `PROJECT_PLAN.md` | SoundDog 项目总体规划、硬件清单、软件架构 |
| `准备工作.md` | 开发环境搭建、分步调试方法论、开发节奏 |
| `CubeMX实操步骤.md` | STM32CubeMX 外设配置完整手顺（14步） |
| `CubeMX_I2S3配置详解.md` | I2S3 + INMP441 时钟推导与配置详解 |
| `外部资源索引.md` | 芯片手册、HAL例程、工具软件等外部资源速查 |
| `tools/gdb_debug.md` | CodeGraph → OpenOCD → GDB 硬件调试教程 |
| `tools/dev-setup.md` | 开发环境搭建指南（CodeGraph + Ponytail + OpenCLI） |
| `tools/skills.md` | `/flash` `/debug` `/check` `/style` 技能速查 |
| `features/.template.md` | FEAT 5阶段开发模板（准备→设计→实现→测试→审查） |
| `bugs/.template.md` | Bug 登记模板 |
| `summary/lessons_summary.md` | 从故障记录提炼的通用规范 |
| `troubleshooting/bug_shturl` | 历史故障复现、根因与修复记录 |

## 维护规则

新增文档才补充条目。空目录、占位文件不录入。修改文档同步更新对应条目。
