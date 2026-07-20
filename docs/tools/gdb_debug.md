# CodeGraph → OpenOCD → GDB 调试流程

## 调试三步

1. **CodeGraph** 定位函数名、调用链 → 知道下断点在哪
2. **OpenOCD** 连接板子（监听端口3333）→ ST-Link ↔ STM32
3. **GDB** 下断点、看寄存器、单步走

## 前置条件

| 工具 | 验证 |
|------|------|
| OpenOCD | `openocd --version` |
| arm-none-eabi-gdb | `arm-none-eabi-gdb --version` |

## 标准流程

```bash
# 终端1: 启动 OpenOCD（保持运行）
openocd -f firmware/openocd.cfg

# 终端2: 启动 GDB
arm-none-eabi-gdb firmware/build/{{PROJECT_NAME}}.elf
```

GDB 内连接并烧录：
```gdb
target remote :3333      # 连接 OpenOCD
monitor reset halt       # 复位暂停
load                     # 烧录固件
break main               # 设断点
continue                 # 运行
```

## 常用命令

| 命令 | 简写 | 作用 |
|------|------|------|
| `continue` | `c` | 继续运行 |
| `next` | `n` | 单步（不进入函数） |
| `step` | `s` | 单步（进入函数） |
| `print 变量` | `p 变量` | 查看变量 |
| `backtrace` | `bt` | 调用栈 |
| `info locals` | `i lo` | 局部变量 |
| `info registers` | `i r` | 寄存器 |
| `watch 变量` | | 变量被修改时暂停 |

## 常见问题

| 问题 | 原因 | 解决 |
|------|------|------|
| `openocd: command not found` | 未加入 PATH | 用完整路径 |
| `Error: open failed` | ST-Link 未连接 | 检查 USB |
| 连接超时 | OpenOCD 未启动 | 确认端口3333监听 |
| 断点不生效 | 编译优化太高 | Makefile 用 `-Og` |
