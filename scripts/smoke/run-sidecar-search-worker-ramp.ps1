param(
    [string]$SidecarPath = "",
    [int]$WorldType = 13,
    [int]$Mixing = 625,
    [int]$SeedStart = 100000,
    [int]$SeedEnd = 300000,
    [int]$TimeoutSeconds = 30
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
        (Join-Path $repoRoot "src-tauri\binaries\oni-sidecar.exe"),
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
        [Parameter(Mandatory = $true)][string]$Mode,
        [Parameter(Mandatory = $true)][string]$JobId
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
            required = @()
            forbidden = @()
            distance = @()
            count = @()
        }
        cpu = @{
            mode = $Mode
            workers = 0
            allowSmt = $true
            allowLowPerf = $false
            placement = "preferred"
            enableWarmup = $false
            enableAdaptiveDown = $true
            chunkSize = 64
            progressInterval = 512
            sampleWindowMs = 2000
            adaptiveMinWorkers = 1
            adaptiveDropThreshold = 0.12
            adaptiveDropWindows = 3
            adaptiveCooldownMs = 8000
        }
    }
}

function Invoke-WorkerRampSmoke {
    param(
        [Parameter(Mandatory = $true)][string]$ResolvedSidecarPath,
        [Parameter(Mandatory = $true)][string]$Mode
    )

    $jobSuffix = [guid]::NewGuid().ToString("N")
    $jobId = "worker-ramp-$Mode-$jobSuffix"
    $payload = New-SearchPayload -Mode $Mode -JobId $jobId
    $json = $payload | ConvertTo-Json -Compress -Depth 20

    $process = New-Object System.Diagnostics.Process
    $process.StartInfo.FileName = $ResolvedSidecarPath
    $process.StartInfo.UseShellExecute = $false
    $process.StartInfo.RedirectStandardInput = $true
    $process.StartInfo.RedirectStandardOutput = $true
    $process.StartInfo.RedirectStandardError = $true
    $null = $process.Start()

    $stopwatch = [System.Diagnostics.Stopwatch]::StartNew()
    $process.StandardInput.WriteLine($json)
    $process.StandardInput.Flush()

    $startedMs = $null
    $ceilingWorkers = 0
    $firstProgressWorkers = $null
    $maxObservedWorkers = 0
    $lineCount = 0
    $cancelSent = $false
    $finalEvent = ""
    $finalLine = ""

    try {
        while (-not $process.HasExited) {
            if ($stopwatch.Elapsed.TotalSeconds -ge $TimeoutSeconds -and -not $cancelSent) {
                $cancelPayload = @{
                    command = "cancel"
                    jobId = $jobId
                } | ConvertTo-Json -Compress
                $process.StandardInput.WriteLine($cancelPayload)
                $process.StandardInput.Flush()
                $cancelSent = $true
            }

            $line = $process.StandardOutput.ReadLine()
            if ($null -eq $line) {
                break
            }
            $lineCount += 1
            $finalLine = $line

            $event = $line | ConvertFrom-Json
            switch ($event.event) {
                "started" {
                    if ($null -eq $startedMs) {
                        $startedMs = [math]::Round($stopwatch.Elapsed.TotalMilliseconds, 1)
                    }
                    $ceilingWorkers = [int]$event.workerCount
                }
                "progress" {
                    $activeWorkers = [int]$event.activeWorkers
                    if ($null -eq $firstProgressWorkers) {
                        $firstProgressWorkers = $activeWorkers
                    }
                    if ($activeWorkers -gt $maxObservedWorkers) {
                        $maxObservedWorkers = $activeWorkers
                    }

                    if (-not $cancelSent -and $ceilingWorkers -gt 0 -and $maxObservedWorkers -ge $ceilingWorkers) {
                        $cancelPayload = @{
                            command = "cancel"
                            jobId = $jobId
                        } | ConvertTo-Json -Compress
                        $process.StandardInput.WriteLine($cancelPayload)
                        $process.StandardInput.Flush()
                        $cancelSent = $true
                    }
                }
                "completed" {
                    $finalEvent = "completed"
                }
                "cancelled" {
                    $finalEvent = "cancelled"
                }
                "failed" {
                    $finalEvent = "failed"
                }
            }

            if (-not [string]::IsNullOrWhiteSpace($finalEvent)) {
                break
            }
        }
    }
    finally {
        try {
            $process.StandardInput.Close()
        }
        catch {
        }

        if (-not $process.HasExited) {
            try {
                $process.Kill()
            }
            catch {
            }
        }
    }

    $stderr = ""
    try {
        $stderr = $process.StandardError.ReadToEnd().Trim()
    }
    catch {
    }

    if ([string]::IsNullOrWhiteSpace($finalEvent)) {
        $finalEvent = if ($process.HasExited) { "exited" } else { "timeout" }
    }

    return [pscustomobject]@{
        Mode = $Mode
        StartedMs = $startedMs
        CeilingWorkers = $ceilingWorkers
        FirstProgressWorkers = $firstProgressWorkers
        MaxObservedWorkers = $maxObservedWorkers
        SawRamp = ($null -ne $firstProgressWorkers -and $ceilingWorkers -gt 0 -and $firstProgressWorkers -lt $ceilingWorkers -and $maxObservedWorkers -ge $ceilingWorkers)
        FinalEvent = $finalEvent
        Lines = $lineCount
        LastLine = $finalLine
        Stderr = $stderr
    }
}

$resolvedSidecarPath = Resolve-SidecarPath -RequestedPath $SidecarPath
Write-Host "[smoke] sidecar: $resolvedSidecarPath"

$results = @(
    Invoke-WorkerRampSmoke -ResolvedSidecarPath $resolvedSidecarPath -Mode "balanced"
    Invoke-WorkerRampSmoke -ResolvedSidecarPath $resolvedSidecarPath -Mode "turbo"
)

$results | Format-Table -AutoSize

$sawAnyRamp = $false
foreach ($result in $results) {
    if ($null -eq $result.StartedMs) {
        throw "Mode '$($result.Mode)' did not emit a started event. lastLine=$($result.LastLine) stderr=$($result.Stderr)"
    }
    if ($result.CeilingWorkers -le 0) {
        throw "Mode '$($result.Mode)' did not report a valid worker ceiling. lastLine=$($result.LastLine)"
    }
    if ($result.SawRamp) {
        $sawAnyRamp = $true
        continue
    }
    if ($null -ne $result.FirstProgressWorkers -and $result.FirstProgressWorkers -lt $result.CeilingWorkers) {
        throw "Mode '$($result.Mode)' had startup headroom but did not ramp to the runtime ceiling. first=$($result.FirstProgressWorkers) max=$($result.MaxObservedWorkers) ceiling=$($result.CeilingWorkers) finalEvent=$($result.FinalEvent) stderr=$($result.Stderr)"
    }
}

if (-not $sawAnyRamp) {
    throw "No mode observed runtime worker ramping. Results: $($results | ConvertTo-Json -Compress -Depth 10)"
}
