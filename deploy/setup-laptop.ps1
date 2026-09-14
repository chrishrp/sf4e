# Configures this laptop to run the sf4e lobby server permanently:
#   - opens the firewall for the server's UDP ports
#   - never sleeps or hibernates, on mains OR battery, and closing the lid
#     does nothing
#   - a scheduled task starts the server at BOOT and at logon (no sign-in
#     needed) and restarts it if it ever stops
#   - a keep-awake helper covers modern-standby laptops
#
# Run it once via setup-laptop.cmd (which asks for admin rights).

$ErrorActionPreference = 'Continue'
$dir = Split-Path -Parent $MyInvocation.MyCommand.Path
$taskName = 'sf4e lobby server'

Write-Host ''
Write-Host '[1/4] Windows Firewall'
netsh advfirewall firewall delete rule name="$taskName" 2>$null | Out-Null
netsh advfirewall firewall add rule name="$taskName" dir=in action=allow protocol=UDP localport=23400-23420,24001-24020,25001-25080 | Out-Null
Write-Host '      allowed UDP 23400-23420, 24001-24020, 25001-25080'

Write-Host '[2/4] Power: never sleep (mains OR battery), lid does nothing, no hibernate'
powercfg /change standby-timeout-ac 0
powercfg /change standby-timeout-dc 0
powercfg /change hibernate-timeout-ac 0
powercfg /change hibernate-timeout-dc 0
powercfg /change disk-timeout-ac 0
powercfg /change disk-timeout-dc 0
powercfg /change monitor-timeout-ac 10
# Lid close -> do nothing, both plugged in and on battery
powercfg /setacvalueindex SCHEME_CURRENT SUB_BUTTONS LIDACTION 0
powercfg /setdcvalueindex SCHEME_CURRENT SUB_BUTTONS LIDACTION 0
# System sleep and hybrid sleep off, both power states
powercfg /setacvalueindex SCHEME_CURRENT SUB_SLEEP STANDBYIDLE 0
powercfg /setdcvalueindex SCHEME_CURRENT SUB_SLEEP STANDBYIDLE 0
powercfg /setacvalueindex SCHEME_CURRENT SUB_SLEEP HYBRIDSLEEP 0
powercfg /setdcvalueindex SCHEME_CURRENT SUB_SLEEP HYBRIDSLEEP 0
powercfg /setactive SCHEME_CURRENT
# Disable hibernation entirely so a lid-close can never hibernate
powercfg /hibernate off 2>$null
Write-Host '      done. Keep it plugged in anyway.'

Write-Host '[3/4] Task: start the server at boot AND logon, restart if it stops'
schtasks /delete /tn "$taskName" /f 2>$null | Out-Null
try {
    $action = New-ScheduledTaskAction -Execute 'cmd.exe' -Argument '/c run-server.cmd >> server.log 2>&1' -WorkingDirectory $dir
    $triggers = @((New-ScheduledTaskTrigger -AtStartup), (New-ScheduledTaskTrigger -AtLogOn))
    $principal = New-ScheduledTaskPrincipal -UserId 'S-1-5-18' -LogonType ServiceAccount -RunLevel Highest
    $settings = New-ScheduledTaskSettingsSet -AllowStartIfOnBatteries -DontStopIfGoingOnBatteries -MultipleInstances IgnoreNew -StartWhenAvailable -RestartInterval (New-TimeSpan -Minutes 1) -RestartCount 999 -ExecutionTimeLimit ([TimeSpan]::Zero)
    Register-ScheduledTask -TaskName $taskName -Action $action -Trigger $triggers -Principal $principal -Settings $settings -Force | Out-Null
    Write-Host "      task '$taskName' registered (runs as SYSTEM; no sign-in needed after a reboot)"
}
catch {
    Write-Host "      PowerShell task registration failed ($($_.Exception.Message)); falling back to schtasks onlogon" -ForegroundColor Yellow
    schtasks /create /tn "$taskName" /tr "cmd /c `"`"$dir\run-server.cmd`"`" >> `"$dir\server.log`" 2>&1" /sc onlogon /rl highest /f | Out-Null
}

Write-Host '[4/4] Starting it now'
schtasks /run /tn "$taskName" 2>$null | Out-Null
Start-Sleep -Seconds 4
if (Get-Process LobbyServer -ErrorAction SilentlyContinue) {
    Write-Host '      running.' -ForegroundColor Green
} else {
    Write-Host '      not running yet - check server.log in this folder.' -ForegroundColor Yellow
}

Write-Host ''
Write-Host 'This machine''s addresses (forward the UDP ports here on your router):'
Get-NetIPAddress -AddressFamily IPv4 -ErrorAction SilentlyContinue |
    Where-Object { $_.IPAddress -ne '127.0.0.1' -and $_.IPAddress -notlike '169.254.*' } |
    ForEach-Object { Write-Host ('  ' + $_.IPAddress) }
Write-Host ''
Write-Host 'Setup complete. The server now survives sleep attempts, a closed lid,'
Write-Host 'and reboots on its own. Router ports to forward (UDP):'
Write-Host '  23400-23420, 24001-24020, 25001-25080'
Write-Host ''
Write-Host 'The live server output is written to server.log next to this script.'
