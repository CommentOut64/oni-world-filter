param(
    [string]$SidecarPath = "",
    [int]$WorldType = 13,
    [int]$Mixing = 625,
    [int]$SeedStart = 100000,
    [int]$SeedEnd = 100020,
    [int[]]$WorkerCounts = @(1, 2, 4)
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Resolve-SidecarPath {
    param(
        [Parameter(Mandatory = $true)][string]$RequestedPath
    )

    if (-not [string]::IsNullOrWhiteSpace($RequestedPath)) {
        return (Resolve-Path -LiteralPath $RequestedPath).Path
    }

    $repoRoot = Resolve-Path (Join-Path $PSScriptRoot "..\..")
    $candidates = @(
        (Join-Path $repoRoot "out\build\mingw-debug\src\oni-sidecar.exe"),
        (Join-Path $repoRoot "out\build\mingw-release\src\oni-sidecar.exe"),
        (Join-Path $repoRoot "out\build\x64-debug\src\oni-sidecar.exe"),
        (Join-Path $repoRoot "out\build\x64-release\src\oni-sidecar.exe")
    )

    foreach ($candidate in $candidates) {
        if (Test-Path -LiteralPath $candidate) {
            return (Resolve-Path -LiteralPath $candidate).Path
        }
    }

    throw "Cannot resolve oni-sidecar.exe. Please pass -SidecarPath explicitly."
}

function New-SearchPayload {
    param(
        [Parameter(Mandatory = $true)][string]$JobId,
        [Parameter(Mandatory = $true)][int]$Workers,
        [AllowEmptyCollection()][string[]]$Required = @()
    )

    return @{
        command = "search"
        jobId = $JobId
        worldType = $WorldType
        seedStart = $SeedStart
        seedEnd = $SeedEnd
        mixing = $Mixing
        threads = 0
        constraints = @{
            required = $Required
            forbidden = @()
            distance = @()
            count = @()
        }
        cpu = @{
            mode = "custom"
            workers = $Workers
            allowSmt = $true
            allowLowPerf = $false
            placement = "preferred"
            enableWarmup = $false
            enableAdaptiveDown = $false
            chunkSize = 64
            progressInterval = 50
            sampleWindowMs = 1000
            adaptiveMinWorkers = 1
            adaptiveDropThreshold = 0.12
            adaptiveDropWindows = 3
            adaptiveCooldownMs = 8000
        }
    }
}

function Invoke-ScaleCase {
    param(
        [Parameter(Mandatory = $true)][string]$ResolvedSidecarPath,
        [Parameter(Mandatory = $true)][string]$Scenario,
        [AllowEmptyCollection()][string[]]$Required = @(),
        [Parameter(Mandatory = $true)][int]$Workers
    )

    $payload = New-SearchPayload -JobId "$Scenario-$Workers-$([guid]::NewGuid().ToString('N'))" -Workers $Workers -Required $Required

    $process = New-Object System.Diagnostics.Process
    $process.StartInfo.FileName = $ResolvedSidecarPath
    $process.StartInfo.UseShellExecute = $false
    $process.StartInfo.RedirectStandardInput = $true
    $process.StartInfo.RedirectStandardOutput = $true
    $process.StartInfo.RedirectStandardError = $true
    $null = $process.Start()

    $json = $payload | ConvertTo-Json -Compress -Depth 20
    $stopwatch = [System.Diagnostics.Stopwatch]::StartNew()
    $process.StandardInput.WriteLine($json)
    $process.StandardInput.Close()

    $stdout = $process.StandardOutput.ReadToEnd()
    $stderr = $process.StandardError.ReadToEnd().Trim()
    $process.WaitForExit()
    $stopwatch.Stop()

    $events = @()
    foreach ($line in ($stdout -split "`r?`n")) {
        $trimmed = $line.Trim()
        if ([string]::IsNullOrWhiteSpace($trimmed)) {
            continue
        }
        $events += ,($trimmed | ConvertFrom-Json)
    }

    $startedEvent = $events | Where-Object { $_.event -eq "started" } | Select-Object -First 1
    $completedEvent = $events |
        Where-Object { $_.event -eq "completed" -or $_.event -eq "failed" } |
        Select-Object -First 1

    return [pscustomobject]@{
        Scenario = $Scenario
        Workers = $Workers
        StartedWorkers = if ($startedEvent) { $startedEvent.workerCount } else { $null }
        WallMs = [math]::Round($stopwatch.Elapsed.TotalMilliseconds, 1)
        CpuMs = [math]::Round($process.TotalProcessorTime.TotalMilliseconds, 1)
        AvgSeedsPerSecond = if ($completedEvent -and $completedEvent.throughput) {
            [math]::Round([double]$completedEvent.throughput.averageSeedsPerSecond, 2)
        } else {
            $null
        }
        TotalMatches = if ($completedEvent) { $completedEvent.totalMatches } else { $null }
        FinalEvent = if ($completedEvent) { $completedEvent.event } else { $null }
        Stderr = $stderr
    }
}

$resolvedSidecarPath = Resolve-SidecarPath -RequestedPath $SidecarPath

Write-Host "[smoke] sidecar: $resolvedSidecarPath"

$scenarios = @(
    [pscustomobject]@{
        Name = "high-match"
        Required = @()
    },
    [pscustomobject]@{
        Name = "zero-match"
        Required = @("cryo_tank")
    }
)

$results = @()
foreach ($scenario in $scenarios) {
    foreach ($workers in $WorkerCounts) {
        $results += Invoke-ScaleCase `
            -ResolvedSidecarPath $resolvedSidecarPath `
            -Scenario $scenario.Name `
            -Required $scenario.Required `
            -Workers $workers
    }
}

$results | Format-Table Scenario, Workers, StartedWorkers, WallMs, CpuMs, AvgSeedsPerSecond, TotalMatches, FinalEvent -AutoSize

foreach ($result in $results) {
    if ($result.FinalEvent -ne "completed") {
        throw "Scenario '$($result.Scenario)' with $($result.Workers) workers did not complete successfully. stderr=$($result.Stderr)"
    }
    if ($null -eq $result.AvgSeedsPerSecond) {
        throw "Scenario '$($result.Scenario)' with $($result.Workers) workers did not report throughput."
    }
}
