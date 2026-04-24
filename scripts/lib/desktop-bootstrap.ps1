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

function Get-DesktopVersion {
    param(
        [Parameter(Mandatory = $true)][string]$RepoRoot
    )

    $versionPath = Join-Path $RepoRoot "VERSION"
    if (-not (Test-Path -LiteralPath $versionPath)) {
        throw "VERSION file not found: $versionPath"
    }
    $version = (Get-Content -Raw $versionPath).Trim()
    if ($version -notmatch '^\d+\.\d+\.\d+(-[0-9A-Za-z.-]+)?(\+[0-9A-Za-z.-]+)?$') {
        throw "Invalid desktop version in ${versionPath}: $version"
    }
    return $version
}

function Set-TextFileUtf8NoBom {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][string]$Value
    )

    $fullPath = [System.IO.Path]::GetFullPath($Path)
    $utf8NoBom = New-Object System.Text.UTF8Encoding($false)
    [System.IO.File]::WriteAllText($fullPath, $Value, $utf8NoBom)
}

function Sync-DesktopVersion {
    param(
        [Parameter(Mandatory = $true)][string]$RepoRoot
    )

    $version = Get-DesktopVersion -RepoRoot $RepoRoot

    $cargoTomlPath = Join-Path $RepoRoot "src-tauri/Cargo.toml"
    $cargoToml = Get-Content -Raw $cargoTomlPath
    $cargoToml = [regex]::Replace($cargoToml, '(?m)^version\s*=\s*"[^"]+"', "version = `"$version`"", 1)
    Set-TextFileUtf8NoBom -Path $cargoTomlPath -Value $cargoToml

    $tauriConfigPath = Join-Path $RepoRoot "src-tauri/tauri.conf.json"
    $tauriConfig = Get-Content -Raw $tauriConfigPath | ConvertFrom-Json
    $tauriConfig.version = $version
    Set-TextFileUtf8NoBom -Path $tauriConfigPath -Value (($tauriConfig | ConvertTo-Json -Depth 20) + "`r`n")

    $desktopPackagePath = Join-Path $RepoRoot "desktop/package.json"
    $desktopPackage = Get-Content -Raw $desktopPackagePath | ConvertFrom-Json
    $desktopPackage.version = $version
    Set-TextFileUtf8NoBom -Path $desktopPackagePath -Value (($desktopPackage | ConvertTo-Json -Depth 20) + "`r`n")
}

function Assert-DesktopPackageIdentity {
    param(
        [Parameter(Mandatory = $true)][string]$RepoRoot
    )

    $expectedName = "oni-world-filter"
    $cargoTomlPath = Join-Path $RepoRoot "src-tauri/Cargo.toml"
    $cargoToml = Get-Content -Raw $cargoTomlPath
    $cargoName = [regex]::Match($cargoToml, '(?m)^name\s*=\s*"([^"]+)"').Groups[1].Value
    $tauriConfigPath = Join-Path $RepoRoot "src-tauri/tauri.conf.json"
    $tauriConfig = Get-Content -Raw $tauriConfigPath | ConvertFrom-Json
    $desktopPackagePath = Join-Path $RepoRoot "desktop/package.json"
    $desktopPackage = Get-Content -Raw $desktopPackagePath | ConvertFrom-Json

    if ($cargoName -ne $expectedName -or $tauriConfig.productName -ne $expectedName -or $tauriConfig.app.windows[0].title -ne $expectedName -or $desktopPackage.name -ne $expectedName) {
        throw "Desktop package identity mismatch; expected all package names and window title to be '$expectedName'."
    }
}

function Assert-VersionConsistency {
    param(
        [Parameter(Mandatory = $true)][string]$RepoRoot
    )

    $sourceVersion = Get-DesktopVersion -RepoRoot $RepoRoot

    $cargoTomlPath = Join-Path $RepoRoot "src-tauri/Cargo.toml"
    $cargoToml = Get-Content -Raw $cargoTomlPath
    $cargoMatch = [regex]::Match($cargoToml, '(?m)^version\s*=\s*"([^"]+)"')
    if (-not $cargoMatch.Success) {
        throw "Failed to parse version from $cargoTomlPath"
    }
    $cargoVersion = $cargoMatch.Groups[1].Value

    $tauriConfigPath = Join-Path $RepoRoot "src-tauri/tauri.conf.json"
    $tauriVersion = (Get-Content -Raw $tauriConfigPath | ConvertFrom-Json).version
    $desktopPackagePath = Join-Path $RepoRoot "desktop/package.json"
    $desktopVersion = (Get-Content -Raw $desktopPackagePath | ConvertFrom-Json).version

    if ($sourceVersion -ne $cargoVersion -or $sourceVersion -ne $tauriVersion -or $sourceVersion -ne $desktopVersion) {
        throw "Version mismatch: VERSION=$sourceVersion, Cargo.toml=$cargoVersion, tauri.conf.json=$tauriVersion, desktop/package.json=$desktopVersion"
    }
}

function Assert-VsToolchain {
    if (Get-Command cl.exe -ErrorAction SilentlyContinue) {
        return
    }

    $vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
    if (-not (Test-Path -LiteralPath $vswhere)) {
        throw "vswhere.exe not found: $vswhere"
    }

    $installationPath = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if (-not $installationPath) {
        throw "Visual Studio C++ x64 toolchain was not found."
    }

    $vsDevCmd = Join-Path $installationPath "Common7\Tools\VsDevCmd.bat"
    if (-not (Test-Path -LiteralPath $vsDevCmd)) {
        throw "VsDevCmd.bat not found: $vsDevCmd"
    }

    $environmentLines = & cmd /c "`"$vsDevCmd`" -arch=x64 >nul && set"
    if ($LASTEXITCODE -ne 0) {
        throw "Failed to load Visual Studio developer environment."
    }

    foreach ($line in $environmentLines) {
        $separatorIndex = $line.IndexOf("=")
        if ($separatorIndex -lt 1) {
            continue
        }
        $name = $line.Substring(0, $separatorIndex)
        $value = $line.Substring($separatorIndex + 1)
        Set-Item -Path "Env:$name" -Value $value
    }

    if (-not (Get-Command cl.exe -ErrorAction SilentlyContinue)) {
        throw "cl.exe is still unavailable after loading Visual Studio developer environment."
    }
}

function Assert-NodeAndYarn {
    if (-not (Get-Command node -ErrorAction SilentlyContinue)) {
        throw "node is not installed or unavailable in PATH."
    }
    if (-not (Get-Command yarn -ErrorAction SilentlyContinue) -and -not (Get-Command corepack -ErrorAction SilentlyContinue)) {
        throw "Neither yarn nor corepack is available in PATH."
    }
}

function Assert-RustAndCargoTauri {
    if (-not (Get-Command rustc -ErrorAction SilentlyContinue)) {
        throw "rustc is not installed or unavailable in PATH."
    }
    if (-not (Get-Command cargo -ErrorAction SilentlyContinue)) {
        throw "cargo is not installed or unavailable in PATH."
    }
    if (-not (Get-Command cargo-tauri -ErrorAction SilentlyContinue)) {
        throw "cargo-tauri is not installed. Run: cargo install tauri-cli --version '^2.0.0'"
    }
}

function Repair-StaleCMakeCache {
    param(
        [string[]]$Presets = @("x64-debug", "x64-release")
    )

    foreach ($preset in $Presets) {
        $buildDir = Join-Path "out/build" $preset
        $cacheFile = Join-Path $buildDir "CMakeCache.txt"
        if (-not (Test-Path -LiteralPath $cacheFile)) {
            continue
        }

        $cacheContent = Get-Content -Raw $cacheFile
        if ($cacheContent -match "mingw" -or $cacheContent -match "gcc\.exe" -or $cacheContent -match "g\+\+\.exe" -or $cacheContent -match "ld\.exe") {
            Remove-Item -LiteralPath $buildDir -Recurse -Force
        }
    }
}

function Get-SidecarPreset {
    param(
        [Parameter(Mandatory = $true)][ValidateSet("Release")][string]$Configuration
    )

    switch ($Configuration) {
        "Release" { return "x64-release" }
    }
}

function Build-AndSyncSidecar {
    param(
        [Parameter(Mandatory = $true)][string]$RepoRoot,
        [Parameter(Mandatory = $true)][ValidateSet("Release")][string]$Configuration
    )

    $preset = Get-SidecarPreset -Configuration $Configuration
    Repair-StaleCMakeCache -Presets @($preset)

    Write-Host "Configuring native build preset ($preset)..."
    cmake --preset $preset
    if ($LASTEXITCODE -ne 0) {
        throw "cmake --preset $preset failed with exit code $LASTEXITCODE"
    }

    Write-Host "Building oni-sidecar target..."
    cmake --build (Join-Path "out/build" $preset) --target oni-sidecar
    if ($LASTEXITCODE -ne 0) {
        throw "cmake --build out/build/$preset --target oni-sidecar failed with exit code $LASTEXITCODE"
    }

    $source = Resolve-Path (Join-Path $RepoRoot "out/build/$preset/src/oni-sidecar.exe")
    $assetSource = Resolve-Path (Join-Path $RepoRoot "out/build/$preset/src/data.zip")
    $targetDir = Join-Path $RepoRoot "src-tauri/binaries"
    if (-not (Test-Path -LiteralPath $targetDir)) {
        New-Item -ItemType Directory -Path $targetDir | Out-Null
    }
    $target = Join-Path $targetDir "oni-sidecar.exe"
    $assetTarget = Join-Path $targetDir "data.zip"
    Copy-Item -LiteralPath $source -Destination $target -Force
    Copy-Item -LiteralPath $assetSource -Destination $assetTarget -Force
    Write-Host "Sidecar synced to $target"
    return $target
}

function Collect-ReleaseArtifacts {
    param(
        [Parameter(Mandatory = $true)][string]$SourceDirectory,
        [Parameter(Mandatory = $true)][string]$DestinationDirectory,
        [Parameter(Mandatory = $true)][string[]]$Patterns
    )

    if (-not (Test-Path -LiteralPath $SourceDirectory)) {
        throw "Release source directory not found: $SourceDirectory"
    }

    if (-not (Test-Path -LiteralPath $DestinationDirectory)) {
        New-Item -ItemType Directory -Path $DestinationDirectory | Out-Null
    }

    $copiedFiles = @()
    foreach ($pattern in $Patterns) {
        $matches = Get-ChildItem -LiteralPath $SourceDirectory -Filter $pattern -File -Recurse
        foreach ($match in $matches) {
            $destination = Join-Path $DestinationDirectory $match.Name
            Copy-Item -LiteralPath $match.FullName -Destination $destination -Force
            $copiedFiles += $destination
        }
    }

    if ($copiedFiles.Count -eq 0) {
        throw "No release artifacts matched under $SourceDirectory"
    }

    return $copiedFiles
}

function Invoke-OptionalCodeSigning {
    param(
        [string]$SigningProfile = "unsigned",
        [string[]]$Files = @()
    )

    if ($SigningProfile -eq "unsigned") {
        return
    }

    if ($Files.Count -eq 0) {
        throw "Signing profile '$SigningProfile' requested without files."
    }

    $signCommand = $env:ONI_SIGN_COMMAND
    if (-not $signCommand) {
        throw "Signing profile '$SigningProfile' requested, but ONI_SIGN_COMMAND is not configured."
    }

    foreach ($file in $Files) {
        & cmd /c "`"$signCommand`" `"$file`""
        if ($LASTEXITCODE -ne 0) {
            throw "Code signing failed for $file with exit code $LASTEXITCODE"
        }
    }
}
