@echo off
rem ===== CodeGraph wrapper (TraeCode sandbox can't create global shim, use node directly) =====
rem Usage: .skills\codegraph.bat status | index | explore "..." | callers FUNC | ...

set "CG_SHIM=C:\Users\wumu2\.local\nodejs\node-v24.19.0-win-x64\node_modules\@colbymchenry\codegraph\npm-shim.js"

if not exist "%CG_SHIM%" (
    echo [ERR] CodeGraph shim not found at %CG_SHIM%
    echo       Check Node install path or reinstall: npm i -g @colbymchenry/codegraph
    exit /b 1
)

node "%CG_SHIM%" %*
