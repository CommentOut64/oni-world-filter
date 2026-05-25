param(
    [string]$SourceRoot,
    [string]$TargetRoot,
    [string]$AsubworldsPath,
    [switch]$SkipDependencyInstall
)

$ErrorActionPreference = "Stop"

function Get-RepoRoot {
    return (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
}

function Get-LatestTruthSummaryPath {
    param(
        [Parameter(Mandatory = $true)]
        [string]$RepoRoot
    )

    $artifactRoot = Join-Path $RepoRoot "devdoc/v1.1.0/artifacts"
    if (-not (Test-Path -LiteralPath $artifactRoot)) {
        throw "未找到 DLC5 基线产物目录: $artifactRoot"
    }

    $summary = Get-ChildItem -LiteralPath $artifactRoot -Directory |
        Where-Object { $_.Name -like "dlc5-truth-*" } |
        Sort-Object LastWriteTimeUtc -Descending |
        ForEach-Object {
            $candidate = Join-Path $_.FullName "truth-summary.json"
            if (Test-Path -LiteralPath $candidate) {
                return $candidate
            }
        } |
        Select-Object -First 1

    if (-not $summary) {
        throw "未找到可用的 truth-summary.json，请先执行 scripts/collect-dlc5-truth.ps1"
    }

    return $summary
}

function Resolve-SourceRootFromTruth {
    param(
        [Parameter(Mandatory = $true)]
        [string]$RepoRoot
    )

    $truthSummaryPath = Get-LatestTruthSummaryPath -RepoRoot $RepoRoot
    $truthSummary = Get-Content -Raw -LiteralPath $truthSummaryPath | ConvertFrom-Json
    if (-not $truthSummary.dlc5Root) {
        throw "truth-summary.json 缺少 dlc5Root: $truthSummaryPath"
    }

    return [string]$truthSummary.dlc5Root
}

function Ensure-LocalDependency {
    param(
        [Parameter(Mandatory = $true)]
        [string]$ScriptRoot
    )

    $yamlPackagePath = Join-Path $ScriptRoot "node_modules/yaml/package.json"
    if (Test-Path -LiteralPath $yamlPackagePath) {
        return
    }

    Write-Host "安装 scripts 本地依赖..."
    Push-Location $ScriptRoot
    try {
        npm install | Out-Host
    } finally {
        Pop-Location
    }
}

$repoRoot = Get-RepoRoot
if (-not $SourceRoot) {
    $SourceRoot = Resolve-SourceRootFromTruth -RepoRoot $repoRoot
}
if (-not $TargetRoot) {
    $TargetRoot = Join-Path $repoRoot "asset/dlc/dlc5"
}
if (-not $AsubworldsPath) {
    $AsubworldsPath = Join-Path $repoRoot "asset/Asubworlds.json"
}

if (-not (Test-Path -LiteralPath $SourceRoot)) {
    throw "DLC5 源目录不存在: $SourceRoot"
}

if (-not $SkipDependencyInstall) {
    Ensure-LocalDependency -ScriptRoot $PSScriptRoot
}

$nodeScriptPath = Join-Path $PSScriptRoot "import-dlc5-assets.mjs"
& node $nodeScriptPath `
    --source $SourceRoot `
    --target $TargetRoot `
    --asubworlds $AsubworldsPath `
    --clean-target
