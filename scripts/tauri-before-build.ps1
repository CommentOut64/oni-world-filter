Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$repoRoot = Resolve-Path (Join-Path $PSScriptRoot "..")
. (Join-Path $PSScriptRoot "lib/desktop-bootstrap.ps1")

Push-Location $repoRoot
try {
    Sync-AppIconAssets -RepoRoot $repoRoot
    Invoke-Yarn -Args @("--cwd", "desktop", "build")
} finally {
    Pop-Location
}
