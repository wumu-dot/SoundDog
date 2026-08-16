@echo off
setlocal
echo ========================================
echo  SoundDog DOC-STATE 漂移检测
echo ========================================
echo.

cd /d "%~dp0.."
"C:\Program Files\Git\bin\bash.exe" scripts/check-doc-drift.sh firmware/soundDog CLAUDE.md

echo.
echo ========================================
pause
