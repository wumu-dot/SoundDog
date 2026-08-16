@echo off
setlocal
echo ========================================
echo  SoundDog Build ^& Flash
echo ========================================
echo.

:: ========== 1. Build ==========
echo [1/2] Building firmware...
cd /d "%~dp0soundDog"
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
