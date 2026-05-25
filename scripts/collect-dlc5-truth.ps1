1param(
    [string]$SteamAppsRoot = "",
    [string]$OniInstallRoot = "",
    [string]$IlspyRoot = "C:\Users\wgh\AppData\Local\Programs\ILSpy",
    [string]$OutputRoot = ""
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Resolve-RepoRoot {
    return (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
}

function Ensure-Directory {
    param(
        [Parameter(Mandatory = $true)][string]$Path
    )

    if (-not (Test-Path -LiteralPath $Path)) {
        $null = New-Item -ItemType Directory -Path $Path -Force
    }
    return (Resolve-Path -LiteralPath $Path).Path
}

function Write-TextFile {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][string]$Content
    )

    $parent = Split-Path -Parent $Path
    if ($parent) {
        $null = Ensure-Directory -Path $parent
    }
    Set-Content -LiteralPath $Path -Value $Content -Encoding utf8
}

function Get-RelativePath {
    param(
        [Parameter(Mandatory = $true)][string]$BasePath,
        [Parameter(Mandatory = $true)][string]$ChildPath
    )

    $baseFull = [System.IO.Path]::GetFullPath($BasePath)
    if (-not $baseFull.EndsWith([System.IO.Path]::DirectorySeparatorChar)) {
        $baseFull += [System.IO.Path]::DirectorySeparatorChar
    }
    $baseUri = New-Object System.Uri($baseFull)
    $childUri = New-Object System.Uri([System.IO.Path]::GetFullPath($ChildPath))
    $relative = $baseUri.MakeRelativeUri($childUri).ToString().Replace('/', '\')
    return [System.Uri]::UnescapeDataString($relative)
}

function Read-AcfManifest {
    param(
        [Parameter(Mandatory = $true)][string]$Path
    )

    $values = [ordered]@{}
    $sectionStack = New-Object System.Collections.Generic.List[string]
    $pendingSection = $null
    foreach ($rawLine in Get-Content -LiteralPath $Path) {
        $line = $rawLine.Trim()
        if ($line.Length -eq 0) {
            continue
        }
        if ($pendingSection -ne $null) {
            if ($line -eq '{') {
                $sectionStack.Add($pendingSection)
                $pendingSection = $null
                continue
            }
            $pendingSection = $null
        }
        if ($line -match '^"([^"]+)"\s*\{$') {
            $sectionStack.Add($matches[1])
            continue
        }
        if ($line -match '^"([^"]+)"$') {
            $pendingSection = $matches[1]
            continue
        }
        if ($line -eq '}') {
            if ($sectionStack.Count -gt 0) {
                $sectionStack.RemoveAt($sectionStack.Count - 1)
            }
            continue
        }
        if ($line -match '^"([^"]+)"\s*"([^"]*)"$') {
            $pathParts = @($sectionStack.ToArray()) + $matches[1]
            $flatKey = ($pathParts -join '.')
            $values[$flatKey] = $matches[2]
        }
    }

    return $values
}

function Get-FirstExistingPath {
    param(
        [Parameter(Mandatory = $true)][string[]]$Candidates
    )

    foreach ($candidate in $Candidates) {
        if ([string]::IsNullOrWhiteSpace($candidate)) {
            continue
        }
        if (Test-Path -LiteralPath $candidate) {
            return (Resolve-Path -LiteralPath $candidate).Path
        }
    }

    return $null
}

function Resolve-SteamAppsRoot {
    param(
        [string]$RequestedRoot
    )

    if (-not [string]::IsNullOrWhiteSpace($RequestedRoot)) {
        return (Resolve-Path -LiteralPath $RequestedRoot).Path
    }

    $envRoots = @(
        $env:ONI_STEAMAPPS_ROOT,
        $env:STEAMAPPS_ROOT
    )
    foreach ($envRoot in $envRoots) {
        if (-not [string]::IsNullOrWhiteSpace($envRoot)) {
            $resolved = Get-FirstExistingPath -Candidates @($envRoot)
            if ($resolved) {
                return $resolved
            }
        }
    }

    $registryCandidates = @()
    try {
        $steamPath = (Get-ItemProperty -Path 'HKCU:\Software\Valve\Steam' -Name SteamPath -ErrorAction Stop).SteamPath
        if ($steamPath) {
            $registryCandidates += (Join-Path $steamPath 'steamapps')
        }
    } catch {
    }

    $commonCandidates = @(
        'C:\Program Files (x86)\Steam\steamapps',
        'C:\Program Files\Steam\steamapps',
        'D:\Games\Steam\steamapps',
        'F:\SteamLibrary\steamapps'
    )

    foreach ($candidate in ($registryCandidates + $commonCandidates)) {
        if ([string]::IsNullOrWhiteSpace($candidate)) {
            continue
        }
        if (-not (Test-Path -LiteralPath $candidate)) {
            continue
        }
        if (Test-Path -LiteralPath (Join-Path $candidate 'appmanifest_457140.acf')) {
            return (Resolve-Path -LiteralPath $candidate).Path
        }
    }

    throw 'Unable to resolve Steam apps root. Pass -SteamAppsRoot explicitly.'
}

function Resolve-OniPaths {
    param(
        [string]$RequestedSteamAppsRoot,
        [string]$RequestedInstallRoot
    )

    if (-not [string]::IsNullOrWhiteSpace($RequestedInstallRoot)) {
        $installRoot = (Resolve-Path -LiteralPath $RequestedInstallRoot).Path
        $steamAppsRoot = Split-Path -Parent (Split-Path -Parent $installRoot)
        $manifestPath = $null
        return [pscustomobject]@{
            SteamAppsRoot = $steamAppsRoot
            InstallRoot = $installRoot
            ManifestPath = $manifestPath
        }
    }

    $steamAppsRoot = Resolve-SteamAppsRoot -RequestedRoot $RequestedSteamAppsRoot
    $manifestPath = Join-Path $steamAppsRoot 'appmanifest_457140.acf'
    if (-not (Test-Path -LiteralPath $manifestPath)) {
        throw "Cannot find appmanifest_457140.acf under $steamAppsRoot"
    }

    $manifest = Read-AcfManifest -Path $manifestPath
    $installDir = $manifest['AppState.installdir']
    if ([string]::IsNullOrWhiteSpace($installDir)) {
        throw "Missing AppState.installdir in $manifestPath"
    }

    $installRoot = Join-Path $steamAppsRoot ("common\" + $installDir)
    if (-not (Test-Path -LiteralPath $installRoot)) {
        throw "Cannot find game install root: $installRoot"
    }

    return [pscustomobject]@{
        SteamAppsRoot = $steamAppsRoot
        InstallRoot = (Resolve-Path -LiteralPath $installRoot).Path
        ManifestPath = (Resolve-Path -LiteralPath $manifestPath).Path
    }
}

function Get-Dlc5ResourceSummary {
    param(
        [Parameter(Mandatory = $true)][string]$Dlc5Root
    )

    $files = Get-ChildItem -LiteralPath $Dlc5Root -Recurse -File | Sort-Object FullName
    $relativeFiles = foreach ($file in $files) {
        [pscustomobject]@{
            Path = (Get-RelativePath -BasePath $Dlc5Root -ChildPath $file.FullName)
            Length = $file.Length
            LastWriteTimeUtc = $file.LastWriteTimeUtc.ToString('o')
        }
    }

    $treeLines = foreach ($entry in $relativeFiles) {
        '{0}`t{1}`t{2}' -f $entry.Path, $entry.Length, $entry.LastWriteTimeUtc
    }

    $folderCounts = @{}
    foreach ($entry in $relativeFiles) {
        $folder = Split-Path -Parent $entry.Path
        if ([string]::IsNullOrWhiteSpace($folder)) {
            $folder = '.'
        }
        if (-not $folderCounts.ContainsKey($folder)) {
            $folderCounts[$folder] = 0
        }
        $folderCounts[$folder]++
    }

    $keyFiles = $relativeFiles |
        Where-Object {
            $_.Path -like 'worldgen\clusters\*' -or
            $_.Path -like 'worldgen\worlds\*' -or
            $_.Path -like 'worldgen\subworlds\*' -or
            $_.Path -like 'worldgen\biomes\*' -or
            $_.Path -like 'worldgen\features\*' -or
            $_.Path -like 'worldgen\noise\*' -or
            $_.Path -eq 'worldgen\mixing.yaml' -or
            $_.Path -like 'worldgen\worldMixing\*' -or
            $_.Path -like 'worldgen\subworldMixing\*'
        } |
        Select-Object -ExpandProperty Path

    return [pscustomobject]@{
        FileCount = $relativeFiles.Count
        TotalBytes = ($files | Measure-Object -Property Length -Sum).Sum
        RelativeFiles = $relativeFiles
        TreeLines = $treeLines
        FolderCounts = $folderCounts
        KeyFiles = $keyFiles
    }
}

function Invoke-IlspyDecompile {
    param(
        [Parameter(Mandatory = $true)][string]$IlspyRoot,
        [Parameter(Mandatory = $true)][string]$AssemblyPath,
        [Parameter(Mandatory = $true)][string]$OutputPath
    )

    if (-not (Test-Path -LiteralPath $IlspyRoot)) {
        throw "ILSpy root not found: $IlspyRoot"
    }
    if (-not (Test-Path -LiteralPath $AssemblyPath)) {
        throw "Assembly not found: $AssemblyPath"
    }

    if (-not (Get-Command dotnet -ErrorAction SilentlyContinue)) {
        throw 'dotnet command is not available in PATH.'
    }

    $cacheRoot = Join-Path ([System.IO.Path]::GetTempPath()) 'oni-dlc5-ilspycmd-cache'
    $null = Ensure-Directory -Path $cacheRoot

    $versionIndex = Invoke-RestMethod -Uri 'https://api.nuget.org/v3-flatcontainer/ilspycmd/index.json'
    $stableVersions = @($versionIndex.versions | Where-Object { $_ -notmatch '-' })
    if ($stableVersions.Count -eq 0) {
        throw 'No stable ilspycmd version found on NuGet.'
    }
    $selectedVersion = $stableVersions[-1]
    $versionRoot = Ensure-Directory -Path (Join-Path $cacheRoot $selectedVersion)
    $packagePath = Join-Path $versionRoot ("ilspycmd.$selectedVersion.nupkg")
    $zipPath = Join-Path $versionRoot ("ilspycmd.$selectedVersion.zip")
    $extractRoot = Ensure-Directory -Path (Join-Path $versionRoot 'pkg')

    if (-not (Test-Path -LiteralPath $packagePath)) {
        $packageUrl = "https://www.nuget.org/api/v2/package/ilspycmd/$selectedVersion"
        Invoke-WebRequest -Uri $packageUrl -OutFile $packagePath
    }
    if (-not (Test-Path -LiteralPath $zipPath)) {
        Copy-Item -LiteralPath $packagePath -Destination $zipPath -Force
    }

    $markerPath = Join-Path $extractRoot '.unzipped'
    if (-not (Test-Path -LiteralPath $markerPath)) {
        if (Test-Path -LiteralPath $extractRoot) {
            Remove-Item -LiteralPath $extractRoot -Recurse -Force
        }
        $null = Ensure-Directory -Path $extractRoot
        Expand-Archive -LiteralPath $zipPath -DestinationPath $extractRoot -Force
        Set-Content -LiteralPath $markerPath -Value $selectedVersion -Encoding ascii
    }

    $toolDll = Get-ChildItem -LiteralPath $extractRoot -Recurse -Filter 'ilspycmd.dll' | Select-Object -First 1
    if ($null -eq $toolDll) {
        throw "Cannot find ilspycmd.dll after extracting $packagePath"
    }

    $outputRoot = Ensure-Directory -Path (Join-Path $versionRoot 'output')
    if (Test-Path -LiteralPath $outputRoot) {
        Remove-Item -LiteralPath $outputRoot -Recurse -Force
    }
    $null = Ensure-Directory -Path $outputRoot

    $args = @(
        $toolDll.FullName,
        '--disable-updatecheck',
        '-o',
        $outputRoot,
        $AssemblyPath
    )
    & dotnet @args
    if ($LASTEXITCODE -ne 0) {
        throw "ilspycmd decompile failed with exit code $LASTEXITCODE"
    }

    $generated = Get-ChildItem -LiteralPath $outputRoot -Recurse -Filter '*.cs' | Select-Object -First 1
    if ($null -eq $generated) {
        throw "ilspycmd did not generate a C# file under $outputRoot"
    }
    Copy-Item -LiteralPath $generated.FullName -Destination $OutputPath -Force
}

$repoRoot = Resolve-RepoRoot
$resolved = Resolve-OniPaths -RequestedSteamAppsRoot $SteamAppsRoot -RequestedInstallRoot $OniInstallRoot
$installRoot = $resolved.InstallRoot
$steamAppsRoot = $resolved.SteamAppsRoot
$manifestPath = $resolved.ManifestPath

$dataRoot = Join-Path $installRoot 'OxygenNotIncluded_Data'
$managedRoot = Join-Path $dataRoot 'Managed'
$assemblyPath = Join-Path $managedRoot 'Assembly-CSharp.dll'
$dlc5Root = Join-Path $dataRoot 'StreamingAssets\dlc\dlc5'

if (-not (Test-Path -LiteralPath $dlc5Root)) {
    throw "DLC5 resource root not found: $dlc5Root"
}
if (-not (Test-Path -LiteralPath $assemblyPath)) {
    throw "Assembly-CSharp.dll not found: $assemblyPath"
}

$manifest = if ($manifestPath) { Read-AcfManifest -Path $manifestPath } else { @{} }
$buildId = $manifest['AppState.buildid']
$installedDir = $manifest['AppState.installdir']
$branch = $manifest['AppState.MountedConfig.BetaKey']
if ([string]::IsNullOrWhiteSpace($branch)) {
    $branch = $manifest['AppState.UserConfig.BetaKey']
}
if ([string]::IsNullOrWhiteSpace($branch)) {
    $branch = 'unknown'
}

$timestamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$defaultOutputRoot = Join-Path $repoRoot 'devdoc\v1.1.0\artifacts'
$outputRootResolved = if ([string]::IsNullOrWhiteSpace($OutputRoot)) {
    Ensure-Directory -Path $defaultOutputRoot
} else {
    Ensure-Directory -Path $OutputRoot
}
$runRoot = Ensure-Directory -Path (Join-Path $outputRootResolved "dlc5-truth-$timestamp")
$decompileRoot = Ensure-Directory -Path (Join-Path $runRoot 'decompile')

$resourceSummary = Get-Dlc5ResourceSummary -Dlc5Root $dlc5Root
$resourceTreePath = Join-Path $runRoot 'dlc5-resource-tree.txt'
$manifestSummaryPath = Join-Path $runRoot 'manifest-summary.txt'
$assemblySummaryPath = Join-Path $runRoot 'assembly-summary.txt'
$resourceSummaryJsonPath = Join-Path $runRoot 'resource-summary.json'
$runSummaryPath = Join-Path $runRoot 'truth-summary.json'
$decompiledPath = Join-Path $decompileRoot 'Assembly-CSharp.decompiled.cs'

$manifestDepots = @()
foreach ($key in $manifest.Keys) {
    if ($key -match '^AppState\.InstalledDepots\.(\d+)\.manifest$') {
        $depotId = $matches[1]
        $depotManifest = $manifest[$key]
        $depotSize = $manifest["AppState.InstalledDepots.$depotId.size"]
        $depotDlcAppId = $manifest["AppState.InstalledDepots.$depotId.dlcappid"]
        $manifestDepots += [pscustomobject]@{
            DepotId = $depotId
            Manifest = $depotManifest
            Size = $depotSize
            DlcAppId = $depotDlcAppId
        }
    }
}

$manifestSummary = @(
    "steamAppsRoot=$steamAppsRoot"
    "manifestPath=$manifestPath"
    "installRoot=$installRoot"
    "appid=$($manifest['AppState.appid'])"
    "branch=$branch"
    "betaKey=$branch"
    "buildid=$buildId"
    "installdir=$installedDir"
    "installedDepotCount=$($manifestDepots.Count)"
    "gameAssembly=$assemblyPath"
    "dlc5Root=$dlc5Root"
    "ilspyRoot=$IlspyRoot"
) -join [Environment]::NewLine
Write-TextFile -Path $manifestSummaryPath -Content $manifestSummary

$assemblyInfo = Get-Item -LiteralPath $assemblyPath
$assemblyHash = (Get-FileHash -LiteralPath $assemblyPath -Algorithm SHA256).Hash
$assemblySummary = @(
    "path=$assemblyPath"
    "length=$($assemblyInfo.Length)"
    "lastWriteTimeUtc=$($assemblyInfo.LastWriteTimeUtc.ToString('o'))"
    "sha256=$assemblyHash"
) -join [Environment]::NewLine
Write-TextFile -Path $assemblySummaryPath -Content $assemblySummary

Write-TextFile -Path $resourceTreePath -Content (($resourceSummary.TreeLines -join [Environment]::NewLine))

$resourceSummaryObject = [pscustomobject]@{
    root = $dlc5Root
    fileCount = $resourceSummary.FileCount
    totalBytes = $resourceSummary.TotalBytes
    folderCounts = $resourceSummary.FolderCounts
    keyFiles = $resourceSummary.KeyFiles
}
$resourceSummaryObject | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $resourceSummaryJsonPath -Encoding utf8

Write-Host "Decompiling Assembly-CSharp.dll with ILSpy..."
Invoke-IlspyDecompile -IlspyRoot $IlspyRoot -AssemblyPath $assemblyPath -OutputPath $decompiledPath

$decompiledInfo = Get-Item -LiteralPath $decompiledPath
$runSummary = [pscustomobject]@{
    timestamp = (Get-Date).ToString('o')
    steamAppsRoot = $steamAppsRoot
    manifestPath = $manifestPath
    installRoot = $installRoot
    appid = $manifest['AppState.appid']
    branch = $branch
    betaKey = $branch
    buildid = $buildId
    installdir = $installedDir
    dlc5Root = $dlc5Root
    assembly = [pscustomobject]@{
        path = $assemblyPath
        sha256 = $assemblyHash
        length = $assemblyInfo.Length
        lastWriteTimeUtc = $assemblyInfo.LastWriteTimeUtc.ToString('o')
    }
    decompiled = [pscustomobject]@{
        path = $decompiledPath
        length = $decompiledInfo.Length
        lastWriteTimeUtc = $decompiledInfo.LastWriteTimeUtc.ToString('o')
    }
    resourceSummary = [pscustomobject]@{
        fileCount = $resourceSummary.FileCount
        totalBytes = $resourceSummary.TotalBytes
        keyFileCount = $resourceSummary.KeyFiles.Count
    }
    manifestDepots = $manifestDepots
}

$runSummary | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $runSummaryPath -Encoding utf8

Write-Host "DLC5 truth collection complete."
Write-Host ("Output: {0}" -f $runRoot)
