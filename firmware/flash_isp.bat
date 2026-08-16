@echo off
chcp 65001 >nul
setlocal enabledelayedexpansion
echo ========================================
echo  SoundDog Flash via ISP (UART BOOT0)
echo ========================================
echo.

:: ---- Toolchain paths ----
set "GCC_BIN=C:\ST\STM32CubeCLT_1.21.0\GNU-tools-for-STM32\bin"
set "PROG_BIN=C:\ST\STM32CubeCLT_1.21.0\STM32CubeProgrammer\bin"
set "MINGW_BIN=C:\mingw64\bin"
set "PATH=%GCC_BIN%;%PROG_BIN%;%MINGW_BIN%;%PATH%"

:: ---- 0. Verify toolchain ----
echo [0/3] Checking toolchain...
"%GCC_BIN%\arm-none-eabi-gcc.exe" --version >nul 2>&1
if errorlevel 1 (
    echo ERROR: arm-none-eabi-gcc not found
    pause
    exit /b 1
)
"%PROG_BIN%\STM32_Programmer_CLI.exe" --version >nul 2>&1
if errorlevel 1 (
    echo ERROR: STM32_Programmer_CLI not found
    pause
    exit /b 1
)
echo OK

:: ---- 1. Build ----
echo.
echo [1/3] Building firmware...
cd /d "%~dp0soundDog"
call "%MINGW_BIN%\mingw32-make.exe" -j8 all
if errorlevel 1 (
    cd /d "%~dp0"
    echo.
    echo === BUILD FAILED ===
    pause
    exit /b 1
)
cd /d "%~dp0"
echo OK
echo     Build OK: soundDog\build\soundDog.hex

:: ---- 2. Enter BOOT0 ISP mode ----
echo.
echo [2/3] Entering ISP mode (manual steps):
echo.
echo     ==== MANUAL STEPS ====
echo     1. Power OFF (unplug USB)
echo     2. HOLD BOOT0 button, do not release
echo     3. Power ON (plug USB)
echo     4. Release BOOT0
echo     5. Verify UART wiring: TX-^>PA10 RX-^>PA9 GND-^>GND
echo     ======================
echo.
pause

:: ---- 3. Flash via ISP UART ----
echo.
echo [3/3] Flashing via ISP (USART1, COM4)...
"%PROG_BIN%\STM32_Programmer_CLI.exe" -c port=COM4 br=115200 -w soundDog\build\soundDog.hex -v -s
if errorlevel 1 (
    echo.
    echo === FLASH FAILED ===
    echo Check: 1. BOOT0 held during power-up  2. COM4 correct  3. TX/RX crossed
    pause
    exit /b 1
)

echo.
echo ========================================
echo Flash OK! Now:
echo   1. Power OFF
echo   2. Power ON normally (no BOOT0)
echo   3. Program starts running
echo ========================================
echo.
pause
