param(
    [ValidateSet("standard", "offline", "both")]
    [string]$Variant = "both",
    [ValidateSet("unsigned")]
    [string]$SigningProfile = "unsigned",
    [switch]$SkipYarnInstall
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$repoRoot = Resolve-Path (Join-Path $PSScriptRoot "..")
. (Join-Path $PSScriptRoot "lib/desktop-bootstrap.ps1")

function Get-RequestedVariants {
    param(
        [Parameter(Mandatory = $true)][string]$Variant
    )

    switch ($Variant) {
        "standard" { return @("standard") }
        "offline" { return @("offline") }
        "both" { return @("standard", "offline") }
        default { throw "Unsupported variant: $Variant" }
    }
}

function Get-VariantConfigPath {
    param(
        [Parameter(Mandatory = $true)][ValidateSet("standard", "offline")][string]$Variant,
        [Parameter(Mandatory = $true)][string]$RepoRoot
    )

    switch ($Variant) {
        "standard" { return Join-Path $RepoRoot "src-tauri/tauri.standard.conf.json" }
        "offline" { return Join-Path $RepoRoot "src-tauri/tauri.offline.conf.json" }
    }
}

function Clear-TauriBundleOutput {
    param(
        [Parameter(Mandatory = $true)][string]$RepoRoot
    )

    $bundleDir = Join-Path $RepoRoot "src-tauri/target/release/bundle"
    if (Test-Path -LiteralPath $bundleDir) {
        Remove-Item -LiteralPath $bundleDir -Recurse -Force
    }
}

function Invoke-TauriBuildVariant {
    param(
        [Parameter(Mandatory = $true)][ValidateSet("standard", "offline")][string]$Variant,
        [Parameter(Mandatory = $true)][string]$RepoRoot
    )

    $configPath = Get-VariantConfigPath -Variant $Variant -RepoRoot $RepoRoot
    if (-not (Test-Path -LiteralPath $configPath)) {
        throw "Tauri overlay config not found: $configPath"
    }

    Clear-TauriBundleOutput -RepoRoot $RepoRoot

    Push-Location (Join-Path $RepoRoot "src-tauri")
    try {
        cargo tauri build --config $configPath
        if ($LASTEXITCODE -ne 0) {
            throw "cargo tauri build --config $configPath failed with exit code $LASTEXITCODE"
        }
    } finally {
        Pop-Location
    }
}

function Publish-VariantArtifacts {
    param(
        [Parameter(Mandatory = $true)][ValidateSet("standard", "offline")][string]$Variant,
        [Parameter(Mandatory = $true)][string]$RepoRoot,
        [Parameter(Mandatory = $true)][string]$ReleaseRoot,
        [Parameter(Mandatory = $true)][string]$Version
    )

    $sourceDirectory = Join-Path $RepoRoot "src-tauri/target/release/bundle/nsis"
    $variantDirectory = Join-Path $ReleaseRoot $Variant
    if (Test-Path -LiteralPath $variantDirectory) {
        Remove-Item -LiteralPath $variantDirectory -Recurse -Force
    }
    New-Item -ItemType Directory -Path $variantDirectory | Out-Null

    $collected = @(Collect-ReleaseArtifacts -SourceDirectory $sourceDirectory -DestinationDirectory $variantDirectory -Patterns @("*.exe"))
    if ($collected.Count -ne 1) {
        throw "Expected exactly one NSIS installer for variant '$Variant', but found $($collected.Count)"
    }

    $sourcePath = $collected[0]
    $extension = [System.IO.Path]::GetExtension($sourcePath)
    $finalName = "oni-world-filter-$Version-$Variant-nsis$extension"
    $finalPath = Join-Path $variantDirectory $finalName
    Move-Item -LiteralPath $sourcePath -Destination $finalPath -Force

    return $finalPath
}

function Write-ReleaseChecksums {
    param(
        [Parameter(Mandatory = $true)][string]$ReleaseRoot,
        [Parameter(Mandatory = $true)][string[]]$Files
    )

    $lines = foreach ($file in $Files) {
        $hash = Get-FileHash -LiteralPath $file -Algorithm SHA256
        $relativePath = Get-ReleaseRelativePath -ReleaseRoot $ReleaseRoot -Path $file
        "{0} *{1}" -f $hash.Hash.ToLowerInvariant(), $relativePath.Replace("\", "/")
    }
    $checksumsPath = Join-Path $ReleaseRoot "checksums.txt"
    Set-Content -LiteralPath $checksumsPath -Value $lines -Encoding ascii
    return $checksumsPath
}

function Get-ReleaseRelativePath {
    param(
        [Parameter(Mandatory = $true)][string]$ReleaseRoot,
        [Parameter(Mandatory = $true)][string]$Path
    )

    $root = [System.IO.Path]::GetFullPath($ReleaseRoot)
    $target = [System.IO.Path]::GetFullPath($Path)
    if (-not $target.StartsWith($root, [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "Path '$target' is outside release root '$root'"
    }

    $relative = $target.Substring($root.Length).TrimStart('\', '/')
    if ([string]::IsNullOrWhiteSpace($relative)) {
        throw "Relative path resolution failed for '$target'"
    }
    return $relative
}

function Write-BuildSummary {
    param(
        [Parameter(Mandatory = $true)][string]$ReleaseRoot,
        [Parameter(Mandatory = $true)][string]$Version,
        [Parameter(Mandatory = $true)][string]$SigningProfile,
        [Parameter(Mandatory = $true)][string[]]$Artifacts
    )

    $summary = [ordered]@{
        version = $Version
        builtAtUtc = (Get-Date).ToUniversalTime().ToString("o")
        signingProfile = $SigningProfile
        artifacts = @()
    }

    foreach ($artifact in $Artifacts) {
        $relativePath = (Get-ReleaseRelativePath -ReleaseRoot $ReleaseRoot -Path $artifact).Replace("\", "/")
        $item = [ordered]@{
            relativePath = $relativePath
            sizeBytes = (Get-Item -LiteralPath $artifact).Length
        }
        $summary.artifacts += $item
    }

    $summaryPath = Join-Path $ReleaseRoot "build-summary.json"
    $summary | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $summaryPath -Encoding utf8
    return $summaryPath
}

Push-Location $repoRoot
try {
    $requestedVariants = Get-RequestedVariants -Variant $Variant

    Sync-DesktopVersion -RepoRoot $repoRoot
    Assert-VersionConsistency -RepoRoot $repoRoot
    Assert-DesktopPackageIdentity -RepoRoot $repoRoot
    Assert-VsToolchain
    Assert-NodeAndYarn
    Assert-RustAndCargoTauri
    Repair-StaleCMakeCache

    if (-not $SkipYarnInstall) {
        Write-Host "Installing desktop dependencies..."
        Invoke-Yarn -Args @("--cwd", "desktop", "install", "--frozen-lockfile")
    }

    Write-Host "Building desktop frontend..."
    Invoke-Yarn -Args @("--cwd", "desktop", "build")

    Build-AndSyncSidecar -RepoRoot $repoRoot -Configuration Release | Out-Null

    $version = Get-DesktopVersion -RepoRoot $repoRoot
    $releaseRoot = Join-Path $repoRoot "out/release/desktop/$version"
    if (Test-Path -LiteralPath $releaseRoot) {
        Remove-Item -LiteralPath $releaseRoot -Recurse -Force
    }
    New-Item -ItemType Directory -Path $releaseRoot | Out-Null

    $env:ONI_REQUIRE_SIDECAR = "1"
    $artifacts = @()
    foreach ($item in $requestedVariants) {
        Write-Host "Building Tauri installer variant: $item"
        Invoke-TauriBuildVariant -Variant $item -RepoRoot $repoRoot
        $artifact = Publish-VariantArtifacts -Variant $item -RepoRoot $repoRoot -ReleaseRoot $releaseRoot -Version $version
        $artifacts += $artifact
    }

    Invoke-OptionalCodeSigning -SigningProfile $SigningProfile -Files $artifacts
    $checksumsPath = Write-ReleaseChecksums -ReleaseRoot $releaseRoot -Files $artifacts
    $summaryPath = Write-BuildSummary -ReleaseRoot $releaseRoot -Version $version -SigningProfile $SigningProfile -Artifacts $artifacts

    Write-Host "Release artifacts:"
    foreach ($artifact in $artifacts) {
        Write-Host (" - {0}" -f (Resolve-Path $artifact))
    }
    Write-Host (" - {0}" -f (Resolve-Path $checksumsPath))
    Write-Host (" - {0}" -f (Resolve-Path $summaryPath))
} finally {
    Remove-Item Env:ONI_REQUIRE_SIDECAR -ErrorAction SilentlyContinue
    Pop-Location
}
