# Makefile C_SOURCES 缺少手动添加的源文件

- **Bug ID**：BUG-20260728-002
- **严重等级**：P1-阻断
- **发现日期**：2026-07-28
- **修复日期**：2026-07-28

## 现象描述

`i2s_drv.h` 的 include 修好后，编译仍然报错：`implicit declaration of function 'I2S_DRV_Init'`

用 `./build.sh clean` 编译时，`i2s_drv.c` 完全不被编译（没有 `build/i2s_drv.o` 生成）。

## 复现步骤
1. CubeMX 切到 Makefile 重新生成
2. 手动复制 `i2s_drv.c` 到 `Src/`
3. 编译 → 链接时报 I2S_DRV_Init 未定义

## 根因分析

CubeMX 生成的 Makefile 中 `C_SOURCES` 列表只包含 CubeMX 自己生成或管理的源文件。手动加入的文件（如 `i2s_drv.c`）不在列表中，不会被编译，`.o` 不生成，链接自然失败。

```makefile
# Makefile 原来的 C_SOURCES:
C_SOURCES =  \
Src/main.c \
Src/gpio.c \
Src/i2s.c \        ← CubeMX 生成的
Src/usart.c \
...
# Src/i2s_drv.c  ← 缺了这个！
```

## 修复方案

在 Makefile 的 `C_SOURCES` 列表中添加 `Src/i2s_drv.c`：

```makefile
Src/i2s.c \
Src/i2s_drv.c \     ← 手动添加
Src/usart.c \
```

## 影响文件
- `firmware/soundDog/Makefile`

## 验证方式
1. `./build.sh clean` 编译
2. 确认 `build/i2s_drv.o` 生成
3. 链接通过，无 undefined reference

## 教训
> **CubeMX 切 Makefile 后每加一个手动 `.c` 文件，都要手动加到 Makefile 的 `C_SOURCES` 列表。** 以后加 DSP 文件、传感器驱动都会踩这个坑。也可以考虑在 Makefile 里用 `wildcard` 自动收集 `Src/*.c`，但 CubeMX 自身的 `Src/` 下也有 HAL 模板文件不适合通配，所以手动维护是目前最稳的做法。
