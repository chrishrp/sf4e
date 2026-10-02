@echo off
setlocal
powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0Host-Rematch-Test.ps1" %*
set "helperExitCode=%errorlevel%"
if not "%helperExitCode%"=="0" pause
exit /b %helperExitCode%
