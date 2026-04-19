@echo off
setlocal EnableExtensions

set "SCRIPT_DIR=%~dp0"
for %%I in ("%SCRIPT_DIR%..") do set "REPO_ROOT=%%~fI"
set "PS_SCRIPT=%REPO_ROOT%\scripts\dev-desktop.ps1"

if /I "%~1"=="--help" goto :print_help
if /I "%~1"=="-h" goto :print_help
if /I "%~1"=="/?" goto :print_help

if not exist "%PS_SCRIPT%" (
    echo [dev-desktop.bat] ERROR: script not found: "%PS_SCRIPT%"
    exit /b 1
)

call :ensure_vs_environment || exit /b 1
call :ensure_python || exit /b 1
call :repair_cmake_cache || exit /b 1

powershell -NoProfile -ExecutionPolicy Bypass -File "%PS_SCRIPT%" %*
exit /b %ERRORLEVEL%

:ensure_vs_environment
where cl >nul 2>&1
if not errorlevel 1 exit /b 0

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
    echo [dev-desktop.bat] ERROR: cl.exe unavailable and vswhere not found: "%VSWHERE%"
    exit /b 1
)

set "VS_INSTALL="
for /f "usebackq delims=" %%I in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do (
    set "VS_INSTALL=%%I"
)
if not defined VS_INSTALL (
    echo [dev-desktop.bat] ERROR: cannot locate Visual Studio C++ toolchain via vswhere.
    exit /b 1
)

set "VSDEVCMD=%VS_INSTALL%\Common7\Tools\VsDevCmd.bat"
if not exist "%VSDEVCMD%" (
    echo [dev-desktop.bat] ERROR: VsDevCmd.bat not found: "%VSDEVCMD%"
    exit /b 1
)

echo [dev-desktop.bat] Loading Visual Studio developer environment...
call "%VSDEVCMD%" -arch=x64 >nul
if errorlevel 1 (
    echo [dev-desktop.bat] ERROR: failed to load VS developer environment.
    exit /b 1
)

where cl >nul 2>&1
if errorlevel 1 (
    echo [dev-desktop.bat] ERROR: cl.exe is still unavailable after loading VS environment.
    exit /b 1
)
exit /b 0

:ensure_python
where python >nul 2>&1
if not errorlevel 1 (
    python --version >nul 2>&1
    if not errorlevel 1 exit /b 0
)

if defined EMSDK_PYTHON exit /b 0

where py >nul 2>&1
if errorlevel 1 (
    echo [dev-desktop.bat] ERROR: neither python nor py launcher is available in PATH.
    exit /b 1
)

set "PY_LAUNCHER="
for /f "delims=" %%I in ('where py') do (
    set "PY_LAUNCHER=%%~fI"
    goto :python_resolved
)

:python_resolved
if not defined PY_LAUNCHER (
    echo [dev-desktop.bat] ERROR: py launcher path resolve failed.
    exit /b 1
)
set "EMSDK_PYTHON=%PY_LAUNCHER%"
echo [dev-desktop.bat] python not found, fallback EMSDK_PYTHON=%EMSDK_PYTHON%
exit /b 0

:repair_cmake_cache
set "CACHE_FILE=%REPO_ROOT%\out\build\x64-release\CMakeCache.txt"
if not exist "%CACHE_FILE%" exit /b 0

findstr /I /C:"CMAKE_LINKER:FILEPATH=C:/mingw64/bin/ld.exe" "%CACHE_FILE%" >nul 2>&1
if errorlevel 1 exit /b 0

echo [dev-desktop.bat] Detected stale MinGW linker cache, clearing x64-release CMakeCache.txt...
del /f /q "%CACHE_FILE%" >nul 2>&1
if exist "%CACHE_FILE%" (
    echo [dev-desktop.bat] ERROR: failed to clear stale cache: "%CACHE_FILE%"
    exit /b 1
)
exit /b 0

:print_help
echo One-click desktop integrated debug launcher ^(Windows CMD entry^).
echo.
echo Usage:
echo   scripts\dev-desktop.bat [--help] [PowerShell args]
echo.
echo Features:
echo   - Auto load VS C++ developer environment when cl.exe is missing.
echo   - Auto fallback EMSDK_PYTHON to py.exe when python is missing.
echo   - Auto clear stale x64-release CMake linker cache ^(MinGW ld.exe^).
echo   - Forward all args to scripts\dev-desktop.ps1.
echo.
echo Examples:
echo   scripts\dev-desktop.bat
echo   scripts\dev-desktop.bat -SkipYarnInstall
exit /b 0
