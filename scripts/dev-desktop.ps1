param(
    [switch]$SkipYarnInstall
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$repoRoot = Resolve-Path (Join-Path $PSScriptRoot "..")
. (Join-Path $PSScriptRoot "lib/desktop-bootstrap.ps1")

Push-Location $repoRoot
try {
    Assert-VersionConsistency -RepoRoot $repoRoot
    Assert-VsToolchain
    Assert-NodeAndYarn
    Assert-RustAndCargoTauri

    if (-not $SkipYarnInstall -and -not (Test-Path -LiteralPath "desktop/node_modules")) {
        Write-Host "Installing desktop dependencies..."
        Invoke-Yarn -Args @("--cwd", "desktop", "install")
    }

    Build-AndSyncSidecar -RepoRoot $repoRoot -Configuration Release | Out-Null

    $env:ONI_REQUIRE_SIDECAR = "0"
    $env:ONI_SIDECAR_PATH = (Resolve-Path -LiteralPath (Join-Path $repoRoot "src-tauri/binaries/oni-sidecar.exe")).Path
    Push-Location "src-tauri"
    try {
        cargo tauri dev
        if ($LASTEXITCODE -ne 0) {
            throw "cargo tauri dev failed with exit code $LASTEXITCODE"
        }
    } finally {
        Pop-Location
    }
} finally {
    Pop-Location
}
