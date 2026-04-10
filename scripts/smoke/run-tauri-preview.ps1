Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Assert-Command {
    param(
        [Parameter(Mandatory = $true)][string]$Name
    )
    if (-not (Get-Command $Name -ErrorAction SilentlyContinue)) {
        throw "Required command not found in PATH: $Name"
    }
}

function Enter-VsDevEnvironment {
    if (Get-Command cl -ErrorAction SilentlyContinue) {
        return
    }

    $vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
    if (-not (Test-Path -LiteralPath $vswhere)) {
        throw "vswhere not found: $vswhere"
    }

    $installPath = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if ($LASTEXITCODE -ne 0 -or [string]::IsNullOrWhiteSpace($installPath)) {
        throw "Cannot locate Visual Studio with C++ toolchain via vswhere."
    }

    $vsDevCmd = Join-Path $installPath "Common7\Tools\VsDevCmd.bat"
    if (-not (Test-Path -LiteralPath $vsDevCmd)) {
        throw "VsDevCmd.bat not found: $vsDevCmd"
    }

    Write-Host "[smoke] Enter Visual Studio Developer environment ..."
    $envDump = cmd /c "`"$vsDevCmd`" -arch=x64 >nul && set"
    if ($LASTEXITCODE -ne 0) {
        throw "Failed to activate VS developer environment."
    }

    foreach ($line in $envDump) {
        if ($line -match "^([^=]+)=(.*)$") {
            [System.Environment]::SetEnvironmentVariable($matches[1], $matches[2], "Process")
        }
    }

    if (-not (Get-Command cl -ErrorAction SilentlyContinue)) {
        throw "cl.exe still unavailable after loading VS developer environment."
    }
}

function Ensure-PythonCommand {
    if (Get-Command python -ErrorAction SilentlyContinue) {
        return
    }
    $py = Get-Command py -ErrorAction SilentlyContinue
    if ($py) {
        $env:EMSDK_PYTHON = $py.Source
        Write-Host "[smoke] python not found, fallback EMSDK_PYTHON=$($env:EMSDK_PYTHON)"
        return
    }
    throw "Neither python nor py launcher is available in PATH."
}

$repoRoot = Resolve-Path (Join-Path $PSScriptRoot "..\..")
$sidecarPath = Join-Path $repoRoot "out\build\x64-release\src\oni-sidecar.exe"

Push-Location $repoRoot
try {
    Assert-Command -Name "cmake"
    Assert-Command -Name "cargo"
    Assert-Command -Name "ninja"
    Enter-VsDevEnvironment
    Ensure-PythonCommand

    Write-Host "[smoke] Configure x64-release ..."
    cmake --preset x64-release
    if ($LASTEXITCODE -ne 0) {
        throw "cmake --preset x64-release failed with exit code $LASTEXITCODE"
    }

    Write-Host "[smoke] Build oni-sidecar ..."
    cmake --build out/build/x64-release --target oni-sidecar
    if ($LASTEXITCODE -ne 0) {
        throw "cmake --build out/build/x64-release --target oni-sidecar failed with exit code $LASTEXITCODE"
    }
    if (-not (Test-Path -LiteralPath $sidecarPath)) {
        throw "sidecar binary not found: $sidecarPath"
    }

    $env:ONI_SIDECAR_PATH = $sidecarPath

    Write-Host "[smoke] Run Rust sidecar preview smoke test ..."
    cargo test --manifest-path src-tauri/Cargo.toml sidecar_preview_smoke -- --ignored --nocapture
    if ($LASTEXITCODE -ne 0) {
        throw "cargo test sidecar_preview_smoke failed with exit code $LASTEXITCODE"
    }
}
finally {
    Pop-Location
}
