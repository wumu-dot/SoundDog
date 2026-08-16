#!/bin/bash
# ============================================================
# SoundDog 一键编译 + 烧录脚本
# 用法: ./flash.sh
# ============================================================
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$SCRIPT_DIR/soundDog"
ELF="$PROJECT_DIR/build/soundDog.elf"
OCD_CFG="$SCRIPT_DIR/openocd.cfg"

echo "🔥 SoundDog 编译 + 烧录"
echo ""

# ---------- Step 1: 编译 ----------
echo "  [1/2] 编译固件..."
cd "$PROJECT_DIR"
mingw32-make -j8
echo "  ✅ 编译完成: $ELF"

# ---------- Step 2: 烧录 ----------
echo ""
echo "  [2/2] 烧录到 STM32F407 (ST-Link SWD)..."
openocd -f "$OCD_CFG" \
    -c "program $ELF verify reset exit"

echo ""
echo "✅ 烧录完成，设备已复位运行"
