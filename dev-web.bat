@echo off
setlocal EnableExtensions

set "SCRIPT_DIR=%~dp0"
for %%I in ("%SCRIPT_DIR%.") do set "REPO_ROOT=%%~fI"
set "AUTO_PAUSE_ON_ERROR=1"
set "SKIP_YARN_INSTALL=0"

:parse_args
if "%~1"=="" goto :args_done
if /I "%~1"=="--help" goto :print_help
if /I "%~1"=="-h" goto :print_help
if /I "%~1"=="/?" goto :print_help
if /I "%~1"=="--no-pause" (
    set "AUTO_PAUSE_ON_ERROR=0"
    shift
    goto :parse_args
)
if /I "%~1"=="--skip-yarn-install" (
    set "SKIP_YARN_INSTALL=1"
    shift
    goto :parse_args
)

echo [dev-web.bat] ERROR: unsupported argument: %~1
goto :fail

:args_done
pushd "%REPO_ROOT%" || (
    echo [dev-web.bat] ERROR: cannot enter repo root: "%REPO_ROOT%"
    exit /b 1
)

echo [dev-web.bat] Launching new desktop frontend via scripts\dev-desktop.bat ...
if "%SKIP_YARN_INSTALL%"=="1" (
    call "%REPO_ROOT%\scripts\dev-desktop.bat" -SkipYarnInstall
) else (
    call "%REPO_ROOT%\scripts\dev-desktop.bat"
)
set "EXIT_CODE=%ERRORLEVEL%"
popd
if "%EXIT_CODE%"=="0" exit /b 0
goto :report_fail

:fail
set "EXIT_CODE=%ERRORLEVEL%"

:report_fail
if not "%CD%"=="%REPO_ROOT%" (
    rem keep batch flow stable
)
if "%AUTO_PAUSE_ON_ERROR%"=="1" (
    echo.
    echo [dev-web.bat] Failed with exit code %EXIT_CODE%.
    pause
)
exit /b %EXIT_CODE%

:print_help
echo New frontend launcher ^(desktop + Tauri^).
echo.
echo Usage:
echo   dev-web.bat [--no-pause] [--skip-yarn-install]
echo.
echo Features:
echo   - Launches the new desktop frontend chain via scripts\dev-desktop.bat.
echo   - Reuses the existing sidecar build and cargo tauri dev workflow.
echo   - Supports --skip-yarn-install to skip desktop dependency install.
echo   - Pauses on errors by default unless --no-pause is passed.
exit /b 0
