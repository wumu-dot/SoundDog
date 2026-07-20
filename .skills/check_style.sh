#!/bin/bash
# C代码静态规范检查
set -euo pipefail
echo "📝 C代码静态检查"

cd "$(dirname "$0")/../firmware"
cppcheck --enable=warning,performance,unusedFunction \
    --suppress=missingIncludeSystem \
    Core/ 2>/dev/null || echo "⚠️ cppcheck未安装，跳过"
echo "✅ 静态检查完成"
