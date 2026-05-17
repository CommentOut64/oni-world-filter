param(
    [switch]$SkipYarnInstall
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Test-TcpPortAvailable {
    param(
        [Parameter(Mandatory = $true)][int]$Port
    )

    $listener = $null
    try {
        $listener = [System.Net.Sockets.TcpListener]::new([System.Net.IPAddress]::Loopback, $Port)
        $listener.Start()
        return $true
    }
    catch {
        return $false
    }
    finally {
        if ($listener) {
            $listener.Stop()
        }
    }
}

function Get-AvailableTcpPort {
    param(
        [Parameter(Mandatory = $true)][int]$PreferredPort,
        [int]$SearchStart = 1431,
        [int]$SearchEnd = 1499
    )

    if (Test-TcpPortAvailable -Port $PreferredPort) {
        return $PreferredPort
    }

    for ($port = $SearchStart; $port -le $SearchEnd; $port++) {
        if (Test-TcpPortAvailable -Port $port) {
            return $port
        }
    }

    throw "Cannot find available TCP port in [$SearchStart, $SearchEnd] for desktop dev boot."
}

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
    $devPort = Get-AvailableTcpPort -PreferredPort 1420
    $tauriDevArgs = @("tauri", "dev")
    $devConfigPath = $null
    if ($devPort -ne 1420) {
        Write-Host "Port 1420 is occupied, switch desktop dev port to $devPort ..."
        $devConfigOverride = @{
            build = @{
                beforeDevCommand = "powershell -NoProfile -ExecutionPolicy Bypass -File ./scripts/tauri-before-dev.ps1 -Port $devPort"
                devUrl = "http://127.0.0.1:$devPort"
            }
        } | ConvertTo-Json -Compress
        $devConfigPath = Join-Path $env:TEMP ("oni-desktop-dev-config-{0}.json" -f ([guid]::NewGuid().ToString("N")))
        Set-Content -LiteralPath $devConfigPath -Value $devConfigOverride -Encoding UTF8
        $tauriDevArgs += @("--config", $devConfigPath)
    }
    Push-Location "src-tauri"
    try {
        cargo @tauriDevArgs
        if ($LASTEXITCODE -ne 0) {
            throw "cargo tauri dev failed with exit code $LASTEXITCODE"
        }
    } finally {
        Pop-Location
        if ($devConfigPath) {
            Remove-Item -LiteralPath $devConfigPath -Force -ErrorAction SilentlyContinue
        }
    }
} finally {
    Pop-Location
}
