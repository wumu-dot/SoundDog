@echo off
rem ===== OpenCLI wrapper (TraeCode sandbox can't create global shim, use node directly) =====
rem Usage: .skills\opencli.bat list | hackernews top --limit 5 | ...

set "OC_MAIN=C:\Users\wumu2\.local\nodejs\node-v24.19.0-win-x64\node_modules\@jackwener\opencli\dist\src\main.js"

if not exist "%OC_MAIN%" (
    echo [ERR] OpenCLI main not found at %OC_MAIN%
    echo       Check Node install path or reinstall: npm i -g @jackwener/opencli
    exit /b 1
)

node "%OC_MAIN%" %*
