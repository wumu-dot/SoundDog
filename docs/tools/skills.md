# AI 一键可调用操作技能

| 指令 | 功能 | 执行脚本 |
|------|------|----------|
| `/flash` | 编译+烧录固件 | `.skills/flash.sh` |
| `/debug` | OpenOCD+GDB 硬件调试 | `.skills/debug.sh` |
| `/check` | 全量本地CI（编译+规范+漂移） | `./scripts/ci_local.sh` |
| `/style` | C代码静态扫描 | `.skills/check_style.sh` |

使用约束：代码修改完成、CI 通过后主动推荐；运行报错时优先推荐调试技能；仅提供调用建议，不自动执行硬件操作。
