$gameDir = 'D:\Heroes3\Heroes3_2026.05.01'
$packsDst = "$gameDir\_HD3_Data\Packs\战斗价值信息"
# $PSScriptRoot 是 PowerShell 自动变量，表示当前 deploy.ps1 所在目录。
# 这里用它定位项目根目录下的 Release 输出目录，避免从不同工作目录运行脚本时路径失效。
$src = "$PSScriptRoot\..\Release"
# $PSScriptRoot 是当前 deploy.ps1 所在目录；pcx 素材目录固定在脚本同级的 pcx 子目录。
$imgSrc = "$PSScriptRoot\pcx"

# --- BattleValueInfo 插件 ---
if (-not (Test-Path $packsDst)) {
    New-Item -ItemType Directory -Path $packsDst -Force | Out-Null
}
Copy-Item "$src\BattleValueInfo.dll" $packsDst -Force
# INI 仅首次部署，避免覆盖游戏目录中已修改的配置
#if (-not (Test-Path "$packsDst\BattleValueInfo.ini")) {
    # $PSScriptRoot 是当前 deploy.ps1 所在目录；INI 与脚本同级，因此从这里复制。
    Copy-Item "$PSScriptRoot\BattleValueInfo.ini" $packsDst
#}

# --- 24-bit PCX 素材部署到插件目录\pcx ---
# 插件自己解码 3-plane 24-bit PCX（不走游戏资源系统），放插件目录下的 pcx 子目录
$imgDst = "$packsDst\pcx"
if (-not (Test-Path $imgDst)) {
    New-Item -ItemType Directory -Path $imgDst -Force | Out-Null
}
$pcxFiles = Get-ChildItem "$imgSrc\bv_*.pcx" | Where-Object {
    $_.Name -notmatch 'prev'
}
Write-Host "PCX 源目录: $imgSrc"
Write-Host "PCX 目标目录: $imgDst"
Write-Host "待复制 PCX 文件数: $($pcxFiles.Count)"
$copiedCount = 0
foreach ($f in $pcxFiles) {
    Copy-Item $f.FullName $imgDst -Force
    $copiedCount++
}
Write-Host "成功复制 PCX 文件数: $copiedCount"

Write-Host "已部署到 $packsDst"
