#!/bin/bash
# 校验 CLAUDE.md DOC-STATE 与源码是否一致
# 用法: ./check-doc-drift.sh [--fix] <源码目录> [CLAUDE.md路径]
# 检测6个字段: CHIP, RTOS, BUILD, TASKS, HSE, MIN_STACK
# --fix: 自动将检测到的值写入 CLAUDE.md DOC-STATE 行
set -eu

FIX_MODE=0
if [ "${1:-}" = "--fix" ]; then
    FIX_MODE=1
    shift
fi

SRC_DIR="${1:-.}"
DOC_FILE="${2:-CLAUDE.md}"
SKIP_DIRS="lvgl|Drivers|Middlewares|components|node_modules"

RED="\033[31m"; GREEN="\033[32m"; YELLOW="\033[33m"; CYAN="\033[36m"; RESET="\033[0m"
DRIFT=0

die() { echo -e "${RED}❌ $1${RESET}"; DRIFT=1; }
ok()  { echo -e "${GREEN}✅ $1${RESET}"; }
fix() { echo -e "${CYAN}🔧 $1${RESET}"; }

# ====== 读取 DOC-STATE ======
if [ ! -f "$DOC_FILE" ]; then echo "❌ 找不到 $DOC_FILE"; exit 1; fi

DOC_STATE=$(sed -n 's/.*<!-- DOC-STATE: \([^>]*\) -->.*/\1/p' "$DOC_FILE" | head -1)
if [ -z "$DOC_STATE" ]; then
    echo "⚠️  $DOC_FILE 无 DOC-STATE 标记，将使用空值比对"
    DOC_STATE="CHIP=UNKNOWN, RTOS=None, BUILD=UNKNOWN, TASKS=0, HSE=NOT_FOUND, MIN_STACK=NOT_FOUND"
fi

doc_field() { echo "$DOC_STATE" | tr ',' '\n' | sed -n "s/.*$1=//p" | tr -d ' '; }

DOC_CHIP=$(doc_field "CHIP")
DOC_RTOS=$(doc_field "RTOS")
DOC_BUILD=$(doc_field "BUILD")
DOC_TASKS=$(doc_field "TASKS")
DOC_HSE=$(doc_field "HSE")
DOC_MIN_STACK=$(doc_field "MIN_STACK")

# ====== 源码检测 ======

detect_chip() {
    # 1. Makefile -D 定义 (最可靠: -DSTM32F407xx)
    local mf=$(find "$SRC_DIR" -maxdepth 3 -name "Makefile" 2>/dev/null | grep -vE "$SKIP_DIRS" | head -1)
    if [ -n "$mf" ]; then
        local d=$(grep -oE '\-DSTM32F[0-9]+[A-Za-z0-9]*' "$mf" 2>/dev/null | head -1 | sed 's/-D//' | sed 's/x\+$//')
        if [ -n "$d" ]; then echo "$d"; return; fi
    fi

    # 2. ESP-IDF
    local cm=$(find "$SRC_DIR" -maxdepth 3 -name "CMakeLists.txt" 2>/dev/null | grep -vE "$SKIP_DIRS" | head -1)
    if [ -n "$cm" ] && grep -qE "esp_idf|idf_component" "$cm" 2>/dev/null; then
        echo "ESP32"; return
    fi

    # 3. HAL 库兜底
    if find "$SRC_DIR" -name "stm32f4xx_hal.h" 2>/dev/null | grep -vE "$SKIP_DIRS" | head -1 > /dev/null; then
        echo "STM32F4"; return
    fi
    if find "$SRC_DIR" -name "stm32f1xx_hal.h" 2>/dev/null | grep -vE "$SKIP_DIRS" | head -1 > /dev/null; then
        echo "STM32F1"; return
    fi
    if find "$SRC_DIR" -name "stm32*.h" 2>/dev/null | grep -vE "$SKIP_DIRS" | head -1 > /dev/null; then
        echo "STM32"; return
    fi

    echo "UNKNOWN"
}

detect_rtos() {
    if find "$SRC_DIR" -name "FreeRTOSConfig.h" 2>/dev/null | grep -vE "$SKIP_DIRS" | head -1 > /dev/null; then
        echo "FreeRTOS/CMSIS-RTOS_v2"
    else
        echo "None"
    fi
}

detect_build() {
    local mf=$(find "$SRC_DIR" -maxdepth 3 -name "Makefile" 2>/dev/null | grep -vE "$SKIP_DIRS" | head -1)
    local cm=$(find "$SRC_DIR" -maxdepth 3 -name "CMakeLists.txt" 2>/dev/null | grep -vE "$SKIP_DIRS" | head -1)
    local ide=$(find "$SRC_DIR" -maxdepth 3 \( -name ".project" -o -name ".cproject" \) 2>/dev/null | grep -vE "$SKIP_DIRS" | head -1)

    if [ -n "$ide" ]; then
        echo "STM32CubeIDE+arm-none-eabi-gcc"
    elif [ -n "$mf" ]; then
        if grep -q "arm-none-eabi" "$mf" 2>/dev/null; then
            echo "Makefile+arm-none-eabi-gcc"
        else
            echo "Makefile+gcc"
        fi
    elif [ -n "$cm" ]; then
        if grep -qE "esp_idf|idf_component" "$cm" 2>/dev/null; then
            echo "CMake+ESP-IDF"
        else
            echo "CMake+gcc"
        fi
    else
        echo "UNKNOWN"
    fi
}

detect_tasks() {
    local bare=0 cmsis=0
    bare=$(find "$SRC_DIR" -name "*.c" 2>/dev/null | grep -vE "$SKIP_DIRS" | xargs grep -ch "xTaskCreate(" 2>/dev/null | awk '{sum+=$1} END{print sum+0}' || echo 0)
    cmsis=$(find "$SRC_DIR" -name "*.c" 2>/dev/null | grep -vE "$SKIP_DIRS" | xargs grep -ch "osThreadNew(" 2>/dev/null | awk '{sum+=$1} END{print sum+0}' || echo 0)
    echo $((bare + cmsis))
}

detect_hse() {
    # 1. stm32f4xx_hal_conf.h
    local v=$(find "$SRC_DIR" -name "stm32f4xx_hal_conf.h" 2>/dev/null | grep -v TEMPLATE | head -1)
    if [ -n "$v" ]; then
        v=$(grep '#define\s*HSE_VALUE' "$v" 2>/dev/null | grep -v '^\s*//\|^\s*#if' | grep -o '[0-9]\+' | head -1)
        if [ -n "$v" ]; then echo "$v"; return; fi
    fi

    # 2. pin_config.h / board.h
    local pc=$(find "$SRC_DIR" \( -name "pin_config.h" -o -name "board.h" \) 2>/dev/null | head -1)
    if [ -n "$pc" ]; then
        v=$(grep -E '#define\s*(HSE_FREQ|HSE_VALUE)' "$pc" 2>/dev/null | grep -v '^\s*//' | grep -o '[0-9]\+' | head -1)
        if [ -n "$v" ]; then echo "$v"; return; fi
    fi

    echo "NOT_FOUND"
}

detect_min_stack() {
    local v=$(find "$SRC_DIR" -name "FreeRTOSConfig.h" 2>/dev/null | head -1)
    if [ -n "$v" ]; then
        v=$(grep '#define\s*configMINIMAL_STACK_SIZE' "$v" 2>/dev/null | grep -o '[0-9]\+' | tail -1)
        if [ -n "$v" ]; then echo "$v"; return; fi
    fi
    echo "NOT_FOUND"
}

# ====== 执行检测 ======
SRC_CHIP=$(detect_chip)
SRC_RTOS=$(detect_rtos)
SRC_BUILD=$(detect_build)
SRC_TASKS=$(detect_tasks)
SRC_HSE=$(detect_hse)
SRC_MIN_STACK=$(detect_min_stack)

# ====== 比对 & 修复值收集 ======
# 匹配 → 保留文档原值；漂移 → 用源码值
FINAL_CHIP="$DOC_CHIP"
FINAL_RTOS="$DOC_RTOS"
FINAL_BUILD="$DOC_BUILD"
FINAL_TASKS="$DOC_TASKS"
FINAL_HSE="$DOC_HSE"
FINAL_MIN_STACK="$DOC_MIN_STACK"

compare() {
    local label="$1" doc_val="$2" src_val="$3"
    local field_var="$4"

    if [ -z "$doc_val" ] || [ -z "$src_val" ] || [ "$doc_val" = "SKIP" ]; then
        echo -e "${YELLOW}⊘  $label: 跳过${RESET}"
        return
    fi

    # NOT_FOUND/UNKNOWN = 源码尚未生成，仅提示不报错
    if [ "$src_val" = "NOT_FOUND" ] || [ "$src_val" = "UNKNOWN" ]; then
        echo -e "${YELLOW}⊘  $label: 文档=$doc_val  源码=$src_val (待生成)${RESET}"
        return
    fi

    # TASKS=0 = 任务尚未创建
    if [ "$label" = "业务任务数" ] && [ "$src_val" = "0" ]; then
        echo -e "${YELLOW}⊘  $label: 文档=$doc_val  源码=0 (任务尚未创建)${RESET}"
        return
    fi

    local d="${doc_val// /}" s="${src_val// /}"
    if [[ "$d" == *"$s"* ]] || [[ "$s" == *"$d"* ]]; then
        ok "$label: 文档=$doc_val  源码=$src_val"
    else
        die "$label: 文档=$doc_val  源码=$src_val"
        eval "$field_var=\"$src_val\""
    fi
}

echo "======== 文档 vs 源码 一致性检查 ========"
echo ""

compare "MCU主控"     "$DOC_CHIP"      "$SRC_CHIP"      "FINAL_CHIP"
compare "RTOS"        "$DOC_RTOS"      "$SRC_RTOS"      "FINAL_RTOS"
compare "构建系统"     "$DOC_BUILD"     "$SRC_BUILD"     "FINAL_BUILD"
compare "业务任务数"   "$DOC_TASKS"     "$SRC_TASKS"     "FINAL_TASKS"
compare "HSE_VALUE"   "$DOC_HSE"       "$SRC_HSE"       "FINAL_HSE"
compare "最小堆栈"     "$DOC_MIN_STACK" "$SRC_MIN_STACK" "FINAL_MIN_STACK"

echo ""

if [ "$DRIFT" -eq 0 ]; then
    echo -e "${GREEN}✅ 文档与源码一致。${RESET}"
    exit 0
fi

if [ "$FIX_MODE" -eq 1 ]; then
    NEW_STATE="CHIP=$FINAL_CHIP, RTOS=$FINAL_RTOS, BUILD=$FINAL_BUILD, TASKS=$FINAL_TASKS, HSE=$FINAL_HSE, MIN_STACK=$FINAL_MIN_STACK"
    if grep -q "<!-- DOC-STATE:" "$DOC_FILE"; then
        sed -i "s|<!-- DOC-STATE: [^>]* -->|<!-- DOC-STATE: $NEW_STATE -->|" "$DOC_FILE"
    else
        sed -i "1a<!-- DOC-STATE: $NEW_STATE -->" "$DOC_FILE"
    fi
    fix "DOC-STATE 已自动修复"
    echo -e "  旧: ${RED}$DOC_STATE${RESET}"
    echo -e "  新: ${GREEN}$NEW_STATE${RESET}"
    exit 0
fi

echo -e "${RED}📛 文档漂移！更新 $DOC_FILE 的 DOC-STATE 行，或运行 --fix 自动修复。${RESET}"
exit 1
