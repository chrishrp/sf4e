@echo off
REM sf4e lobby server: opens the firewall, keeps the machine awake, and keeps
REM the server running (restarts it if it ever exits). Safe to run either by
REM double-click (shows a console) or headless from the scheduled task.
REM
REM Ports (all UDP):
REM   23400        matchmaker (create / join by code)
REM   23401-23420  one session per lobby
REM   24001-24020  one GGPO relay per lobby
REM   25001-25080  two spectator pipes per lobby
REM
REM On a rented VPS the provider's own firewall (security group) must allow the
REM same UDP ranges; that is separate from Windows Firewall.

cd /d "%~dp0"

REM Firewall (idempotent).
netsh advfirewall firewall show rule name="sf4e lobby" >nul 2>&1
if errorlevel 1 (
  netsh advfirewall firewall add rule name="sf4e lobby" dir=in action=allow protocol=UDP localport=23400-23420,24001-24020,25001-25080 >nul 2>&1
)

REM Keep the machine awake for as long as the server runs. This matters on
REM modern laptops whose "modern standby" ignores the classic sleep timeouts.
REM The helper exits on its own when this process ends.
if exist "%~dp0keepawake.ps1" start "" /b powershell -NoProfile -ExecutionPolicy Bypass -WindowStyle Hidden -File "%~dp0keepawake.ps1"

:run
echo Starting sf4e lobby server (%date% %time%). Close this window to stop it.
LobbyServer.exe
echo Server exited with code %ERRORLEVEL%. Restarting in 5 seconds...
REM `ping` instead of `timeout`: timeout needs a real console and fails when the
REM task runs this headless.
ping -n 6 127.0.0.1 >nul 2>&1
goto run
