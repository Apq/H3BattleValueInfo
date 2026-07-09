$gameDir = 'D:\Heroes3\Heroes3_2026.05.01'
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
Write-Host "注意：BattleValueInfo 依赖 MegaDesc，请先部署并启用 MegaDesc。"
