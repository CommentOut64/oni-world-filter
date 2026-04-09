param(
    [string]$ExecutablePath = "out/build/mingw-release/src/oniWorldApp.exe",
    [string]$BaseFilterPath = "filter.json",
    [string[]]$Modes = @("standard"),
    [int]$RunsPerMode = 3,
    [string]$OutputDir = "out/benchmarks",
    [bool]$BenchmarkSilent = $true
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Get-CpuCounts {
    $logical = 0
    $physical = 0
    try {
        $cpus = Get-CimInstance -ClassName Win32_Processor -ErrorAction Stop
        foreach ($cpu in $cpus) {
            $logical += [int]$cpu.NumberOfLogicalProcessors
            $physical += [int]$cpu.NumberOfCores
        }
    } catch {
        Write-Warning "Failed to query Win32_Processor, fallback to Environment.ProcessorCount."
    }

    if ($logical -le 0) {
        $logical = [Environment]::ProcessorCount
    }
    if ($physical -le 0) {
        $physical = [Math]::Max(1, [int][Math]::Floor($logical / 2))
    }
    if ($physical -gt $logical) {
        $physical = $logical
    }
    return [pscustomobject]@{
        logical  = $logical
        physical = $physical
    }
}

function New-ModeSpec {
    param(
        [Parameter(Mandatory = $true)][string]$Label,
        [Parameter(Mandatory = $true)][string]$Mode,
        [int]$Workers = 0,
        [bool]$AllowSmt = $true,
        [bool]$AllowLowPerf = $true,
        [string]$Placement = "preferred"
    )
    return [pscustomobject]@{
        label = $Label
        mode = $Mode
        workers = $Workers
        allow_smt = $AllowSmt
        allow_low_perf = $AllowLowPerf
        placement = $Placement
    }
}

function Expand-ModeSpecs {
    param(
        [string[]]$RequestedModes,
        [pscustomobject]$CpuCounts
    )

    $tokens = [System.Collections.Generic.List[string]]::new()
    foreach ($modeArg in $RequestedModes) {
        foreach ($token in ($modeArg -split ",")) {
            $item = $token.Trim().ToLowerInvariant()
            if (-not [string]::IsNullOrWhiteSpace($item)) {
                $tokens.Add($item)
            }
        }
    }
    if ($tokens.Count -eq 0) {
        throw "No benchmark modes provided."
    }

    $specs = [System.Collections.Generic.List[object]]::new()
    $dedup = [System.Collections.Generic.HashSet[string]]::new()
    $addSpec = {
        param([pscustomobject]$Spec)
        if ($dedup.Add($Spec.label)) {
            $specs.Add($Spec)
        }
    }

    foreach ($token in $tokens) {
        if ($token -eq "standard") {
            & $addSpec (New-ModeSpec -Label "conservative" -Mode "conservative")
            & $addSpec (New-ModeSpec -Label "balanced" -Mode "balanced")
            & $addSpec (New-ModeSpec -Label "turbo" -Mode "turbo")
            & $addSpec (New-ModeSpec -Label "custom-physical" -Mode "custom" -Workers $CpuCounts.physical -AllowSmt $false -AllowLowPerf $false)
            & $addSpec (New-ModeSpec -Label "custom-logical" -Mode "custom" -Workers $CpuCounts.logical -AllowSmt $true -AllowLowPerf $true)
            continue
        }

        if ($token -match '^custom:(\d+)$') {
            $workers = [int]$Matches[1]
            & $addSpec (New-ModeSpec -Label "custom-$workers" -Mode "custom" -Workers $workers -AllowSmt $true -AllowLowPerf $true)
            continue
        }
        if ($token -match '^custom-nosmt:(\d+)$') {
            $workers = [int]$Matches[1]
            & $addSpec (New-ModeSpec -Label "custom-nosmt-$workers" -Mode "custom" -Workers $workers -AllowSmt $false -AllowLowPerf $false)
            continue
        }
        if ($token -in @("conservative", "balanced", "turbo", "custom")) {
            & $addSpec (New-ModeSpec -Label $token -Mode $token)
            continue
        }

        throw "Unsupported mode token: $token"
    }

    if ($specs.Count -eq 0) {
        throw "No valid benchmark mode specs resolved."
    }
    return $specs
}

if (-not (Test-Path -LiteralPath $ExecutablePath)) {
    throw "Executable not found: $ExecutablePath"
}
if (-not (Test-Path -LiteralPath $BaseFilterPath)) {
    throw "Filter json not found: $BaseFilterPath"
}
if ($RunsPerMode -lt 1) {
    throw "RunsPerMode must be >= 1."
}

$cpuCounts = Get-CpuCounts
$modeSpecs = Expand-ModeSpecs -RequestedModes $Modes -CpuCounts $cpuCounts

$timestamp = Get-Date -Format "yyyyMMdd-HHmmss"
New-Item -ItemType Directory -Force -Path $OutputDir | Out-Null
$tempDir = Join-Path $OutputDir "tmp-$timestamp"
New-Item -ItemType Directory -Force -Path $tempDir | Out-Null

$baseJsonText = Get-Content -LiteralPath $BaseFilterPath -Raw
$results = [System.Collections.Generic.List[object]]::new()

Write-Host ("CPU detected: physical={0}, logical={1}" -f $cpuCounts.physical, $cpuCounts.logical)
Write-Host ("Benchmark modes: {0}" -f (($modeSpecs | ForEach-Object { $_.label }) -join ", "))

foreach ($spec in $modeSpecs) {
    for ($run = 1; $run -le $RunsPerMode; $run++) {
        $cfg = $baseJsonText | ConvertFrom-Json
        if (-not ($cfg.PSObject.Properties.Name -contains "cpu")) {
            $cfg | Add-Member -NotePropertyName cpu -NotePropertyValue ([pscustomobject]@{})
        }

        $cfg.cpu | Add-Member -NotePropertyName mode -NotePropertyValue $spec.mode -Force
        $cfg.cpu | Add-Member -NotePropertyName printDiagnostics -NotePropertyValue $false -Force
        $cfg.cpu | Add-Member -NotePropertyName benchmarkSilent -NotePropertyValue $BenchmarkSilent -Force
        if ($BenchmarkSilent) {
            $cfg.cpu | Add-Member -NotePropertyName printMatches -NotePropertyValue $false -Force
            $cfg.cpu | Add-Member -NotePropertyName printProgress -NotePropertyValue $false -Force
        }

        if (-not ($cfg.cpu.PSObject.Properties.Name -contains "enableWarmup")) {
            $cfg.cpu | Add-Member -NotePropertyName enableWarmup -NotePropertyValue $true -Force
        }
        if (-not ($cfg.cpu.PSObject.Properties.Name -contains "enableAdaptiveDown")) {
            $cfg.cpu | Add-Member -NotePropertyName enableAdaptiveDown -NotePropertyValue $true -Force
        }

        if ($spec.mode -eq "custom") {
            $workers = [Math]::Max(1, [int]$spec.workers)
            $cfg.cpu | Add-Member -NotePropertyName workers -NotePropertyValue $workers -Force
            $cfg.cpu | Add-Member -NotePropertyName allowSmt -NotePropertyValue $spec.allow_smt -Force
            $cfg.cpu | Add-Member -NotePropertyName allowLowPerf -NotePropertyValue $spec.allow_low_perf -Force
            $cfg.cpu | Add-Member -NotePropertyName placement -NotePropertyValue $spec.placement -Force
        }

        $tempFilter = Join-Path $tempDir ("{0}-run{1}.json" -f $spec.label, $run)
        $cfg | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath $tempFilter -Encoding UTF8

        Write-Host ("[{0}][{1}/{2}] running ..." -f $spec.label, $run, $RunsPerMode)
        $stopwatch = [System.Diagnostics.Stopwatch]::StartNew()
        $output = & $ExecutablePath --filter $tempFilter
        $stopwatch.Stop()

        $summaryLine = $output | Select-String -Pattern "Throughput summary:" | Select-Object -Last 1
        $doneLine = $output | Select-String -Pattern "^Done\. Scanned " | Select-Object -Last 1

        $avg = [double]::NaN
        $stdev = [double]::NaN
        $activeWorkers = -1
        $fallbackCount = -1
        if ($summaryLine) {
            $m = [regex]::Match(
                $summaryLine.Line,
                "avg=([0-9]+(?:\.[0-9]+)?)\s+seeds/s,\s+stdev=([0-9]+(?:\.[0-9]+)?),\s+active_workers=([0-9]+),\s+fallback_count=([0-9]+)")
            if ($m.Success) {
                $avg = [double]$m.Groups[1].Value
                $stdev = [double]$m.Groups[2].Value
                $activeWorkers = [int]$m.Groups[3].Value
                $fallbackCount = [int]$m.Groups[4].Value
            }
        }

        $scanned = -1
        $total = -1
        $matches = -1
        if ($doneLine) {
            $m = [regex]::Match($doneLine.Line, "Done\. Scanned ([0-9]+)/([0-9]+) seeds, found ([0-9]+) matches\.")
            if ($m.Success) {
                $scanned = [int]$m.Groups[1].Value
                $total = [int]$m.Groups[2].Value
                $matches = [int]$m.Groups[3].Value
            }
        }

        $results.Add([pscustomobject]@{
                mode_label = $spec.label
                mode = $spec.mode
                workers = [int]$spec.workers
                allow_smt = [bool]$spec.allow_smt
                allow_low_perf = [bool]$spec.allow_low_perf
                run = $run
                elapsed_sec = [Math]::Round($stopwatch.Elapsed.TotalSeconds, 3)
                scanned = $scanned
                total = $total
                matches = $matches
                avg_seeds_per_sec = $avg
                stdev_seeds_per_sec = $stdev
                active_workers = $activeWorkers
                fallback_count = $fallbackCount
            })
    }
}

$csvPath = Join-Path $OutputDir "benchmark-$timestamp.csv"
$jsonPath = Join-Path $OutputDir "benchmark-$timestamp.json"
$results | Export-Csv -LiteralPath $csvPath -NoTypeInformation -Encoding UTF8
$results | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $jsonPath -Encoding UTF8

$summary = [System.Collections.Generic.List[object]]::new()
foreach ($group in ($results | Group-Object mode_label)) {
    $rows = @($group.Group)
    $validRows = @($rows | Where-Object { -not [double]::IsNaN($_.avg_seeds_per_sec) })
    $validRuns = $validRows.Count

    $meanAvg = [double]::NaN
    $minAvg = [double]::NaN
    $maxAvg = [double]::NaN
    if ($validRuns -gt 0) {
        $avgMeasure = $validRows | Measure-Object -Property avg_seeds_per_sec -Average -Minimum -Maximum
        $meanAvg = [Math]::Round([double]$avgMeasure.Average, 3)
        $minAvg = [Math]::Round([double]$avgMeasure.Minimum, 3)
        $maxAvg = [Math]::Round([double]$avgMeasure.Maximum, 3)
    }

    $elapsedMeasure = $rows | Measure-Object -Property elapsed_sec -Average
    $fallbackMeasure = $rows | Measure-Object -Property fallback_count -Average
    $summary.Add([pscustomobject]@{
            mode_label = $group.Name
            mode = $rows[0].mode
            workers = $rows[0].workers
            allow_smt = $rows[0].allow_smt
            allow_low_perf = $rows[0].allow_low_perf
            runs = $rows.Count
            valid_runs = $validRuns
            mean_avg_seeds_per_sec = $meanAvg
            min_avg_seeds_per_sec = $minAvg
            max_avg_seeds_per_sec = $maxAvg
            mean_elapsed_sec = [Math]::Round([double]$elapsedMeasure.Average, 3)
            mean_fallback_count = [Math]::Round([double]$fallbackMeasure.Average, 3)
        })
}

$summaryCsvPath = Join-Path $OutputDir "benchmark-$timestamp-summary.csv"
$summaryJsonPath = Join-Path $OutputDir "benchmark-$timestamp-summary.json"
$summary | Export-Csv -LiteralPath $summaryCsvPath -NoTypeInformation -Encoding UTF8
$summary | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $summaryJsonPath -Encoding UTF8

Write-Host "Benchmark finished."
Write-Host "CSV : $csvPath"
Write-Host "JSON: $jsonPath"
Write-Host "Summary CSV : $summaryCsvPath"
Write-Host "Summary JSON: $summaryJsonPath"
Write-Host ""
Write-Host "Mode Summary:"
$summary | Sort-Object -Property mean_avg_seeds_per_sec -Descending | Format-Table -AutoSize
