param(
    [string]$BinaryPath = ".\out\build\x64-release\oniWorldApp.exe",
    [string]$FilterPath = ".\tests\fixtures\filter\baseline-hot-water.json",
    [string]$BuildName = "x64-release"
)

$ErrorActionPreference = "Stop"

if (-not (Test-Path $BinaryPath)) {
    throw "未找到可执行文件: $BinaryPath"
}
if (-not (Test-Path $FilterPath)) {
    throw "未找到筛选配置: $FilterPath"
}

$filter = Get-Content -Raw $FilterPath | ConvertFrom-Json
$seedStart = [int]$filter.seedStart
$seedEnd = [int]$filter.seedEnd
$worldType = [int]$filter.worldType
$mixing = [int]$filter.mixing

$sw = [System.Diagnostics.Stopwatch]::StartNew()
$output = & $BinaryPath --filter $FilterPath 2>&1
$sw.Stop()
$outputText = [string]::Join("`n", @($output))

$elapsedSeconds = [Math]::Max(0.001, $sw.Elapsed.TotalSeconds)
$totalSeeds = [Math]::Max(0, $seedEnd - $seedStart + 1)
$seedsPerSecond = [Math]::Round($totalSeeds / $elapsedSeconds, 2)

$matchCount = 0
if ($outputText -match "found\s+(\d+)\s+matches") {
    $matchCount = [int]$Matches[1]
}

Write-Host "build: $BuildName"
Write-Host "worldType: $worldType"
Write-Host "mixing: $mixing"
Write-Host "seedRange: $seedStart ~ $seedEnd"
Write-Host ("elapsedSeconds: {0:N3}" -f $elapsedSeconds)
Write-Host ("seedsPerSecond: {0:N2}" -f $seedsPerSecond)
Write-Host "matchCount: $matchCount"
