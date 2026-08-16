# CubeMX 切换 Makefile/重新生成后手动 include 丢失

- **Bug ID**：BUG-20260728-001
- **严重等级**：P1-阻断
- **发现日期**：2026-07-28
- **修复日期**：2026-07-28

## 现象描述

CubeMX 从 STM32CubeIDE 切换到 Makefile 重新生成代码后，编译报错：
- `unknown type name 'audio_frame_t'`
- `implicit declaration of function 'I2S_DRV_Init'`
- `implicit declaration of function 'printf'`

## 复现步骤
1. CubeMX 选择 Makefile Toolchain → GENERATE CODE（覆盖已有项目）
2. `./build.sh clean` 编译
3. main.c 报一堆 implicit declaration 和 unknown type

## 根因分析

CubeMX 生成代码时**只保留 `/* USER CODE BEGIN xxx */` 和 `/* USER CODE END xxx */` 之间的代码**。注释区外的任何手动修改（包括 `#include`）都会被覆盖删除。

```
❌ 错误写法（会被删）:
#include "main.h"
#include "i2s.h"
#include "i2s_drv.h"    ← 这行会被 CubeMX 覆盖掉
/* USER CODE BEGIN Includes */
/* USER CODE END Includes */

✅ 正确写法（保留）:
#include "main.h"
#include "i2s.h"
/* USER CODE BEGIN Includes */
#include "i2s_drv.h"    ← 放这里，CubeMX 不碰
#include <stdio.h>
/* USER CODE END Includes */
```

## 修复方案

将 `#include "i2s_drv.h"` 和 `#include <stdio.h>` 移入 `/* USER CODE BEGIN Includes */` 区域。

## 影响文件
- `firmware/soundDog/Src/main.c`

## 验证方式
1. 在 CubeMX 中 GENERATE CODE 覆盖项目
2. 检查 main.c 中 `USER CODE BEGIN Includes` 区是否保留 `i2s_drv.h` 和 `stdio.h`
3. `./build.sh clean` 编译通过

## 教训
> **所有手动添加的 include / 变量 / 函数都必须在 USER CODE BEGIN/END 标记之间。** 标记外的任何代码都是 CubeMX 的"领土"，下次生成时会被无条件覆盖。
