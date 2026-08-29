# 启用 HAL_I2C 模块后编译失败——vendored HAL 库为裁剪版无 I2C 驱动文件

- **Bug ID**：BUG-20260829-002
- **严重等级**：P2-一般（构建阻塞，无运行期影响；环境/依赖类）
- **发现日期**：2026-08-29
- **修复日期**：2026-08-29
- **关联 FEAT**：FEAT-A2-03（OLED 频谱显示）

## 现象描述
A2-03 阶段 3 在 `stm32f4xx_hal_conf.h` 启用 `HAL_I2C_MODULE_ENABLED` 并在 Makefile 登记 `stm32f4xx_hal_i2c.c` 后，全量编译立即失败：

```
Inc/stm32f4xx_hal_conf.h:363:11: fatal error: stm32f4xx_hal_i2c.h: No such file or directory
```

所有包含 `stm32f4xx_hal.h` 的编译单元（main/gpio/freertos/i2s/usart 等）全部报同错，属全局阻塞。

## 复现步骤
1. 在裁剪版 HAL 工程（`Drivers/STM32F4xx_HAL_Driver/Src` 仅含历史启用过的外设文件）中取消 `HAL_I2C_MODULE_ENABLED` 注释
2. 运行 `firmware\build.bat`
3. 观察 fatal error

## 根因分析
本项目 HAL 库是 CubeMX 生成时的**裁剪版**：`Src/` 目录仅含 18 个历史启用外设的驱动（I2S/UART/DMA/TIM 等），I2C 从未在 CubeMX 启用过，故 `stm32f4xx_hal_i2c.c/h`、`stm32f4xx_hal_i2c_ex.c/h` 四个文件从未被拷入。启用模块宏后 `hal_conf.h` 直接 `#include "stm32f4xx_hal_i2c.h"` 而文件不存在。
（首次 curl 404 插曲：STM32CubeF4 仓库已将 HAL 驱动拆为独立子模块仓库 `stm32f4xx_hal_driver`，老路径 `STM32CubeF4/master/Drivers/STM32F4xx_HAL_Driver` 不存在，下载到 14 字节的 404 文件。）

## 修复方案
按用户指示「直接用成熟方案，不自造」，从官方独立仓库 [STMicroelectronics/stm32f4xx_hal_driver](https://github.com/STMicroelectronics/stm32f4xx_hal_driver)（master 分支）vendored 4 个文件到本地 HAL 目录：
- `Drivers/STM32F4xx_HAL_Driver/Inc/stm32f4xx_hal_i2c.h`（34977B）
- `Drivers/STM32F4xx_HAL_Driver/Inc/stm32f4xx_hal_i2c_ex.h`（2974B）
- `Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_i2c.c`（241341B）
- `Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_i2c_ex.c`（5350B）

附带小修：`Src/i2c.c` 内 `Error_Handler()` 缺原型导致隐式声明告警-即错误（`-Wimplicit-function-declaration`），文件内补 `void Error_Handler(void);` 声明。

**版本差异风险（留给后续排查参考）**：本地库文件头部的 `@version` 注释已被裁剪，无法与官方 master 精确对版。本次编译 0 error、无新增警告，但若后续 I2C 运行期行为异常，应优先怀疑 HAL 版本不匹配（老版 F4 工程 + 最新 master 驱动），届时可回退到与本地库同期的 release tag。

## 影响文件
- `firmware/soundDog/Drivers/STM32F4xx_HAL_Driver/Inc/stm32f4xx_hal_i2c.h`（新增，官方 vendored）
- `firmware/soundDog/Drivers/STM32F4xx_HAL_Driver/Inc/stm32f4xx_hal_i2c_ex.h`（新增，官方 vendored）
- `firmware/soundDog/Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_i2c.c`（新增，官方 vendored）
- `firmware/soundDog/Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_i2c_ex.c`（新增，官方 vendored）
- `firmware/soundDog/Src/i2c.c`（Error_Handler 原型声明）
- `firmware/soundDog/Inc/stm32f4xx_hal_conf.h`（启用 I2C 模块宏——触发条件）
- `firmware/soundDog/Makefile`（登记 4 个源文件）

## 验证方式
修复后 `firmware\build.bat` 全量编译：
```
   text    data     bss     dec     hex
 156556     108   86080  242744   3b438  build/soundDog.elf
[ OK ] Build OK
```
0 error，无新增警告（仅历史遗留 `__FPU_PRESENT` 重定义）。

**验证条件**（必填，R25）：
- 硬件/环境：Windows + STM32CubeCLT 工具链（build.bat 内置 PATH），未上板
- 验证的因果链：仅**编译级**验证（fatal error 消失 + 链接通过 + size 输出）；I2C 总线通信与 OLED 显示的**功能级验证尚未执行**（待 A2-03 阶段 4 烧录：boot 串口 `OLED init OK` + 自检网格 + 柱状图跟随声音）
- 结论级别：编译通过（功能待验证）——不得因"编译通过"推断"I2C 驱动工作正常"

## 通用教训（提炼至 lessons_summary）
1. 裁剪版 HAL 工程启用新外设模块前，先 `Glob` 确认 `Drivers/.../Src` 是否有对应驱动文件，没有就从官方仓库 vendored（HAL 驱动已拆分至独立仓库 `stm32f4xx_hal_driver`，老 CubeF4 路径 404）。
2. vendored 官方文件尽量对准与本地库同期的 release tag；文件头 `@version` 被裁剪时无法对版，需在 bug 记录中留版本差异风险说明。
3. 新外设 .c 里调用 `Error_Handler()` 前确认原型（本项目声明在 main.h 用户区，非 HAL 公共头）。
