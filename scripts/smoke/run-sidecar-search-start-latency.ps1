param(
    [string]$SidecarPath = "",
    [int]$WorldType = 13,
    [int]$Mixing = 625,
    [int]$SeedStart = 100000,
    [int]$SeedEnd = 300000
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
            progressInterval = 1000
            sampleWindowMs = 2000
            adaptiveMinWorkers = 1
            adaptiveDropThreshold = 0.12
            adaptiveDropWindows = 3
            adaptiveCooldownMs = 8000
        }
    }
}

function Measure-FirstStartedEvent {
    param(
        [Parameter(Mandatory = $true)][string]$ResolvedSidecarPath,
        [Parameter(Mandatory = $true)][hashtable]$Payload
    )

    $json = $Payload | ConvertTo-Json -Compress -Depth 20

    $process = New-Object System.Diagnostics.Process
    $process.StartInfo.FileName = $ResolvedSidecarPath
    $process.StartInfo.UseShellExecute = $false
    $process.StartInfo.RedirectStandardInput = $true
    $process.StartInfo.RedirectStandardOutput = $true
    $process.StartInfo.RedirectStandardError = $true
    $null = $process.Start()

    $stopwatch = [System.Diagnostics.Stopwatch]::StartNew()
    $process.StandardInput.WriteLine($json)
    $process.StandardInput.Close()
    $firstLine = $process.StandardOutput.ReadLine()
    $stopwatch.Stop()

    try {
        if (-not $process.HasExited) {
            $process.Kill()
        }
    }
    catch {
    }

    $stderr = ""
    try {
        $stderr = $process.StandardError.ReadToEnd().Trim()
    }
    catch {
    }

    return [pscustomobject]@{
        Mode = $Payload.cpu.mode
        FirstEventMs = [math]::Round($stopwatch.Elapsed.TotalMilliseconds, 1)
        FirstLine = $firstLine
        Stderr = $stderr
    }
}

$resolvedSidecarPath = Resolve-SidecarPath -RequestedPath $SidecarPath
$jobSuffix = [guid]::NewGuid().ToString("N")

Write-Host "[smoke] sidecar: $resolvedSidecarPath"

$results = @(
    Measure-FirstStartedEvent -ResolvedSidecarPath $resolvedSidecarPath -Payload (New-SearchPayload -Mode "balanced" -JobId "latency-balanced-$jobSuffix")
    Measure-FirstStartedEvent -ResolvedSidecarPath $resolvedSidecarPath -Payload (New-SearchPayload -Mode "turbo" -JobId "latency-turbo-$jobSuffix")
)

$results | Format-Table -AutoSize

foreach ($result in $results) {
    if ($result.FirstLine -notmatch '"event":"started"') {
        throw "Mode '$($result.Mode)' did not receive started event first. firstLine=$($result.FirstLine) stderr=$($result.Stderr)"
    }
}

