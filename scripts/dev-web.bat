@echo off
setlocal EnableExtensions

set "SCRIPT_DIR=%~dp0"
for %%I in ("%SCRIPT_DIR%..") do set "REPO_ROOT=%%~fI"

call "%REPO_ROOT%\dev-web.bat" %*
exit /b %ERRORLEVEL%
