@echo off
setlocal
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0pack-release.ps1" %*
set "packExitCode=%ERRORLEVEL%"
echo.
pause
exit /b %packExitCode%
