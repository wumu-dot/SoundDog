#!/bin/bash
# 一键 GDB 调试：启动 OpenOCD + GDB
set -euo pipefail
echo "🔍 GDB 调试 | OpenOCD + ST-Link SWD"

cd "$(dirname "$0")/../firmware"

if ! command -v arm-none-eabi-gdb &>/dev/null; then
    echo "❌ arm-none-eabi-gdb 未找到，请安装 ARM GCC toolchain"
    exit 1
fi

echo "  [1/3] 启动 OpenOCD (localhost:3333)..."
openocd -f openocd.cfg &
OCD_PID=$!
sleep 3

cleanup() {
    echo ""
    echo "🧹 关闭 OpenOCD..."
    kill $OCD_PID 2>/dev/null || true
}
trap cleanup EXIT

echo "  [2/3] 连接 GDB..."
arm-none-eabi-gdb build/sounddog_firmware.elf \
    -ex "target remote :3333" \
    -ex "monitor reset halt" \
    -ex "load" \
    -ex "break main" \
    -ex "echo === GDB ready. Type 'continue' to run. ===\n" \
    -ex "continue"

echo "  [3/3] 调试会话结束"
