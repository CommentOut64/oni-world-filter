param(
    [int]$DevBootWaitSeconds = 20
)

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

    throw "Cannot find available TCP port in [$SearchStart, $SearchEnd] for tauri dev smoke boot."
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

    Write-Host "[smoke] Run Rust sidecar search smoke test ..."
    cargo test --manifest-path src-tauri/Cargo.toml sidecar_search_smoke -- --ignored --nocapture
    if ($LASTEXITCODE -ne 0) {
        throw "cargo test sidecar_search_smoke failed with exit code $LASTEXITCODE"
    }

    Write-Host "[smoke] Boot tauri dev process (startup check) ..."
    $devPort = Get-AvailableTcpPort -PreferredPort 1420
    $devArgs = @("tauri", "dev", "--no-watch")
    $devConfigPath = $null
    if ($devPort -ne 1420) {
        Write-Host "[smoke] Port 1420 is occupied, switch smoke dev port to $devPort ..."
        $devConfigOverride = @{
            build = @{
                beforeDevCommand = "cd desktop && corepack yarn dev --host 127.0.0.1 --port $devPort --strictPort"
                devUrl = "http://127.0.0.1:$devPort"
            }
        } | ConvertTo-Json -Compress
        $devConfigPath = Join-Path $env:TEMP ("oni-tauri-dev-config-{0}.json" -f ([guid]::NewGuid().ToString("N")))
        Set-Content -LiteralPath $devConfigPath -Value $devConfigOverride -Encoding UTF8
        $devArgs += @("--config", $devConfigPath)
    }

    $devWorkDir = Join-Path $repoRoot "src-tauri"
    $devStdoutLog = Join-Path $env:TEMP ("oni-tauri-dev-stdout-{0}.log" -f ([guid]::NewGuid().ToString("N")))
    $devStderrLog = Join-Path $env:TEMP ("oni-tauri-dev-stderr-{0}.log" -f ([guid]::NewGuid().ToString("N")))
    $devProcess = $null
    $startupPassed = $false
    try {
        $devProcess = Start-Process -FilePath "cargo" -ArgumentList $devArgs -WorkingDirectory $devWorkDir -PassThru -RedirectStandardOutput $devStdoutLog -RedirectStandardError $devStderrLog
        Start-Sleep -Seconds $DevBootWaitSeconds
        if ($devProcess.HasExited) {
            $devProcess.WaitForExit()
            $exitCode = if ($null -ne $devProcess.ExitCode) { $devProcess.ExitCode } else { "unknown" }
            $stdoutTail = ""
            $stderrTail = ""
            if (Test-Path -LiteralPath $devStdoutLog) {
                $stdoutTail = (Get-Content -LiteralPath $devStdoutLog -Tail 30) -join [Environment]::NewLine
            }
            if (Test-Path -LiteralPath $devStderrLog) {
                $stderrTail = (Get-Content -LiteralPath $devStderrLog -Tail 30) -join [Environment]::NewLine
            }

            $details = @()
            if (-not [string]::IsNullOrWhiteSpace($stdoutTail)) {
                $details += "stdout tail:`n$stdoutTail"
            }
            if (-not [string]::IsNullOrWhiteSpace($stderrTail)) {
                $details += "stderr tail:`n$stderrTail"
            }
            $details += "stdout log: $devStdoutLog"
            $details += "stderr log: $devStderrLog"

            throw ("cargo tauri dev exited early with code {0}`n{1}" -f $exitCode, ($details -join "`n`n"))
        }

        $startupPassed = $true
        Write-Host "[smoke] tauri dev process is alive after $DevBootWaitSeconds s, startup check passed."
    }
    finally {
        if ($devProcess -and -not $devProcess.HasExited) {
            Stop-Process -Id $devProcess.Id -Force
        }
        if ($startupPassed) {
            Remove-Item -LiteralPath $devStdoutLog -Force -ErrorAction SilentlyContinue
            Remove-Item -LiteralPath $devStderrLog -Force -ErrorAction SilentlyContinue
            if ($devConfigPath) {
                Remove-Item -LiteralPath $devConfigPath -Force -ErrorAction SilentlyContinue
            }
        }
    }
}
finally {
    Pop-Location
}
