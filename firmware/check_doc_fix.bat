@echo off
setlocal
echo ========================================
echo  SoundDog DOC-STATE 自动修复
echo ========================================
echo.

cd /d "%~dp0.."
"C:\Program Files\Git\bin\bash.exe" scripts/check-doc-drift.sh --fix firmware/soundDog CLAUDE.md

echo.
echo ========================================
pause
