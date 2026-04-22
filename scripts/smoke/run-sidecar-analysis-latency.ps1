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

function New-AnalyzePayload {
    param(
        [Parameter(Mandatory = $true)][string]$JobId
    )

    return @{
        command = "analyze_search_request"
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
            mode = "balanced"
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

function New-SearchPayload {
    param(
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
            mode = "balanced"
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

function Measure-FreshRequest {
    param(
        [Parameter(Mandatory = $true)][string]$ResolvedSidecarPath,
        [Parameter(Mandatory = $true)][hashtable]$Payload,
        [Parameter(Mandatory = $true)][string]$TerminalEvent
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

    $lineCount = 0
    $terminalLine = $null
    while (($line = $process.StandardOutput.ReadLine()) -ne $null) {
        $lineCount += 1
        $event = $line | ConvertFrom-Json
        if ($event.event -eq $TerminalEvent -and $event.jobId -eq $Payload.jobId) {
            $terminalLine = $line
            break
        }
    }
    $process.WaitForExit()
    $stopwatch.Stop()
    $stderr = $process.StandardError.ReadToEnd().Trim()

    return [pscustomobject]@{
        Scenario = $Payload.command
        Mode = "fresh"
        Event = $TerminalEvent
        DurationMs = [math]::Round($stopwatch.Elapsed.TotalMilliseconds, 1)
        Lines = $lineCount
        TerminalLine = $terminalLine
        ExitCode = $process.ExitCode
        Stderr = $stderr
    }
}

function Measure-WarmRequests {
    param(
        [Parameter(Mandatory = $true)][string]$ResolvedSidecarPath,
        [Parameter(Mandatory = $true)][hashtable]$CatalogPayload,
        [Parameter(Mandatory = $true)][hashtable]$AnalyzePayload,
        [Parameter(Mandatory = $true)][hashtable]$SearchPayload
    )

    $process = New-Object System.Diagnostics.Process
    $process.StartInfo.FileName = $ResolvedSidecarPath
    $process.StartInfo.UseShellExecute = $false
    $process.StartInfo.RedirectStandardInput = $true
    $process.StartInfo.RedirectStandardOutput = $true
    $process.StartInfo.RedirectStandardError = $true
    $null = $process.Start()

    function Send-And-Measure {
        param(
            [Parameter(Mandatory = $true)][hashtable]$Payload,
            [Parameter(Mandatory = $true)][string]$TerminalEvent
        )

        $json = $Payload | ConvertTo-Json -Compress -Depth 20
        $sw = [System.Diagnostics.Stopwatch]::StartNew()
        $process.StandardInput.WriteLine($json)
        $process.StandardInput.Flush()
        $lineCount = 0

        while (($line = $process.StandardOutput.ReadLine()) -ne $null) {
            $lineCount += 1
            $event = $line | ConvertFrom-Json
            if ($event.event -eq $TerminalEvent -and $event.jobId -eq $Payload.jobId) {
                $sw.Stop()
                return [pscustomobject]@{
                    Scenario = $Payload.command
                    Mode = "warm"
                    Event = $TerminalEvent
                    DurationMs = [math]::Round($sw.Elapsed.TotalMilliseconds, 1)
                    Lines = $lineCount
                    TerminalLine = $line
                }
            }
        }

        throw "Did not observe terminal event '$TerminalEvent' for job '$($Payload.jobId)'."
    }

    try {
        $catalogResult = Send-And-Measure -Payload $CatalogPayload -TerminalEvent "search_catalog"
        $analyzeResult = Send-And-Measure -Payload $AnalyzePayload -TerminalEvent "search_analysis"
        $searchResult = Send-And-Measure -Payload $SearchPayload -TerminalEvent "started"

        return @(
            $catalogResult
            $analyzeResult
            $searchResult
        )
    }
    finally {
        try {
            $process.StandardInput.Close()
        }
        catch {
        }
        try {
            if (-not $process.HasExited) {
                $process.Kill()
                $process.WaitForExit()
            }
        }
        catch {
        }
    }
}

$resolvedSidecarPath = Resolve-SidecarPath -RequestedPath $SidecarPath
$jobSuffix = [guid]::NewGuid().ToString("N")

Write-Host "[smoke] sidecar: $resolvedSidecarPath"

$freshCatalog = Measure-FreshRequest `
    -ResolvedSidecarPath $resolvedSidecarPath `
    -Payload @{ command = "get_search_catalog"; jobId = "catalog-fresh-$jobSuffix" } `
    -TerminalEvent "search_catalog"

$freshAnalyze = Measure-FreshRequest `
    -ResolvedSidecarPath $resolvedSidecarPath `
    -Payload (New-AnalyzePayload -JobId "analyze-fresh-$jobSuffix") `
    -TerminalEvent "search_analysis"

$warmResults = Measure-WarmRequests `
    -ResolvedSidecarPath $resolvedSidecarPath `
    -CatalogPayload @{ command = "get_search_catalog"; jobId = "catalog-warm-$jobSuffix" } `
    -AnalyzePayload (New-AnalyzePayload -JobId "analyze-warm-$jobSuffix") `
    -SearchPayload (New-SearchPayload -JobId "search-warm-$jobSuffix")

$results = @(
    $freshCatalog
    $freshAnalyze
    $warmResults
)

$results | Format-Table Scenario, Mode, Event, DurationMs, Lines -AutoSize

foreach ($result in $results) {
    if ([string]::IsNullOrWhiteSpace($result.TerminalLine)) {
        throw "Scenario '$($result.Scenario)' did not produce terminal event '$($result.Event)'."
    }
}
