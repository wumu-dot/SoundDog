#!/bin/bash
# ============================================================
# SoundDog 一键编译脚本
# 用法: ./build.sh [clean]
#   ./build.sh          → 增量编译
#   ./build.sh clean    → 全量重新编译
# ============================================================
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$SCRIPT_DIR/soundDog"

echo "🔨 SoundDog 编译"
echo "  项目: $PROJECT_DIR"

cd "$PROJECT_DIR"

if [ "${1:-}" = "clean" ]; then
    echo "  [1/2] 清理旧构建..."
    mingw32-make clean
    echo "  [2/2] 全量编译..."
else
    echo "  [1/1] 增量编译..."
fi

mingw32-make -j8

echo ""
echo "✅ 编译完成"
echo "  ELF:  $PROJECT_DIR/build/soundDog.elf"
echo "  HEX:  $PROJECT_DIR/build/soundDog.hex"
echo "  BIN:  $PROJECT_DIR/build/soundDog.bin"
