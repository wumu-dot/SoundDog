# STM32 GDB 调试配置
set remotetimeout 10
target extended-remote localhost:3333
monitor reset halt
load
monitor reset init
