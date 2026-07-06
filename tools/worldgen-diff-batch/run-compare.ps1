param(
    [int]$MaxCoords = 100,
    [int]$Workers = 0,
    [UInt64]$Seed = 20260705,
    [string]$OutputDir = "",
    [string]$AffinityMask = "",
    [string]$SidecarPath = "",
    [string]$ExternalRoot = ""
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Resolve-AffinityMask {
    param([string]$Value)

    if ([string]::IsNullOrWhiteSpace($Value)) {
        return $null
    }

    $trimmed = $Value.Trim()
    if ($trimmed.StartsWith("0x", [System.StringComparison]::OrdinalIgnoreCase)) {
        $hex = $trimmed.Substring(2)
        return [UInt64]::Parse($hex, [System.Globalization.NumberStyles]::HexNumber)
    }

    return [UInt64]::Parse($trimmed, [System.Globalization.NumberStyles]::Integer)
}

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$mainScript = Join-Path $scriptDir "src\main.mjs"

if ([string]::IsNullOrWhiteSpace($OutputDir)) {
    $OutputDir = Join-Path $scriptDir ("runs\" + (Get-Date -Format "yyyyMMdd-HHmmss"))
}

$nodeArgs = @(
    $mainScript,
    "--max-coords", $MaxCoords,
    "--seed", $Seed,
    "--output-dir", $OutputDir
)

if ($Workers -gt 0) {
    $nodeArgs += @("--workers", $Workers)
}
if (-not [string]::IsNullOrWhiteSpace($SidecarPath)) {
    $nodeArgs += @("--sidecar", $SidecarPath)
}
if (-not [string]::IsNullOrWhiteSpace($ExternalRoot)) {
    $nodeArgs += @("--external-root", $ExternalRoot)
}

$resolvedAffinity = Resolve-AffinityMask -Value $AffinityMask
if ($null -eq $resolvedAffinity) {
    & node @nodeArgs
    exit $LASTEXITCODE
}

$argumentList = $nodeArgs | ForEach-Object { $_.ToString() }
$process = Start-Process -FilePath "node" -ArgumentList $argumentList -PassThru -WindowStyle Hidden
try {
    Start-Sleep -Milliseconds 200
    $process.ProcessorAffinity = [IntPtr]::new([int64]$resolvedAffinity)
    Wait-Process -Id $process.Id
    exit $process.ExitCode
} finally {
    if (-not $process.HasExited) {
        $process.Kill()
    }
}
