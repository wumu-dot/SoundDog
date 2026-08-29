@echo off
chcp 65001 >nul
setlocal
echo ========================================
echo  SoundDog Build ^& Flash
echo ========================================
echo.

:: ---- Toolchain paths ----
set "GCC_BIN=C:\ST\STM32CubeCLT_1.21.0\GNU-tools-for-STM32\bin"
set "MINGW_BIN=C:\mingw64\bin"
set "PATH=%GCC_BIN%;%MINGW_BIN%;%PATH%"

:: ========== 1. Build ==========
echo [1/2] Building firmware...
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
echo [ OK ] Build OK

:: ========== 2. Flash ==========
echo.
echo [2/2] Flashing via ST-Link (SWD)...
openocd -f openocd.cfg -c "program soundDog/build/soundDog.elf verify reset exit"
if %errorlevel% neq 0 (
    echo.
    echo [FAIL] Flash FAILED! Check ST-Link connection.
    pause
    exit /b 1
)

echo.
echo ========================================
echo [ OK ] Build ^& Flash complete!
echo Device is now running.
echo ========================================
pause
