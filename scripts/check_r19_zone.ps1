# check_r19_zone.ps1 — R19 禁区 pre-commit 检查（2026-08-30，rules.md §6）
# 用法：
#   powershell -File scripts/check_r19_zone.ps1              # 检查 staged 文件（pre-commit 调用）
#   powershell -File scripts/check_r19_zone.ps1 -InstallHook # 安装/重建 .git/hooks/pre-commit（串接 R19+R27 双检查）
# 判据：
#   Core/ Drivers/ lvgl/ Middlewares/ 新增文件 = 放行（vendored 合法流程）
#   修改/删除已有库文件 = 拦截（R19：禁止修改底层库核心逻辑；例外走 --no-verify 并在提交说明登记理由）
param([switch]$InstallHook)

$zonePattern = '^(firmware/soundDog/(Core|Drivers|lvgl|Middlewares)/|Core/|Drivers/|lvgl/|Middlewares/)'

if ($InstallHook) {
    $hookPath = ".git/hooks/pre-commit"
    # 首行必须 shebang：Git for Windows 的 sh 无法 spawn 无 shebang 的裸命令行脚本
    # 串接双检查：R19 先跑（安全红线优先），R27 三源后跑，任一失败 exit 1 拦截提交
    $hook = "#!/bin/sh`n" +
            "powershell -NoProfile -ExecutionPolicy Bypass -File `"scripts/check_r19_zone.ps1`" || exit 1`n" +
            "powershell -NoProfile -ExecutionPolicy Bypass -File `"scripts/check_r27_research.ps1`" || exit 1`n"
    Set-Content -Path $hookPath -Value $hook -Encoding ascii
    Write-Host "[OK] pre-commit hook installed (R19 zone + R27 research): $hookPath"
    exit 0
}

# 取 staged 变更（含新增/修改/删除），状态码：A=新增 M=修改 D=删除
$staged = git diff --cached --name-status 2>$null
if (-not $staged) { exit 0 }  # 无 staged 内容（如空提交），放行

$violations = @()
foreach ($line in $staged) {
    $status = $line.Substring(0, 1)
    $file = $line.Substring(1).Trim()
    if ($file -match $zonePattern) {
        if ($status -eq 'A') { continue }  # 新增 vendored 文件 = 合法（R27 流程）
        $violations += "  [$status] $file"
    }
}

if ($violations.Count -gt 0) {
    Write-Host "[R19 BLOCKED] 禁止修改底层库文件核心逻辑（rules.md §2/R19）：" -ForegroundColor Red
    $violations | ForEach-Object { Write-Host $_ -ForegroundColor Red }
    Write-Host "豁免路径：hal_msp.c 引脚配置 / CubeMX 生成初始化代码调整（须在提交说明登记）。" -ForegroundColor Yellow
    Write-Host "确属必要的库修改：git commit --no-verify 并在提交说明中写明理由 + 影响标注。" -ForegroundColor Yellow
    exit 1
}

exit 0
