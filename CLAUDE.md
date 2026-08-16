# 项目全局上下文（会话自动继承）
<!-- DOC-STATE: CHIP=STM32F407ZGT6, RTOS=FreeRTOS/CMSIS-RTOS_v2, BUILD=STM32CubeIDE+arm-none-eabi-gcc, TASKS=1, HSE=8000000, MIN_STACK=128 -->

## 硬件平台
主控：STM32F407ZGT6 | HSE=8MHz PLL→SYSCLK 168MHz | 调试接口 ST-Link SWD
引脚映射权威来源：`firmware/soundDog/Inc/pin_config.h`（改引脚只改这一个文件，CLAUDE.md 不重复维护）

## 软件环境
RTOS：FreeRTOS CMSIS-RTOS_v2 抢占式，当前 1 个占位任务（规划 6 个）
驱动库：STM32 HAL (CubeMX 生成)
编译构建：Makefile + mingw32-make + arm-none-eabi-gcc（也有 CubeIDE 工程可选用）

## 固定全局宏（禁止随意修改）
HSE_VALUE=8000000；configMINIMAL_STACK_SIZE=128，业务任务堆栈最低256字；
NVIC Priority Group 4（16级抢占），FreeRTOS `configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY=5`
中断优先级铁律：0~4 禁止调 FreeRTOS FromISR API，仅 5~15 可安全调用
HAL 时基：TIM7（独立于 FreeRTOS 的 SysTick，互不抢心跳）

## 场景导航（做X前先读Y）
| 你要做什么 | 先读这个 |
|-----------|---------|
| 改引脚/外设配置 | `firmware/soundDog/Inc/pin_config.h` + `CubeMX实操步骤.md` |
| 改FreeRTOS任务 | `PROJECT_PLAN.md` §5.1 |
| 音频采集/DSP调试 | `准备工作.md` |
| I2S/INMP441 问题 | `CubeMX_I2S3配置详解.md` + `外部资源索引.md` |
| 查外部资料（原理图/手册/例程） | `外部资源索引.md` |
| 编译或烧录失败 | `firmware/build.sh` / `firmware/build_and_flash.bat` |
| 搭建开发环境 | `docs/tools/dev-setup.md`（CodeGraph + Ponytail + OpenCLI） |
| 规划功能方向（开父FEAT） | `docs/features/.template-parent.md` |
| 实现具体功能（开子FEAT） | `docs/features/.template-child.md` |
| 出问题快速定位（文件→FEAT反查） | `docs/features/维护地图.md` |

## 开发硬性边界
1. 禁止修改 `firmware/Drivers/` 下 HAL 库文件；App 层开发仅通过 CubeMX 配置和适配层接口
2. 中断优先级固定分组4（NVIC_PriorityGroup_4）
3. FreeRTOS 中断优先级铁律：
   - `configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY = 5`
   - 优先级 0~4（数值小）= 不可调用任何 FreeRTOS FromISR API
   - 优先级 5~15（数值大）= 可安全调用 FromISR（如 `xTaskNotifyFromISR`）
   - SysTick (15)=FreeRTOS 内核 tick，TIM7 (TICK)=HAL 时基，互不抢心跳
   - DMA1_Stream2 (I2S3) = 5，不可降到 4 以下

---
## 芯片预设（检测到对应平台时自动填入）

### STM32 系列
| 字段 | 值 |
|------|-----|
| 调试接口 | ST-Link SWD |
| RTOS | FreeRTOS CMSIS-RTOS_v2 |
| 驱动库 | STM32 HAL |
| 编译构建 | Makefile + mingw32-make + arm-none-eabi-gcc |
| 固定宏 | HSE_VALUE=8000000；configMINIMAL_STACK_SIZE=128，业务任务堆栈最低256字 |
| 硬性边界 | 禁止修改 firmware/Drivers/ HAL 底层库文件；中断优先级固定分组4；FreeRTOS ISR 优先级 ≥ 5 |

### ESP32 系列
| 字段 | 值 |
|------|-----|
| 调试接口 | USB-UART JTAG |
| RTOS | FreeRTOS（ESP-IDF 内置） |
| 驱动库 | ESP-IDF |
| 编译构建 | CMake + ESP-IDF + xtensa-esp32-elf-gcc |
| 固定宏 | configMINIMAL_STACK_SIZE={{从sdkconfig读取}} |
| 硬性边界 | 禁止修改 components/ 下 framework 文件；WiFi/BLE 栈任务优先级不做修改 |

### 其他芯片
上面没有则逐项询问用户填写。

### 其他芯片适配检查清单

使用非 STM32/ESP32 芯片时，逐项确认并修改：

**不受影响（直接可用）：**

| 功能 | 说明 |
|------|------|
| DOC-STATE 校验 | SRC_CHIP=UNKNOWN，子串匹配仍工作 |
| ci_local.sh 门禁 | HSE检查自动跳过，其余不变 |
| FEAT 5阶段模板 | 芯片无关 |
| Bug 模板 + 故障库 | 芯片无关 |
| R0-R20 硬规则 | 芯片无关 |
| docs/ 文档体系 | 芯片无关 |

**需手动修改（3个文件）：**

| 检查项 | 文件位置 | 当前值（STM32示例） | 改为 |
|--------|----------|-------------------|------|
| 调试器配置 | `firmware/openocd.cfg` | `source [find target/stm32f4x.cfg]` | 目标芯片配置，如 `nrf52.cfg`、`rp2040.cfg` |
| 烧录命令 | `.skills/flash.sh` | `openocd -f openocd.cfg -c "program ..."` | 确认烧录方式，可能需要 pyocd、JLink |
| GDB 调试 | `.skills/debug.sh` | `arm-none-eabi-gdb` | RISC-V用 `riscv-elf-gdb`，xtensa用 `xtensa-esp32-elf-gdb` |
| RTOS | `DOC-STATE` | `FreeRTOS/CMSIS-RTOS_v2` | 改为 `Zephyr` / `RT-Thread` / `None` |
| 构建系统 | `DOC-STATE` | `Makefile+arm-none-eabi-gcc` | 改为实际工具链 |
| 时钟源 | `DOC-STATE` | `HSE=8000000` | 填主时钟频率，无HSE填 `N/A` |

> 操作流程：按上表逐项改完 → 运行 `scripts/check-doc-drift.sh` 校验 → 通过则适配完成。
