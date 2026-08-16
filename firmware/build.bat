@echo off
setlocal
echo ========================================
echo  SoundDog Build
echo ========================================
echo.

:: ========== Build ==========
echo Building firmware...
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
echo.
echo [ OK ] Build OK
echo ELF: soundDog\build\soundDog.elf
echo HEX: soundDog\build\soundDog.hex
echo.
echo ========================================
pause
