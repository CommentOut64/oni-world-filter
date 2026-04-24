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

function Assert-TauriOverlayFilesPresent {
    $paths = @(
        ".\src-tauri\tauri.standard.conf.json",
        ".\src-tauri\tauri.offline.conf.json"
    )

    foreach ($path in $paths) {
        Assert-Condition -Condition (Test-Path -LiteralPath $path) -Message "Missing Tauri overlay config: $path"
    }
}

function Assert-ReleaseArtifacts {
    param(
        [Parameter(Mandatory = $true)][string]$ResolvedReleaseRoot
    )

    $standard = Get-ChildItem -LiteralPath (Join-Path $ResolvedReleaseRoot "standard") -Filter *.exe -File
    $offline = Get-ChildItem -LiteralPath (Join-Path $ResolvedReleaseRoot "offline") -Filter *.exe -File

    Assert-Condition -Condition (@($standard).Count -eq 1) -Message "Expected exactly one standard NSIS installer."
    Assert-Condition -Condition (@($offline).Count -eq 1) -Message "Expected exactly one offline NSIS installer."
    Assert-Condition -Condition ($offline[0].Length -gt $standard[0].Length) -Message "Offline installer is not larger than standard installer."
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
Assert-TauriOverlayFilesPresent
Assert-ReleaseArtifacts -ResolvedReleaseRoot $resolvedReleaseRoot
Assert-NoLegacyReferences

Write-Host "Desktop release verification passed: $resolvedReleaseRoot"
