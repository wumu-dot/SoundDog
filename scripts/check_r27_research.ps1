# check_r27_research.ps1 — R27 预研三源 pre-commit 检查（2026-08-30，rules.md §6）
# 触发条件：staged 代码（firmware/soundDog 下非库区 .c/.h）新增了 HEAD 中 soundDog 树
#           从未用过的 arm_* API 符号（新调用/新类型均算新 API 面）。
# 检查要求：同一提交的 staged 文档/代码注释新增行须含三源证据标记——
#   源① "源码"       （被调函数源码阅读记录：行号/官方源码副作用结论）
#   源② "issue"      （忽略大小写；官方/参考仓库 Issue 区调研）
#   源③ "坑"+"预防"  （坑清单交付物：坑→后果→预防→验证判据）
# 豁免：新增符号在 HEAD 的 soundDog 树已出现（复用 R8）；机械改动无新 API 自然放行（§5）。
# 逃生口：git commit --no-verify 并在提交说明登记理由（与 R19 钩子同口径）。
# 安装：powershell -File scripts/check_r19_zone.ps1 -InstallHook
#       （重建 pre-commit，串接 R19 禁区 + R27 三源两个检查）

# 库区（R19 域：vendored 新增合法，不由本钩子管）
$zonePattern = '^(firmware/soundDog/(Core|Drivers|lvgl|Middlewares)/|Core/|Drivers/|lvgl/|Middlewares/)'
$codePattern  = '^firmware/soundDog/.*\.(c|h)$'

$staged = git diff --cached --name-status 2>$null
if (-not $staged) { exit 0 }  # 无 staged 内容，放行

# 收集 staged 代码文件（非库区）
$codeFiles = @()
foreach ($line in $staged) {
    $file = ($line.Substring(1).Trim() -split "`t")[-1]
    if (($file -match $codePattern) -and ($file -notmatch $zonePattern)) { $codeFiles += $file }
}
if ($codeFiles.Count -eq 0) { exit 0 }

# 收集代码新增行（含注释——R27 允许出处写代码注释或 FEAT 文档）
$addedCodeText = ""
foreach ($f in $codeFiles) {
    $lines = git diff --cached -U0 -- $f 2>$null
    foreach ($l in $lines) {
        if ($l -match '^\+[^+]') { $addedCodeText += $l + "`n" }
    }
}
if (-not $addedCodeText) { exit 0 }

# 提取新 API 符号：arm_* 且 HEAD 的 soundDog 树从未出现过
$seen = @{}
$newSymbols = @()
foreach ($m in [regex]::Matches($addedCodeText, '\barm_[a-z0-9_]+')) {
    $sym = $m.Value
    if ($seen.ContainsKey($sym)) { continue }
    $seen[$sym] = $true
    git grep -q -F -e $sym HEAD -- firmware/soundDog 2>$null
    if ($LASTEXITCODE -ne 0) { $newSymbols += $sym }
}
if ($newSymbols.Count -eq 0) { exit 0 }

# 三源证据：代码注释新增行 + staged docs 新增行，合并检查
$evidenceText = $addedCodeText
foreach ($line in $staged) {
    $file = ($line.Substring(1).Trim() -split "`t")[-1]
    if ($file -match '^docs/.*\.md$') {
        $lines = git diff --cached -U0 -- $file 2>$null
        foreach ($l in $lines) {
            if ($l -match '^\+[^+]') { $evidenceText += $l + "`n" }
        }
    }
}

$missing = @()
if ($evidenceText -notmatch '源码')  { $missing += '源① 源码内部行为（被调函数源码阅读记录）' }
if ($evidenceText -notmatch '(?i)issue') { $missing += '源② Issue 区调研（官方/参考仓库 issues）' }
if (-not (($evidenceText -match '坑') -and ($evidenceText -match '预防'))) {
    $missing += '源③ 坑清单（坑→后果→预防→验证判据）'
}

if ($missing.Count -gt 0) {
    Write-Host "[R27 BLOCKED] 新增第三方 API 调用，但预研三源证据缺失（rules.md §4 R27）：" -ForegroundColor Red
    Write-Host "  新 API 符号：$($newSymbols -join ', ')" -ForegroundColor Red
    foreach ($m3 in $missing) { Write-Host "  缺 $m3" -ForegroundColor Red }
    Write-Host "修复：预研证据（源码阅读/Issue 编号/坑清单）写入本提交 FEAT 文档的新增行，文档与代码同提交。" -ForegroundColor Yellow
    Write-Host "确属豁免（机械改动/证据在历史提交）：git commit --no-verify 并在提交说明登记理由。" -ForegroundColor Yellow
    exit 1
}

exit 0
