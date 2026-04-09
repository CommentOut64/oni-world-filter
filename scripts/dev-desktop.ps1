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

$repoRoot = Resolve-Path (Join-Path $PSScriptRoot "..")
Push-Location $repoRoot
try {
    if (-not $SkipYarnInstall -and -not (Test-Path -LiteralPath "desktop/node_modules")) {
        Write-Host "Installing desktop dependencies..."
        Invoke-Yarn -Args @("--cwd", "desktop", "install")
    }

    Assert-CargoTauri
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
