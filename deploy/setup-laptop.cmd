@echo off
REM One-time setup to run the sf4e lobby server permanently on this laptop:
REM   - never sleeps or hibernates, on mains OR battery, and a closed lid does
REM     nothing
REM   - starts the server at boot AND at logon, with NO sign-in required, and
REM     restarts it if it ever stops
REM   - keeps the machine awake even on "modern standby" laptops
REM
REM Just double-click it once. It asks for administrator rights itself.

net session >nul 2>&1
if errorlevel 1 (
  echo Requesting administrator rights...
  powershell -NoProfile -Command "Start-Process -FilePath '%~f0' -Verb RunAs"
  exit /b
)

cd /d "%~dp0"
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0setup-laptop.ps1"

echo.
echo Next: forward the UDP ports above on your router to this machine, and if
echo you use two routers in a row, the first must forward/DMZ to the second.
echo.
pause
