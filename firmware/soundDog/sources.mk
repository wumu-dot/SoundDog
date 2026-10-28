##########################################################################################################################
# sources.mk — 手写源码 / 头文件路径 / 编译宏（与 CubeMX 生成区分离）
#
# 为什么单独拆出来：
#   Makefile 由 CubeMX(projectgenerator) 生成，重新 GENERATE CODE 时会整体重写
#   C_SOURCES / C_INCLUDES / C_DEFS（Makefile 内没有 USER CODE 保护区）。
#   把"人加的"集中到本文件后，重新生成只需在 Makefile 恢复一行 include。
#   背景见 docs/bugs/BUG-20260728-001。
#
#   Makefile 接入位置（必须在 C_DEFS 与 C_INCLUDES 都已定义之后，
#   即生成版 C_INCLUDES 块之后、CFLAGS 之前）：
#     include sources.mk
#   本文件末尾会自行把三组变量 += 到生成版清单，无需在 Makefile 再写 +=。
#
# 新增手写 .c：只改本文件 PROJECT_C_SOURCES，不要改 Makefile。
# 新增头文件目录：加 -I 到 PROJECT_C_INCLUDES。
# 新增编译宏：加 -D 到 PROJECT_C_DEFS。
##########################################################################################################################

# ---------------------------------------------------------------------------
# 手写源文件（CubeMX 不会生成）
#   注：HAL 的 iwdg / i2c / i2c_ex 也在此列——.ioc 未启用 IWDG/I2C，
#   CubeMX 重新生成不会把它们的 .c 写进 Makefile。
# ---------------------------------------------------------------------------
PROJECT_C_SOURCES = \
  BSP/i2s_drv.c \
  BSP/rs485_drv.c \
  BSP/iwdg_drv.c \
  App/rtos_hooks.c \
  BSP/fft.c \
  BSP/spectrum.c \
  BSP/mfcc.c \
  BSP/mel.c \
  BSP/mfcc_dct.c \
  BSP/model_normal.c \
  BSP/dist.c \
  BSP/debounce.c \
  App/modbus_app.c \
  Middlewares/Third_Party/FreeModbus/modbus/mb.c \
  Middlewares/Third_Party/FreeModbus/modbus/rtu/mbrtu.c \
  Middlewares/Third_Party/FreeModbus/modbus/rtu/mbcrc.c \
  Middlewares/Third_Party/FreeModbus/modbus/functions/mbfunccoils.c \
  Middlewares/Third_Party/FreeModbus/modbus/functions/mbfuncdiag.c \
  Middlewares/Third_Party/FreeModbus/modbus/functions/mbfuncdisc.c \
  Middlewares/Third_Party/FreeModbus/modbus/functions/mbfuncholding.c \
  Middlewares/Third_Party/FreeModbus/modbus/functions/mbfuncinput.c \
  Middlewares/Third_Party/FreeModbus/modbus/functions/mbfuncother.c \
  Middlewares/Third_Party/FreeModbus/modbus/functions/mbutils.c \
  Middlewares/Third_Party/FreeModbus/port/cmsis_rtos2/portserial.c \
  Middlewares/Third_Party/FreeModbus/port/cmsis_rtos2/porttimer.c \
  Middlewares/Third_Party/FreeModbus/port/cmsis_rtos2/portevent.c \
  Middlewares/Third_Party/FreeModbus/port/cmsis_rtos2/portcritical.c \
  Src/i2c.c \
  BSP/oled_drv.c \
  Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_iwdg.c \
  Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_i2c.c \
  Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_i2c_ex.c \
  ../cmsis-dsp/repo/Source/TransformFunctions/arm_rfft_fast_f32.c \
  ../cmsis-dsp/repo/Source/TransformFunctions/arm_rfft_fast_init_f32.c \
  ../cmsis-dsp/repo/Source/TransformFunctions/arm_cfft_f32.c \
  ../cmsis-dsp/repo/Source/TransformFunctions/arm_cfft_radix8_f32.c \
  ../cmsis-dsp/repo/Source/TransformFunctions/arm_cfft_init_f32.c \
  ../cmsis-dsp/repo/Source/TransformFunctions/arm_bitreversal2.c \
  ../cmsis-dsp/repo/Source/CommonTables/arm_common_tables.c \
  ../cmsis-dsp/repo/Source/CommonTables/arm_const_structs.c \
  ../cmsis-dsp/repo/Source/ComplexMathFunctions/arm_cmplx_mag_f32.c \
  ../cmsis-dsp/repo/Source/FastMathFunctions/arm_sin_f32.c \
  ../cmsis-dsp/repo/Source/FastMathFunctions/arm_cos_f32.c \
  ../cmsis-dsp/repo/Source/BasicMathFunctions/arm_mult_f32.c \
  ../cmsis-dsp/repo/Source/BasicMathFunctions/arm_dot_prod_f32.c \
  ../cmsis-dsp/repo/Source/BasicMathFunctions/arm_scale_f32.c \
  ../cmsis-dsp/repo/Source/BasicMathFunctions/arm_offset_f32.c \
  ../cmsis-dsp/repo/Source/FastMathFunctions/arm_vlog_f32.c \
  ../cmsis-dsp/repo/Source/SupportFunctions/arm_copy_f32.c \
  ../cmsis-dsp/repo/Source/SupportFunctions/arm_fill_f32.c

# ---------------------------------------------------------------------------
# 手写头文件搜索路径
# ---------------------------------------------------------------------------
PROJECT_C_INCLUDES = \
  -I../cmsis-dsp/repo/Include \
  -IMiddlewares/Third_Party/FreeModbus/modbus/include \
  -IMiddlewares/Third_Party/FreeModbus/modbus/rtu \
  -IMiddlewares/Third_Party/FreeModbus/port

# ---------------------------------------------------------------------------
# 手写编译宏
#   HSE_VALUE 由 CubeMX 依 .ioc(RCC.HSE_VALUE) 生成，不在此重复。
# ---------------------------------------------------------------------------
PROJECT_C_DEFS = \
  -DARM_MATH_CM4F

# ---------------------------------------------------------------------------
# main.c 归属修正（防 CubeMX 重新生成时同名互撞）
#   .ioc 为 ProjectManager.MainLocation=Src：重新 GENERATE CODE 会在 Src/main.c
#   产出一份新骨架，而本工程实际主程序是 App/main.c。两者同名时：
#     - OBJECTS 用 $(notdir) → 两个 main.c 争抢同一个 build/main.o；
#     - 生成版 Makefile 会指向 Src/main.c → 编到空骨架，丢业务代码。
#   规则：App/main.c 存在时，以它为唯一 main 源，剔除 Src/main.c。
# ---------------------------------------------------------------------------
ifneq (,$(wildcard App/main.c))
  ifneq (,$(filter Src/main.c,$(C_SOURCES)))
    C_SOURCES := $(filter-out Src/main.c,$(C_SOURCES))
  endif
  ifeq (,$(filter App/main.c,$(C_SOURCES)))
    C_SOURCES += App/main.c
  endif
endif

# ---------------------------------------------------------------------------
# freertos.c 归属修正（同类问题）
#   .ioc 启用了 FREERTOS，重新 GENERATE CODE 会在 Src/freertos.c 产默认任务
#   骨架，而本工程实际任务实现在 App/freertos.c。规则同上：App 版本存在时，
#   以它为唯一 freertos 源，剔除 Src/freertos.c。
# ---------------------------------------------------------------------------
ifneq (,$(wildcard App/freertos.c))
  ifneq (,$(filter Src/freertos.c,$(C_SOURCES)))
    C_SOURCES := $(filter-out Src/freertos.c,$(C_SOURCES))
  endif
  ifeq (,$(filter App/freertos.c,$(C_SOURCES)))
    C_SOURCES += App/freertos.c
  endif
endif

# ---------------------------------------------------------------------------
# 自应用：被 include 时并入生成版清单（此时 C_SOURCES/C_INCLUDES/C_DEFS 均已定义）
# ---------------------------------------------------------------------------
C_SOURCES  += $(PROJECT_C_SOURCES)
C_INCLUDES += $(PROJECT_C_INCLUDES)
C_DEFS     += $(PROJECT_C_DEFS)
