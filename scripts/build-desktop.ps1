param(
    [switch]$SkipYarnInstall
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Invoke-Yarn {
    param(
        [Parameter(Mandatory = $true)][string[]]$Args
    )

    if (Get-Command yarn -ErrorAction SilentlyContinue) {
        & yarn @Args
        if ($LASTEXITCODE -ne 0) {
            throw "yarn command failed with exit code $LASTEXITCODE"
        }
        return
    }
    if (Get-Command corepack -ErrorAction SilentlyContinue) {
        & corepack yarn @Args
        if ($LASTEXITCODE -ne 0) {
            throw "corepack yarn command failed with exit code $LASTEXITCODE"
        }
        return
    }
    throw "Neither yarn nor corepack is available in PATH."
}

function Assert-CargoTauri {
    if (-not (Get-Command cargo-tauri -ErrorAction SilentlyContinue)) {
        throw "cargo-tauri is not installed. Run: cargo install tauri-cli --version '^2.0.0'"
    }
}

function Ensure-SidecarBinary {
    Write-Host "Configuring native build preset (x64-release)..."
    cmake --preset x64-release
    if ($LASTEXITCODE -ne 0) {
        throw "cmake --preset x64-release failed with exit code $LASTEXITCODE"
    }

    Write-Host "Building oni-sidecar target..."
    cmake --build out/build/x64-release --target oni-sidecar
    if ($LASTEXITCODE -ne 0) {
        throw "cmake --build out/build/x64-release --target oni-sidecar failed with exit code $LASTEXITCODE"
    }

    $source = Resolve-Path "out/build/x64-release/oni-sidecar.exe"
    $targetDir = Join-Path $repoRoot "src-tauri/binaries"
    if (-not (Test-Path -LiteralPath $targetDir)) {
        New-Item -ItemType Directory -Path $targetDir | Out-Null
    }
    $target = Join-Path $targetDir "oni-sidecar.exe"
    Copy-Item -LiteralPath $source -Destination $target -Force
    Write-Host "Sidecar synced to $target"
}

$repoRoot = Resolve-Path (Join-Path $PSScriptRoot "..")
Push-Location $repoRoot
try {
    if (-not $SkipYarnInstall -and -not (Test-Path -LiteralPath "desktop/node_modules")) {
        Write-Host "Installing desktop dependencies..."
        Invoke-Yarn -Args @("--cwd", "desktop", "install")
    }

    Assert-CargoTauri
    Ensure-SidecarBinary
    Push-Location "src-tauri"
    try {
        cargo tauri build
        if ($LASTEXITCODE -ne 0) {
            throw "cargo tauri build failed with exit code $LASTEXITCODE"
        }
    } finally {
        Pop-Location
    }
} finally {
    Pop-Location
}
