param(
    [string]$ReleaseRoot = ""
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Assert-Condition {
    param(
        [Parameter(Mandatory = $true)][bool]$Condition,
        [Parameter(Mandatory = $true)][string]$Message
    )

    if (-not $Condition) {
        throw $Message
    }
}

function Resolve-ReleaseRoot {
    param(
        [AllowEmptyString()][string]$RequestedPath = ""
    )

    if (-not [string]::IsNullOrWhiteSpace($RequestedPath)) {
        return (Resolve-Path -LiteralPath $RequestedPath).Path
    }

    $releaseBase = Resolve-Path ".\out\release\desktop"
    $latest = Get-ChildItem -LiteralPath $releaseBase -Directory |
        Sort-Object LastWriteTimeUtc -Descending |
        Select-Object -First 1
    if ($null -eq $latest) {
        throw "No desktop release directory found under $releaseBase"
    }
    return $latest.FullName
}

function Assert-LegacyFilesDeleted {
    $paths = @(
        "package.json",
        "pixi.toml",
        "src/entry_wasm.cpp",
        "src/entry_cli.cpp"
    )

    foreach ($path in $paths) {
        Assert-Condition -Condition (-not (Test-Path -LiteralPath $path)) -Message "Legacy path still exists: $path"
    }
}

function Assert-CMakePresetsCollapsed {
    $presetNames = (Get-Content -Raw ".\CMakePresets.json" | ConvertFrom-Json).configurePresets |
        Where-Object {
            $hiddenProperty = $_.PSObject.Properties["hidden"]
            -not ($null -ne $hiddenProperty -and [bool]$hiddenProperty.Value)
        } |
        ForEach-Object { $_.name }
    $expected = @("x64-debug", "x64-release")
    Assert-Condition -Condition (@($presetNames).Count -eq 2) -Message "CMake configure presets were not collapsed to two entries."
    foreach ($name in $expected) {
        Assert-Condition -Condition ($presetNames -contains $name) -Message "Expected preset missing: $name"
    }
}

function Assert-ReleaseArtifacts {
    param(
        [Parameter(Mandatory = $true)][string]$ResolvedReleaseRoot
    )

    $installer = Get-ChildItem -LiteralPath (Join-Path $ResolvedReleaseRoot "installer") -Filter *Setup.exe -File
    $portableStandard = Get-ChildItem -LiteralPath (Join-Path $ResolvedReleaseRoot "portable-standard") -Filter *Portable-standard.zip -File
    $portableOffline = Get-ChildItem -LiteralPath (Join-Path $ResolvedReleaseRoot "portable-offline") -Filter *Portable-offline.zip -File

    Assert-Condition -Condition (@($installer).Count -eq 1) -Message "Expected exactly one setup installer."
    Assert-Condition -Condition (@($portableStandard).Count -eq 1) -Message "Expected exactly one portable standard archive."
    Assert-Condition -Condition (@($portableOffline).Count -eq 1) -Message "Expected exactly one portable offline archive."
    Assert-Condition -Condition ($portableOffline[0].Length -gt $portableStandard[0].Length) -Message "Portable offline archive is not larger than portable standard archive."
}

function Assert-NoLegacyReferences {
    $arguments = @(
        "-n",
        "mingw-|wasm-|pixi|oniWorldApp.exe|entry_cli|entry_wasm|filter.json",
        "README.md",
        "README.zh-CN.md",
        "llmdoc/index.md",
        "llmdoc/overview/project.md",
        "llmdoc/guides/batch-filter.md",
        "llmdoc/reference/filter-config.md",
        "llmdoc/decisions/2026-04-09-tauri-desktop-refactor-plan.md",
        "scripts",
        "src-tauri",
        "src",
        "CMakePresets.json",
        "--glob",
        "!docs/superpowers/**",
        "--glob",
        "!src-tauri/target/**",
        "--glob",
        "!src-tauri/Cargo.lock",
        "--glob",
        "!scripts/verify-desktop-release.ps1"
    )

    $matches = & rg @arguments
    if ($LASTEXITCODE -eq 0 -and -not [string]::IsNullOrWhiteSpace(($matches -join "`n"))) {
        throw "Legacy references still exist:`n$($matches -join "`n")"
    }
    if ($LASTEXITCODE -gt 1) {
        throw "rg failed while scanning legacy references."
    }
}

$resolvedReleaseRoot = Resolve-ReleaseRoot -RequestedPath $ReleaseRoot
Assert-LegacyFilesDeleted
Assert-CMakePresetsCollapsed
Assert-ReleaseArtifacts -ResolvedReleaseRoot $resolvedReleaseRoot
Assert-NoLegacyReferences

Write-Host "Desktop release verification passed: $resolvedReleaseRoot"
