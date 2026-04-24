param(
    [string]$SeedsFile = "",
    [int]$WorldType = 13,
    [int]$Mixing = 625,
    [string[]]$SidecarPaths = @(),
    [switch]$IncludeAnalyze
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function ConvertTo-CanonicalValue {
    param(
        [Parameter(ValueFromPipeline = $true)]
        $Value
    )

    if ($null -eq $Value) {
        return $null
    }

    if ($Value -is [string] -or
        $Value -is [char] -or
        $Value -is [bool] -or
        $Value -is [byte] -or
        $Value -is [sbyte] -or
        $Value -is [int16] -or
        $Value -is [uint16] -or
        $Value -is [int32] -or
        $Value -is [uint32] -or
        $Value -is [int64] -or
        $Value -is [uint64] -or
        $Value -is [single] -or
        $Value -is [double] -or
        $Value -is [decimal]) {
        return $Value
    }

    if ($Value -is [System.Collections.IDictionary]) {
        $ordered = [ordered]@{}
        foreach ($key in ($Value.Keys | Sort-Object)) {
            $ordered[$key] = ConvertTo-CanonicalValue $Value[$key]
        }
        return [pscustomobject]$ordered
    }

    if ($Value -is [System.Collections.IEnumerable] -and -not ($Value -is [string])) {
        $items = @()
        foreach ($item in $Value) {
            $items += ,(ConvertTo-CanonicalValue $item)
        }
        return ,$items
    }

    $properties = @($Value.PSObject.Properties | Where-Object { $_.MemberType -like '*Property' })
    if ($properties.Count -gt 0) {
        $ordered = [ordered]@{}
        foreach ($property in ($properties.Name | Sort-Object)) {
            $ordered[$property] = ConvertTo-CanonicalValue $Value.$property
        }
        return [pscustomobject]$ordered
    }

    return $Value.ToString()
}

function ConvertTo-CanonicalJson {
    param(
        [Parameter(Mandatory = $true)]
        $Value
    )

    return (ConvertTo-CanonicalValue $Value | ConvertTo-Json -Compress -Depth 100)
}

function Sort-ByCanonicalJson {
    param(
        [Parameter(Mandatory = $true)]
        [AllowEmptyCollection()]
        [object[]]$Items
    )

    return @(
        $Items |
            Sort-Object -Property @{
                Expression = { ConvertTo-CanonicalJson $_ }
            }
    )
}

function Normalize-Summary {
    param(
        [Parameter(Mandatory = $true)]
        $Summary
    )

    $canonical = ConvertTo-CanonicalValue $Summary
    if ($canonical.PSObject.Properties.Name -contains "geysers") {
        $canonical.geysers = @(Sort-ByCanonicalJson -Items @($canonical.geysers))
    }
    if ($canonical.PSObject.Properties.Name -contains "traits") {
        $canonical.traits = @(Sort-ByCanonicalJson -Items @($canonical.traits))
    }
    return $canonical
}

function Normalize-PreviewEvents {
    param(
        [Parameter(Mandatory = $true)]
        [object[]]$Events
    )

    $event = $Events |
        Where-Object { $_.event -in @("preview", "failed") } |
        Select-Object -First 1

    if ($null -eq $event) {
        throw "preview 命令未返回 `preview` 或 `failed` 事件"
    }

    if ($event.event -eq "failed") {
        return [pscustomobject][ordered]@{
            event = "failed"
            message = $event.message
        }
    }

    $normalized = [ordered]@{
        event = "preview"
        worldType = $event.worldType
        seed = $event.seed
        mixing = $event.mixing
        preview = ConvertTo-CanonicalValue $event.preview
    }

    if ($normalized.preview.PSObject.Properties.Name -contains "summary") {
        $normalized.preview.summary = Normalize-Summary $normalized.preview.summary
    }

    return [pscustomobject]$normalized
}

function Normalize-SearchEvents {
    param(
        [Parameter(Mandatory = $true)]
        [object[]]$Events
    )

    $started = $Events | Where-Object { $_.event -eq "started" } | Select-Object -First 1
    $matches = @(
        $Events |
            Where-Object { $_.event -eq "match" } |
            ForEach-Object {
                [pscustomobject][ordered]@{
                    event = "match"
                    seed = $_.seed
                    summary = Normalize-Summary $_.summary
                }
            }
    )
    $matches = @($matches | Sort-Object -Property seed)

    $terminal = $Events |
        Where-Object { $_.event -in @("completed", "failed", "cancelled") } |
        Select-Object -First 1

    if ($null -eq $terminal) {
        throw "search 命令未返回终态事件"
    }

    $normalized = [ordered]@{
        command = "search"
        started = $null
        matches = $matches
        terminal = $null
    }

    if ($null -ne $started) {
        $normalized.started = [pscustomobject][ordered]@{
            event = "started"
            seedStart = $started.seedStart
            seedEnd = $started.seedEnd
            totalSeeds = $started.totalSeeds
        }
    }

    switch ($terminal.event) {
        "completed" {
            $normalized.terminal = [pscustomobject][ordered]@{
                event = "completed"
                processedSeeds = $terminal.processedSeeds
                totalSeeds = $terminal.totalSeeds
                totalMatches = $terminal.totalMatches
                stoppedByBudget = $terminal.stoppedByBudget
            }
        }
        "cancelled" {
            $normalized.terminal = [pscustomobject][ordered]@{
                event = "cancelled"
                processedSeeds = $terminal.processedSeeds
                totalSeeds = $terminal.totalSeeds
                totalMatches = $terminal.totalMatches
            }
        }
        "failed" {
            $normalized.terminal = [pscustomobject][ordered]@{
                event = "failed"
                message = $terminal.message
            }
        }
    }

    return [pscustomobject]$normalized
}

function Normalize-AnalyzeEvents {
    param(
        [Parameter(Mandatory = $true)]
        [object[]]$Events
    )

    $event = $Events |
        Where-Object { $_.event -in @("search_analysis", "failed") } |
        Select-Object -First 1

    if ($null -eq $event) {
        throw "analyze 命令未返回 `search_analysis` 或 `failed` 事件"
    }

    if ($event.event -eq "failed") {
        return [pscustomobject][ordered]@{
            event = "failed"
            message = $event.message
        }
    }

    return [pscustomobject][ordered]@{
        event = "search_analysis"
        analysis = ConvertTo-CanonicalValue $event.analysis
    }
}

function Invoke-SidecarRequest {
    param(
        [Parameter(Mandatory = $true)]
        [string]$ExecutablePath,
        [Parameter(Mandatory = $true)]
        [string]$JsonRequest
    )

    $startInfo = [System.Diagnostics.ProcessStartInfo]::new()
    $startInfo.FileName = $ExecutablePath
    $startInfo.WorkingDirectory = Split-Path -Path $ExecutablePath -Parent
    $startInfo.UseShellExecute = $false
    $startInfo.RedirectStandardInput = $true
    $startInfo.RedirectStandardOutput = $true
    $startInfo.RedirectStandardError = $true
    $startInfo.CreateNoWindow = $true

    $process = [System.Diagnostics.Process]::new()
    $process.StartInfo = $startInfo
    $null = $process.Start()

    try {
        $process.StandardInput.WriteLine($JsonRequest)
        $process.StandardInput.Close()
        $stdout = $process.StandardOutput.ReadToEnd()
        $stderr = $process.StandardError.ReadToEnd()
        $process.WaitForExit()
        return [pscustomobject]@{
            ExitCode = $process.ExitCode
            Stdout = $stdout.Trim()
            Stderr = $stderr.Trim()
        }
    } finally {
        $process.Dispose()
    }
}

function Parse-JsonLines {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Stdout
    )

    $events = @()
    foreach ($line in ($Stdout -split "`r?`n")) {
        $trimmed = $line.Trim()
        if ([string]::IsNullOrWhiteSpace($trimmed)) {
            continue
        }
        $events += ,($trimmed | ConvertFrom-Json)
    }
    return ,$events
}

function Get-SeedConfiguration {
    param(
        [AllowEmptyString()]
        [string]$SeedsFilePath = "",
        [Parameter(Mandatory = $true)]
        [int]$DefaultWorldType,
        [Parameter(Mandatory = $true)]
        [int]$DefaultMixing
    )

    if ([string]::IsNullOrWhiteSpace($SeedsFilePath)) {
        return [pscustomobject]@{
            WorldType = $DefaultWorldType
            Mixing = $DefaultMixing
            Seeds = @(
                100030,
                100123,
                100001,
                100010,
                100050,
                100111,
                100222,
                100333,
                100444,
                100555
            )
        }
    }

    if (-not (Test-Path -LiteralPath $SeedsFilePath)) {
        throw "SeedsFile 不存在: $SeedsFilePath"
    }

    $config = Get-Content -Raw -LiteralPath $SeedsFilePath | ConvertFrom-Json
    return [pscustomobject]@{
        WorldType = [int]$config.worldType
        Mixing = [int]$config.mixing
        Seeds = @($config.seeds | ForEach-Object { [int]$_ })
    }
}

function Get-SidecarExecutables {
    param(
        [Parameter(Mandatory = $true)]
        [string]$RepoRoot,
        [string[]]$ExplicitPaths
    )

    if ($ExplicitPaths.Count -gt 0) {
        $resolved = @()
        foreach ($path in $ExplicitPaths) {
            if (-not (Test-Path -LiteralPath $path)) {
                throw "显式指定的 sidecar 不存在: $path"
            }
            $resolved += ,(Resolve-Path -LiteralPath $path).Path
        }
        return @(
            $resolved |
                Sort-Object -Unique |
                ForEach-Object { Get-Item -LiteralPath $_ }
        )
    }

    return @(
        Get-ChildItem -Path $RepoRoot -Recurse -Filter "oni-sidecar.exe" |
            Where-Object { $_.Length -gt 0 } |
            Sort-Object FullName
    )
}

function Get-SearchRequestJson {
    param(
        [Parameter(Mandatory = $true)]
        [string]$JobId,
        [Parameter(Mandatory = $true)]
        [int]$WorldType,
        [Parameter(Mandatory = $true)]
        [int]$Mixing,
        [Parameter(Mandatory = $true)]
        [int]$Seed
    )

    return (@{
        command = "search"
        jobId = $JobId
        worldType = $WorldType
        seedStart = $Seed
        seedEnd = $Seed
        mixing = $Mixing
        threads = 1
        constraints = @{
            required = @()
            forbidden = @()
            distance = @()
            count = @()
        }
    } | ConvertTo-Json -Compress -Depth 20)
}

function Get-PreviewRequestJson {
    param(
        [Parameter(Mandatory = $true)]
        [string]$JobId,
        [Parameter(Mandatory = $true)]
        [int]$WorldType,
        [Parameter(Mandatory = $true)]
        [int]$Mixing,
        [Parameter(Mandatory = $true)]
        [int]$Seed
    )

    return (@{
        command = "preview"
        jobId = $JobId
        worldType = $WorldType
        seed = $Seed
        mixing = $Mixing
    } | ConvertTo-Json -Compress -Depth 10)
}

function Get-AnalyzeRequestJson {
    param(
        [Parameter(Mandatory = $true)]
        [string]$JobId,
        [Parameter(Mandatory = $true)]
        [int]$WorldType,
        [Parameter(Mandatory = $true)]
        [int]$Mixing,
        [Parameter(Mandatory = $true)]
        [int]$Seed
    )

    return (@{
        command = "analyze_search_request"
        jobId = $JobId
        worldType = $WorldType
        seedStart = $Seed
        seedEnd = $Seed
        mixing = $Mixing
        threads = 1
        constraints = @{
            required = @()
            forbidden = @()
            distance = @()
            count = @()
        }
    } | ConvertTo-Json -Compress -Depth 20)
}

function Get-ResultHash {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Text
    )

    $sha = [System.Security.Cryptography.SHA256]::Create()
    try {
        $bytes = [System.Text.Encoding]::UTF8.GetBytes($Text)
        $hash = $sha.ComputeHash($bytes)
        return ([System.BitConverter]::ToString($hash)).Replace("-", "").ToLowerInvariant()
    } finally {
        $sha.Dispose()
    }
}

function Save-CaseArtifact {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Root,
        [Parameter(Mandatory = $true)]
        [string]$CaseName,
        [Parameter(Mandatory = $true)]
        [string]$SidecarName,
        [Parameter(Mandatory = $true)]
        [AllowEmptyString()]
        [string]$Stdout,
        [Parameter(Mandatory = $true)]
        [AllowEmptyString()]
        [string]$Stderr,
        [Parameter(Mandatory = $true)]
        [string]$Normalized
    )

    $caseDir = Join-Path $Root $CaseName
    $null = New-Item -ItemType Directory -Path $caseDir -Force
    $safeName = ($SidecarName -replace '[^A-Za-z0-9._-]', '_')
    Set-Content -LiteralPath (Join-Path $caseDir "$safeName.stdout.ndjson") -Value $Stdout -Encoding UTF8
    Set-Content -LiteralPath (Join-Path $caseDir "$safeName.stderr.log") -Value $Stderr -Encoding UTF8
    Set-Content -LiteralPath (Join-Path $caseDir "$safeName.normalized.json") -Value $Normalized -Encoding UTF8
}

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$seedConfig = Get-SeedConfiguration -SeedsFilePath $SeedsFile -DefaultWorldType $WorldType -DefaultMixing $Mixing
$sidecars = Get-SidecarExecutables -RepoRoot $repoRoot -ExplicitPaths $SidecarPaths

if ($sidecars.Count -lt 2) {
    throw "至少需要 2 个 oni-sidecar.exe 才能做一致性对比"
}

$timestamp = Get-Date -Format "yyyyMMdd-HHmmss"
$reportRoot = Join-Path $repoRoot "out\reports\sidecar-consistency\$timestamp"
$null = New-Item -ItemType Directory -Path $reportRoot -Force

Write-Host "[check] found $($sidecars.Count) sidecar binaries"
foreach ($sidecar in $sidecars) {
    Write-Host ("[sidecar] {0} | mtime={1:yyyy-MM-dd HH:mm:ss} | size={2}" -f $sidecar.FullName, $sidecar.LastWriteTime, $sidecar.Length)
}

$cases = @()
foreach ($seed in $seedConfig.Seeds) {
    $cases += ,([pscustomobject]@{
        Name = "preview-seed-$seed"
        Command = "preview"
        RequestJson = Get-PreviewRequestJson -JobId "consistency-preview-$seed" -WorldType $seedConfig.WorldType -Mixing $seedConfig.Mixing -Seed $seed
    })
    $cases += ,([pscustomobject]@{
        Name = "search-seed-$seed"
        Command = "search"
        RequestJson = Get-SearchRequestJson -JobId "consistency-search-$seed" -WorldType $seedConfig.WorldType -Mixing $seedConfig.Mixing -Seed $seed
    })
    if ($IncludeAnalyze.IsPresent) {
        $cases += ,([pscustomobject]@{
            Name = "analyze-seed-$seed"
            Command = "analyze"
            RequestJson = Get-AnalyzeRequestJson -JobId "consistency-analyze-$seed" -WorldType $seedConfig.WorldType -Mixing $seedConfig.Mixing -Seed $seed
        })
    }
}

$summary = @()

foreach ($case in $cases) {
    $caseResults = @()
    foreach ($sidecar in $sidecars) {
        $invokeResult = Invoke-SidecarRequest -ExecutablePath $sidecar.FullName -JsonRequest $case.RequestJson
        $events = Parse-JsonLines -Stdout $invokeResult.Stdout
        $normalizedObject = switch ($case.Command) {
            "preview" { Normalize-PreviewEvents -Events $events }
            "search" { Normalize-SearchEvents -Events $events }
            "analyze" { Normalize-AnalyzeEvents -Events $events }
            default { throw "未知命令类型: $($case.Command)" }
        }
        $normalizedJson = ConvertTo-CanonicalJson $normalizedObject
        $hash = Get-ResultHash -Text $normalizedJson
        $sidecarSuffix = $sidecar.DirectoryName.Substring($repoRoot.Length).TrimStart('\').Replace('\', '__')
        $artifactSidecarName = $sidecar.Name + "_" + $sidecarSuffix

        Save-CaseArtifact -Root $reportRoot `
            -CaseName $case.Name `
            -SidecarName $artifactSidecarName `
            -Stdout $invokeResult.Stdout `
            -Stderr $invokeResult.Stderr `
            -Normalized $normalizedJson

        $caseResults += ,([pscustomobject]@{
            SidecarPath = $sidecar.FullName
            ExitCode = $invokeResult.ExitCode
            Hash = $hash
            NormalizedJson = $normalizedJson
        })
    }

    $baseline = $caseResults |
        Sort-Object -Property @{
            Expression = {
                (Get-Item -LiteralPath $_.SidecarPath).LastWriteTime
            }
            Descending = $true
        }, @{
            Expression = { $_.SidecarPath }
        } |
        Select-Object -First 1

    $mismatches = @(
        $caseResults | Where-Object {
            $_.ExitCode -ne $baseline.ExitCode -or $_.Hash -ne $baseline.Hash
        }
    )

    if ($mismatches.Count -eq 0) {
        Write-Host "[case] $($case.Name) -> PASS"
    } else {
        Write-Host "[case] $($case.Name) -> FAIL"
        Write-Host "[diff] baseline=$($baseline.SidecarPath)"
        foreach ($mismatch in $mismatches) {
            Write-Host "[diff] mismatch=$($mismatch.SidecarPath)"
        }
    }

    $summary += ,([pscustomobject]@{
        Case = $case.Name
        Passed = ($mismatches.Count -eq 0)
        Baseline = $baseline.SidecarPath
        Mismatches = @($mismatches | ForEach-Object { $_.SidecarPath })
    })
}

$summaryPath = Join-Path $reportRoot "summary.json"
$summary | ConvertTo-Json -Depth 20 | Set-Content -LiteralPath $summaryPath -Encoding UTF8

$passCount = @($summary | Where-Object { $_.Passed }).Count
$failCount = $summary.Count - $passCount

Write-Host ""
Write-Host "[summary] total=$($summary.Count) pass=$passCount fail=$failCount"
Write-Host "[summary] report=$reportRoot"

if ($failCount -gt 0) {
    exit 1
}
