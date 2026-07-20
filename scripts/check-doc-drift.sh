#!/bin/bash
# 校验 CLAUDE.md DOC-STATE 与源码是否一致
# 用法: ./check-doc-drift.sh <源码目录> [CLAUDE.md路径]
set -eu

SRC_DIR="${1:-.}"
DOC_FILE="${2:-CLAUDE.md}"
SKIP_DIRS="lvgl|Drivers|Middlewares|components|node_modules"

RED="\033[31m"; GREEN="\033[32m"; YELLOW="\033[33m"; RESET="\033[0m"
DRIFT=0

die() { echo -e "${RED}❌ $1${RESET}"; DRIFT=1; }
ok()  { echo -e "${GREEN}✅ $1${RESET}"; }

# ====== 读取 DOC-STATE ======
if [ ! -f "$DOC_FILE" ]; then echo "❌ 找不到 $DOC_FILE"; exit 1; fi

DOC_STATE=$(sed -n 's/.*<!-- DOC-STATE: \([^>]*\) -->.*/\1/p' "$DOC_FILE" | head -1)
if [ -z "$DOC_STATE" ]; then
    echo "⚠️  $DOC_FILE 无 DOC-STATE 标记"
    echo "   添加: <!-- DOC-STATE: CHIP=..., RTOS=..., BUILD=..., TASKS=..., HSE=..., MIN_STACK=... -->"
    exit 0
fi

doc_field() { echo "$DOC_STATE" | tr ',' '\n' | sed -n "s/.*$1=//p" | tr -d ' '; }

DOC_CHIP=$(doc_field "CHIP")
DOC_BUILD=$(doc_field "BUILD")
DOC_TASKS=$(doc_field "TASKS")
DOC_HSE=$(doc_field "HSE")
DOC_MIN_STACK=$(doc_field "MIN_STACK")

# ====== 工具函数 ======
# 在源码目录搜索，跳过第三方库
src_grep() {
    find "$SRC_DIR" -type f \( -name "*.c" -o -name "*.h" -o -name "*.txt" -o -name "Makefile" \) \
        | grep -vE "$SKIP_DIRS" \
        | xargs grep -l "$1" 2>/dev/null || true
}
src_grep_count() {
    find "$SRC_DIR" -type f -name "*.c" \
        | grep -vE "$SKIP_DIRS" \
        | xargs grep -ch "$1" 2>/dev/null \
        | awk '{sum+=$1} END{print sum+0}' || echo "0"
}

# ====== 芯片检测 ======
ESP_HITS=$(src_grep "idf_component_register\|ESP_LOGI\|esp_err\|esp_netif")
STM_HITS=$(src_grep "stm32f4xx_hal\|HAL_Init")
STM_ANY=$(src_grep "stm32")

if [ -n "$ESP_HITS" ] && [ -z "$STM_HITS" ]; then
    SRC_CHIP="ESP32"
elif [ -n "$STM_HITS" ]; then
    SRC_CHIP="STM32F4"  # 子串匹配 STM32F407ZGT6 / STM32F407VET6 等
elif [ -n "$STM_ANY" ]; then
    SRC_CHIP="STM32"
else
    SRC_CHIP="UNKNOWN"
fi

# ====== 构建系统 ======
MAKE=$(find "$SRC_DIR" -maxdepth 2 -name "Makefile" | grep -vE "$SKIP_DIRS" | head -1)
CMAKE=$(find "$SRC_DIR" -maxdepth 2 -name "CMakeLists.txt" | grep -vE "$SKIP_DIRS" | head -1)
ECLIPSE=$(find "$SRC_DIR" -maxdepth 2 \( -name ".project" -o -name ".cproject" \) | grep -vE "$SKIP_DIRS" | head -1)

if [ -n "$MAKE" ]; then
    if grep -q "arm-none-eabi" "$MAKE" 2>/dev/null; then
        SRC_BUILD="Makefile+arm-none-eabi-gcc"
    else
        SRC_BUILD="Makefile+gcc"
    fi
elif [ -n "$ECLIPSE" ]; then
    SRC_BUILD="STM32CubeIDE+arm-none-eabi-gcc"
elif [ -n "$CMAKE" ]; then
    if grep -q "esp_idf\|idf_component" "$CMAKE" 2>/dev/null; then
        SRC_BUILD="CMake+ESP-IDF"
    else
        SRC_BUILD="CMake+gcc"
    fi
else
    SRC_BUILD="UNKNOWN"
fi

# ====== 任务数量 ======
BARE=$(src_grep_count "xTaskCreate(")
CMSIS=$(src_grep_count "osThreadNew(")
SRC_TASKS=$((BARE + CMSIS))

# ====== HSE ======
SRC_HSE=$(find "$SRC_DIR" -name "stm32f4xx_hal_conf.h" 2>/dev/null \
    | grep -v TEMPLATE | head -1 \
    | xargs grep '#define\s*HSE_VALUE\s' 2>/dev/null \
    | grep -v '^.*#if\|^.*TEMPLATE' \
    | sed 's/.*HSE_VALUE//' \
    | grep -o '[0-9]\+' | head -1 \
    || echo "NOT_FOUND")
[ -z "$SRC_HSE" ] && SRC_HSE="NOT_FOUND"

# ====== 最小堆栈 ======
SRC_MIN_STACK=$(find "$SRC_DIR" -name "FreeRTOSConfig.h" 2>/dev/null \
    | head -1 \
    | xargs grep '#define\s*configMINIMAL_STACK_SIZE' 2>/dev/null \
    | sed 's/.*configMINIMAL_STACK_SIZE//' \
    | grep -o '[0-9]\+' | tail -1 \
    || echo "NOT_FOUND")
[ -z "$SRC_MIN_STACK" ] && SRC_MIN_STACK="NOT_FOUND"

# ====== 比对 ======
compare() {
    local label="$1" doc_val="$2" src_val="$3"
    if [ -z "$doc_val" ] || [ -z "$src_val" ] || [ "$doc_val" = "SKIP" ]; then
        echo -e "${YELLOW}⊘  $label: 跳过${RESET}"
        return
    fi
    # NOT_FOUND/UNKNOWN 表示源码尚未生成（如CubeMX未运行），仅提示不报错
    # TASKS=0（任务尚未创建）同样跳过
    if [ "$src_val" = "NOT_FOUND" ] || [ "$src_val" = "UNKNOWN" ]; then
        echo -e "${YELLOW}⊘  $label: 文档=$doc_val  源码=$src_val (待生成)${RESET}"
        return
    fi
    if [ "$label" = "业务任务数" ] && [ "$src_val" = "0" ]; then
        echo -e "${YELLOW}⊘  $label: 文档=$doc_val  源码=0 (任务尚未创建)${RESET}"
        return
    fi
    local d="${doc_val// /}" s="${src_val// /}"
    if [[ "$d" == *"$s"* ]] || [[ "$s" == *"$d"* ]]; then
        ok "$label: 文档=$doc_val  源码=$src_val"
    else
        die "$label: 文档=$doc_val  源码=$src_val"
    fi
}

echo "======== 文档 vs 源码 一致性检查 ========"
echo ""

compare "MCU主控"     "$DOC_CHIP"      "$SRC_CHIP"
compare "构建系统"     "$DOC_BUILD"     "$SRC_BUILD"
compare "业务任务数"   "$DOC_TASKS"     "$SRC_TASKS"
compare "HSE_VALUE"   "$DOC_HSE"       "$SRC_HSE"
compare "最小堆栈"     "$DOC_MIN_STACK" "$SRC_MIN_STACK"

echo ""
if [ "$DRIFT" -eq 1 ]; then
    echo -e "${RED}📛 文档漂移！更新 $DOC_FILE 的 DOC-STATE 行。${RESET}"
    exit 1
else
    echo -e "${GREEN}✅ 文档与源码一致。${RESET}"
    exit 0
fi
