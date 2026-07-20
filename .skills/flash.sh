#!/bin/bash
# 一键编译 + 烧录 (STM32CubeIDE)
set -euo pipefail
echo "🔨 编译 + 烧录"

cd "$(dirname "$0")/../firmware"

echo "  [1/2] 编译固件..."
# STM32CubeIDE 无头构建
stm32cubeide --launcher.suppressErrors -nosplash \
    -application org.eclipse.cdt.managedbuilder.core.headlessbuild \
    -import "$(pwd)" -build sounddog_firmware 2>/dev/null \
    || { echo "⚠️ STM32CubeIDE 不可用，尝试 Makefile..."; make clean && make -j6 all; }
echo "  ✅ 编译完成"

echo "  [2/2] 烧录到芯片..."
openocd -f openocd.cfg -c "program build/sounddog_firmware.elf verify reset exit"
echo "  ✅ 烧录完成，设备已复位运行"
