Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$repoRoot = Resolve-Path (Join-Path $PSScriptRoot "..\..")
$sidecarPath = Join-Path $repoRoot "out\build\x64-release\oni-sidecar.exe"

Push-Location $repoRoot
try {
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
