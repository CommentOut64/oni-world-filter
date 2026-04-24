Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$repoRoot = Resolve-Path (Join-Path $PSScriptRoot "..")
. (Join-Path $PSScriptRoot "lib/desktop-bootstrap.ps1")

Push-Location $repoRoot
try {
    Invoke-Yarn -Args @("--cwd", "desktop", "dev")
} finally {
    Pop-Location
}
