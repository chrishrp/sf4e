@echo off
REM Keeps the soak test alive unattended.
REM
REM If the game dies -- a crash, a silent kill, anything -- this relaunches it
REM within ~15 seconds so an overnight run does not quietly end the moment one
REM process goes away (which is exactly how the 2026-09-15 run was lost).
REM
REM Run it on BOTH PCs, from inside the folder that holds SF4Enhanced.exe.
REM Leave the window open. Close it (or press Ctrl+C) to stop watching.
REM
REM It does NOT touch a healthy game: it only acts when SSFIV.exe is absent.

REM Delayed expansion: the restart counter is incremented and printed inside the
REM same if-block, and %VAR% there would expand at parse time (always stale).
setlocal enabledelayedexpansion
cd /d "%~dp0"

if not exist "SF4Enhanced.exe" (
  echo ERROR: SF4Enhanced.exe is not in this folder.
  echo Put soak-keepalive.cmd next to SF4Enhanced.exe and run it again.
  pause
  exit /b 1
)

echo ================================================
echo  sf4e soak keepalive
echo  Watching SSFIV.exe -- relaunches it if it dies.
echo  Leave this window open. Ctrl+C to stop.
echo ================================================
echo.

set /a RESTARTS=0

:loop
tasklist /FI "IMAGENAME eq SSFIV.exe" 2>nul | find /I "SSFIV.exe" >nul
if errorlevel 1 (
  set /a RESTARTS+=1
  echo [!DATE! !TIME!] game not running - starting it ^(restart #!RESTARTS!^)
  start "" "SF4Enhanced.exe"
  REM Give it time to boot and reach the lobby before checking again, so a slow
  REM start is never mistaken for a second death and double-launched.
  ping -n 46 127.0.0.1 >nul
) else (
  ping -n 16 127.0.0.1 >nul
)
goto loop
