@echo off
chcp 65001 >nul
setlocal
echo ========================================
echo  SoundDog Build
echo ========================================
echo.

:: ---- Toolchain paths ----
set "GCC_BIN=C:\ST\STM32CubeCLT_1.21.0\GNU-tools-for-STM32\bin"
set "MINGW_BIN=C:\mingw64\bin"
set "PATH=%GCC_BIN%;%MINGW_BIN%;%PATH%"

:: ========== Build ==========
echo Building firmware...
cd /d "%~dp0soundDog"
if /i "%~1"=="clean" (
    echo [clean] Removing build dir...
    if exist build rmdir /s /q build
)
mingw32-make -j8 all
if %errorlevel% neq 0 (
    cd /d "%~dp0"
    echo.
    echo [FAIL] Build FAILED! Check errors above.
    pause
    exit /b 1
)
cd /d "%~dp0"
echo.
echo [ OK ] Build OK
echo ELF: soundDog\build\soundDog.elf
echo HEX: soundDog\build\soundDog.hex
echo.
echo ========================================
pause
