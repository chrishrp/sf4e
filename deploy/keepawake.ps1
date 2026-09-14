# Keeps the machine from sleeping while the lobby server runs. Launched hidden
# by run-server.cmd; re-asserts the "system required" execution state every
# minute, which stops both classic idle sleep and modern-standby low-power idle.
# It exits when its parent (run-server.cmd) does.
Add-Type -Namespace Sf4e -Name Power -MemberDefinition @'
[System.Runtime.InteropServices.DllImport("kernel32.dll")]
public static extern uint SetThreadExecutionState(uint esFlags);
'@
while ($true) {
    # ES_CONTINUOUS (0x80000000) | ES_SYSTEM_REQUIRED (0x00000001)
    [Sf4e.Power]::SetThreadExecutionState(0x80000001) | Out-Null
    Start-Sleep -Seconds 60
}
