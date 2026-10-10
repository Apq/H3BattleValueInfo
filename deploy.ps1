$ErrorActionPreference = 'Stop'
# 共享部署目标：$env:H3_GAME_DIR > 仓库根 H3Env.ps1 > 内置默认值（换版本改 H3Env.ps1）。
$h3root = Split-Path -Parent $PSScriptRoot
if (Test-Path -LiteralPath "$h3root\H3Env.ps1") { . "$h3root\H3Env.ps1" }
if (-not (Get-Command Get-H3GameDir -ErrorAction SilentlyContinue)) {
    function Get-H3GameDir {
        if ($env:H3_GAME_DIR -and (Test-Path -LiteralPath $env:H3_GAME_DIR)) { $env:H3_GAME_DIR }
        else { 'D:\Heroes3\Heroes3_2026.10.09' }
    }
}
$gameDir = Get-H3GameDir
$packsDst = "$gameDir\_HD3_Data\Packs\远优对比"
$src = "$PSScriptRoot\Release"

# --- BattleValueInfo 插件 ---
if (-not (Test-Path $packsDst)) {
    New-Item -ItemType Directory -Path $packsDst -Force | Out-Null
}
Copy-Item "$src\BattleValueInfo.dll" $packsDst -Force
Copy-Item "$PSScriptRoot\BattleValueInfo.ini" $packsDst -Force
Copy-Item "$PSScriptRoot\使用说明.txt" $packsDst -Force

$imgSrc = "$PSScriptRoot\img"
$imgDst = "$packsDst\img"
if (Test-Path $imgSrc) {
    New-Item -ItemType Directory -Path $imgDst -Force | Out-Null
    Get-ChildItem "$imgSrc\*.*" -ErrorAction SilentlyContinue | ForEach-Object {
        Copy-Item $_.FullName $imgDst -Force
    }
}

Write-Host "已部署到 $packsDst"
Write-Host "BattleValueInfo 仅负责战场远程/魔法输出对比；生物框和战斗价值由 MegaDesc 负责。"
