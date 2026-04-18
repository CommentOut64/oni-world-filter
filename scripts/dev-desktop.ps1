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

function Ensure-PythonCommand {
    $python = Get-Command python -ErrorAction SilentlyContinue
    if ($python) {
        & $python.Source --version *> $null
        if ($LASTEXITCODE -eq 0) {
            return
        }
    }

    $py = Get-Command py -ErrorAction SilentlyContinue
    if ($py) {
        $env:EMSDK_PYTHON = $py.Source
        Write-Host "python not found, fallback EMSDK_PYTHON=$($env:EMSDK_PYTHON)"
        return
    }

    throw "Neither python nor py launcher is available in PATH."
}

function Ensure-SidecarBinary {
    Write-Host "Configuring native build preset (mingw-release)..."
    cmake --preset mingw-release
    if ($LASTEXITCODE -ne 0) {
        throw "cmake --preset mingw-release failed with exit code $LASTEXITCODE"
    }

    Write-Host "Building oni-sidecar target..."
    cmake --build out/build/mingw-release --target oni-sidecar
    if ($LASTEXITCODE -ne 0) {
        throw "cmake --build out/build/mingw-release --target oni-sidecar failed with exit code $LASTEXITCODE"
    }

    $source = Resolve-Path "out/build/mingw-release/src/oni-sidecar.exe"
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

    Ensure-PythonCommand
    Assert-CargoTauri
    Ensure-SidecarBinary
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
