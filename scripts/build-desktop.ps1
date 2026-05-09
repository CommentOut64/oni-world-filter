param(
    [ValidateSet("standard", "offline", "both")]
    [string]$Variant = "both",
    [ValidateSet("installer", "portable", "all")]
    [string]$Package = "all",
    [ValidateSet("unsigned")]
    [string]$SigningProfile = "unsigned",
    [switch]$SkipYarnInstall
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"
Add-Type -AssemblyName System.IO.Compression.FileSystem

$repoRoot = Resolve-Path (Join-Path $PSScriptRoot "..")
. (Join-Path $PSScriptRoot "lib/desktop-bootstrap.ps1")

function Get-RequestedPortableVariants {
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

function Test-ShouldBuildInstaller {
    param(
        [Parameter(Mandatory = $true)][string]$Package
    )

    return $Package -eq "installer" -or $Package -eq "all"
}

function Test-ShouldBuildPortable {
    param(
        [Parameter(Mandatory = $true)][string]$Package
    )

    return $Package -eq "portable" -or $Package -eq "all"
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

function Invoke-TauriBuild {
    param(
        [Parameter(Mandatory = $true)][string]$RepoRoot,
        [switch]$NoBundle
    )

    Push-Location (Join-Path $RepoRoot "src-tauri")
    try {
        if ($NoBundle) {
            cargo tauri build --no-bundle
            if ($LASTEXITCODE -ne 0) {
                throw "cargo tauri build --no-bundle failed with exit code $LASTEXITCODE"
            }
            return
        }

        cargo tauri build
        if ($LASTEXITCODE -ne 0) {
            throw "cargo tauri build failed with exit code $LASTEXITCODE"
        }
    } finally {
        Pop-Location
    }
}

function Publish-InstallerArtifacts {
    param(
        [Parameter(Mandatory = $true)][string]$RepoRoot,
        [Parameter(Mandatory = $true)][string]$ReleaseRoot,
        [Parameter(Mandatory = $true)][string]$Version
    )

    $sourceDirectory = Join-Path $RepoRoot "src-tauri/target/release/bundle/nsis"
    $installerDirectory = Join-Path $ReleaseRoot "installer"
    if (Test-Path -LiteralPath $installerDirectory) {
        Remove-Item -LiteralPath $installerDirectory -Recurse -Force
    }
    New-Item -ItemType Directory -Path $installerDirectory | Out-Null

    $collected = @(Collect-ReleaseArtifacts -SourceDirectory $sourceDirectory -DestinationDirectory $installerDirectory -Patterns @("*.exe"))
    if ($collected.Count -ne 1) {
        throw "Expected exactly one NSIS installer, but found $($collected.Count)"
    }

    $sourcePath = $collected[0]
    $finalPath = Join-Path $installerDirectory "oni-world-filter-$Version-Setup.exe"
    Move-Item -LiteralPath $sourcePath -Destination $finalPath -Force
    return $finalPath
}

function Resolve-DesktopExecutablePath {
    param(
        [Parameter(Mandatory = $true)][string]$RepoRoot
    )

    $path = Join-Path $RepoRoot "src-tauri/target/release/oni-world-filter.exe"
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw "Desktop executable not found: $path"
    }
    return (Resolve-Path -LiteralPath $path).Path
}

function Resolve-WebView2FixedRuntimeSource {
    $source = $env:ONI_WEBVIEW2_FIXED_RUNTIME_DIR
    if ([string]::IsNullOrWhiteSpace($source)) {
        throw "Portable offline packaging requires ONI_WEBVIEW2_FIXED_RUNTIME_DIR."
    }
    $resolved = (Resolve-Path -LiteralPath $source).Path
    if (-not (Test-Path -LiteralPath $resolved -PathType Container)) {
        throw "WebView2 fixed runtime directory not found: $resolved"
    }
    return $resolved
}

function New-ZipArchiveFromDirectory {
    param(
        [Parameter(Mandatory = $true)][string]$SourceDirectory,
        [Parameter(Mandatory = $true)][string]$DestinationPath
    )

    if (-not (Test-Path -LiteralPath $SourceDirectory -PathType Container)) {
        throw "Zip source directory not found: $SourceDirectory"
    }

    $destinationDirectory = Split-Path -Parent $DestinationPath
    if (-not [string]::IsNullOrWhiteSpace($destinationDirectory) -and -not (Test-Path -LiteralPath $destinationDirectory)) {
        New-Item -ItemType Directory -Path $destinationDirectory | Out-Null
    }

    if (Test-Path -LiteralPath $DestinationPath) {
        Remove-Item -LiteralPath $DestinationPath -Force
    }

    [System.IO.Compression.ZipFile]::CreateFromDirectory(
        $SourceDirectory,
        $DestinationPath,
        [System.IO.Compression.CompressionLevel]::Optimal,
        $false
    )
}

function New-PortableStageDirectory {
    param(
        [Parameter(Mandatory = $true)][ValidateSet("standard", "offline")][string]$Variant,
        [Parameter(Mandatory = $true)][string]$RepoRoot,
        [Parameter(Mandatory = $true)][string]$Version
    )

    $stageRoot = Join-Path $RepoRoot "src-tauri/target/release/portable-stage/$Variant"
    if (Test-Path -LiteralPath $stageRoot) {
        Remove-Item -LiteralPath $stageRoot -Recurse -Force
    }
    New-Item -ItemType Directory -Path $stageRoot | Out-Null

    $packageRoot = Join-Path $stageRoot "oni-world-filter-$Version-Portable-$Variant"
    $resourceBinariesDirectory = Join-Path $packageRoot "resources/binaries"
    foreach ($path in @(
        $packageRoot,
        $resourceBinariesDirectory,
        (Join-Path $packageRoot "data/app-data"),
        (Join-Path $packageRoot "data/logs"),
        (Join-Path $packageRoot "data/sidecars"),
        (Join-Path $packageRoot "data/webview")
    )) {
        New-Item -ItemType Directory -Path $path -Force | Out-Null
    }

    return @{
        stageRoot = $stageRoot
        packageRoot = $packageRoot
        resourceBinariesDirectory = $resourceBinariesDirectory
    }
}

function Publish-PortableArtifacts {
    param(
        [Parameter(Mandatory = $true)][ValidateSet("standard", "offline")][string]$Variant,
        [Parameter(Mandatory = $true)][string]$RepoRoot,
        [Parameter(Mandatory = $true)][string]$ReleaseRoot,
        [Parameter(Mandatory = $true)][string]$Version
    )

    $desktopExecutable = Resolve-DesktopExecutablePath -RepoRoot $RepoRoot
    $layout = New-PortableStageDirectory -Variant $Variant -RepoRoot $RepoRoot -Version $Version
    $packageRoot = $layout.packageRoot
    $stageRoot = $layout.stageRoot
    $resourceBinariesDirectory = $layout.resourceBinariesDirectory

    Copy-Item -LiteralPath $desktopExecutable -Destination (Join-Path $packageRoot "oni-world-filter.exe") -Force
    Copy-Item -LiteralPath (Join-Path $RepoRoot "src-tauri/binaries/oni-sidecar.exe") -Destination (Join-Path $resourceBinariesDirectory "oni-sidecar.exe") -Force
    Copy-Item -LiteralPath (Join-Path $RepoRoot "src-tauri/binaries/data.zip") -Destination (Join-Path $resourceBinariesDirectory "data.zip") -Force
    Set-Content -LiteralPath (Join-Path $packageRoot "portable.flag") -Value "portable" -Encoding ascii

    if ($Variant -eq "offline") {
        $runtimeSource = Resolve-WebView2FixedRuntimeSource
        Copy-Item -LiteralPath $runtimeSource -Destination (Join-Path $packageRoot "resources/Microsoft.WebView2.FixedVersionRuntime") -Recurse -Force
    }

    $variantDirectory = Join-Path $ReleaseRoot "portable-$Variant"
    if (Test-Path -LiteralPath $variantDirectory) {
        Remove-Item -LiteralPath $variantDirectory -Recurse -Force
    }
    New-Item -ItemType Directory -Path $variantDirectory | Out-Null

    $archivePath = Join-Path $variantDirectory "oni-world-filter-$Version-Portable-$Variant.zip"
    New-ZipArchiveFromDirectory -SourceDirectory $packageRoot -DestinationPath $archivePath

    return $archivePath
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
    $requestedPortableVariants = @(Get-RequestedPortableVariants -Variant $Variant)
    $buildInstaller = Test-ShouldBuildInstaller -Package $Package
    $buildPortable = Test-ShouldBuildPortable -Package $Package

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

    if ($buildInstaller) {
        Write-Host "Building Tauri setup installer..."
        Clear-TauriBundleOutput -RepoRoot $repoRoot
        Invoke-TauriBuild -RepoRoot $repoRoot
        $artifacts += Publish-InstallerArtifacts -RepoRoot $repoRoot -ReleaseRoot $releaseRoot -Version $version
    } elseif ($buildPortable) {
        Write-Host "Building Tauri desktop executable for portable packaging..."
        Invoke-TauriBuild -RepoRoot $repoRoot -NoBundle
    }

    if ($buildPortable) {
        foreach ($portableVariant in $requestedPortableVariants) {
            Write-Host "Packaging portable variant: $portableVariant"
            $artifacts += Publish-PortableArtifacts -Variant $portableVariant -RepoRoot $repoRoot -ReleaseRoot $releaseRoot -Version $version
        }
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
