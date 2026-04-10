param(
    [string]$BinaryPath = ".\out\build\x64-release\oniWorldApp.exe",
    [string]$BaseFilterPath = ".\tests\fixtures\filter\baseline-hot-water.json",
    [int[]]$ThreadCandidates = @(0, 2, 4, 6, 8, 12)
)

$ErrorActionPreference = "Stop"

if (-not (Test-Path $BinaryPath)) {
    throw "未找到可执行文件: $BinaryPath"
}
if (-not (Test-Path $BaseFilterPath)) {
    throw "未找到筛选配置: $BaseFilterPath"
}

$baseJson = Get-Content -Raw $BaseFilterPath | ConvertFrom-Json
$tempDir = Join-Path $env:TEMP "oni-thread-policy-benchmark"
New-Item -ItemType Directory -Force -Path $tempDir | Out-Null

$results = @()

foreach ($threads in $ThreadCandidates) {
    $case = $baseJson | ConvertTo-Json -Depth 20 | ConvertFrom-Json
    if (-not $case.cpu) {
        $case | Add-Member -NotePropertyName "cpu" -NotePropertyValue ([pscustomobject]@{})
    }
    $case.cpu.mode = if ($threads -eq 0) { "balanced" } else { "custom" }
    $case.cpu.workers = $threads
    $case.cpu.enableWarmup = $false
    $case.cpu.enableAdaptiveDown = $false
    $case.cpu.printMatches = $false
    $case.cpu.printProgress = $false
    $case.cpu.benchmarkSilent = $true

    $filterPath = Join-Path $tempDir ("benchmark-{0}.json" -f $threads)
    $case | ConvertTo-Json -Depth 20 | Set-Content -Encoding UTF8 $filterPath

    $seedStart = [int]$case.seedStart
    $seedEnd = [int]$case.seedEnd
    $totalSeeds = [Math]::Max(0, $seedEnd - $seedStart + 1)

    $sw = [System.Diagnostics.Stopwatch]::StartNew()
    $output = & $BinaryPath --filter $filterPath 2>&1
    $sw.Stop()
    $outputText = [string]::Join("`n", @($output))

    $elapsedSeconds = [Math]::Max(0.001, $sw.Elapsed.TotalSeconds)
    $seedsPerSecond = [Math]::Round($totalSeeds / $elapsedSeconds, 2)
    $matchCount = 0
    if ($outputText -match "found\s+(\d+)\s+matches") {
        $matchCount = [int]$Matches[1]
    }

    $results += [pscustomobject]@{
        mode = $case.cpu.mode
        workers = $threads
        elapsedSeconds = [Math]::Round($elapsedSeconds, 3)
        seedsPerSecond = $seedsPerSecond
        matchCount = $matchCount
    }
}

$results | Sort-Object -Property seedsPerSecond -Descending | Format-Table -AutoSize
