#!/bin/bash
# 嵌入式项目本地CI门禁：编译→故障匹配→规范检查→文档漂移
# 用法: bash scripts/ci_local.sh [源码目录] [CLAUDE.md]
set -eu

SRC_DIR="${1:-firmware}"
DOC_FILE="${2:-CLAUDE.md}"

echo "🔨 编译校验"

# 自动检测构建系统
if [ -f "$SRC_DIR/Makefile" ]; then
    cd "$SRC_DIR" && make clean && make -j6 all
    BUILD_OK=$?
elif [ -f "$SRC_DIR/CMakeLists.txt" ]; then
    cd "$SRC_DIR" && mkdir -p build && cd build && cmake .. && make -j6
    BUILD_OK=$?
elif [ -f "$SRC_DIR/.project" ] || [ -f "$SRC_DIR/.cproject" ]; then
    echo "  STM32CubeIDE 项目 — 请在 IDE 中编译，跳过命令行构建"
    BUILD_OK=0
else
    echo "  ⚠️ 未检测到构建系统 (Makefile/CMakeLists.txt/.project)，跳过编译"
    BUILD_OK=0
fi

if [ $BUILD_OK -ne 0 ]; then
    echo "❌ 编译失败，检索故障库"
    ERROR_KEY=$(cd "$SRC_DIR" && make 2>&1 | grep -E "error|undefined|fault" | head -1 || echo "")
    if [ -n "$ERROR_KEY" ]; then
        grep -i "${ERROR_KEY:0:40}" ../docs/troubleshooting/bug_shturl 2>/dev/null | head -3 || echo "无匹配历史故障记录"
    fi
    exit 1
fi
echo "✅ 编译通过"

cd "$(dirname "$0")/.."

echo "📋 规范检查"
PASS=1

# FreeRTOS 最小任务堆栈
FREERTOS_CONFIG=$(find "$SRC_DIR" -name "FreeRTOSConfig.h" 2>/dev/null | head -1)
if [ -n "$FREERTOS_CONFIG" ]; then
    STACK_MIN=$(grep "configMINIMAL_STACK_SIZE" "$FREERTOS_CONFIG" | grep -o '[0-9]\+' | tail -1)
    if [ -n "$STACK_MIN" ] && [ "$STACK_MIN" -lt 128 ]; then
        echo "❌ configMINIMAL_STACK_SIZE 最小128字，当前: $STACK_MIN"
        PASS=0
    fi
fi

# HSE 晶振范围 (仅 STM32)
HSE_FILE=$(find "$SRC_DIR" -name "stm32f4xx_hal_conf.h" 2>/dev/null | grep -v TEMPLATE | head -1)
if [ -n "$HSE_FILE" ]; then
    HSE_VAL=$(grep '#define\s*HSE_VALUE' "$HSE_FILE" | grep -v '^\s*//\|^\s*#if' | grep -o '[0-9]\+' | head -1)
    if [ -n "$HSE_VAL" ] && { [ "$HSE_VAL" -lt 4000000 ] || [ "$HSE_VAL" -gt 25000000 ]; }; then
        echo "❌ HSE_VALUE 需在 4M~25M 区间，当前: $HSE_VAL"
        PASS=0
    fi
fi

[ "$PASS" -eq 0 ] && { echo "❌ 规范检查失败"; exit 1; }
echo "✅ 规范检查通过"

echo "📋 文档漂移检查"
bash scripts/check-doc-drift.sh "$SRC_DIR" "$DOC_FILE" || { echo "❌ 请更新 CLAUDE.md DOC-STATE"; exit 1; }

echo "✅ 全量CI通过"
