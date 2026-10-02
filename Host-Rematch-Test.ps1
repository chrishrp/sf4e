# Run beside the matching LobbyServer.exe, SF4Enhanced.exe and runtime DLLs.
# This helper owns only the server recorded in test-server-logs/server-state.json.
[CmdletBinding()]
param(
    [switch]$ServerOnly,
    [switch]$Stop
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$expectedVersion = '1.2.0'
$serverPort = 23400
$packageDirectory = [IO.Path]::GetFullPath($PSScriptRoot)
$serverPath = Join-Path $packageDirectory 'LobbyServer.exe'
$launcherPath = Join-Path $packageDirectory 'SF4Enhanced.exe'
$logDirectory = Join-Path $packageDirectory 'test-server-logs'
$statePath = Join-Path $logDirectory 'server-state.json'
$mutex = $null
$ownsMutex = $false
$exitCode = 0

function Get-OwnedServer {
    if (-not (Test-Path -LiteralPath $statePath -PathType Leaf)) { return $null }
    try {
        $record = Get-Content -LiteralPath $statePath -Raw | ConvertFrom-Json
        $savedProcessId = [int]$record.ProcessId
        $savedStartTicks = [long]$record.StartTimeUtcTicks
        $savedPath = [string]$record.ExecutablePath
        if ($savedProcessId -le 0 -or $savedStartTicks -le 0 -or
            -not [string]::Equals($savedPath, $serverPath, [StringComparison]::OrdinalIgnoreCase)) {
            throw 'The saved process identity is not valid for this package.'
        }
    }
    catch {
        throw "Cannot validate '$statePath'. No process will be stopped. $($_.Exception.Message)"
    }

    $candidate = Get-Process -Id $savedProcessId -ErrorAction SilentlyContinue
    if ($null -ne $candidate) {
        try {
            # Retain the process handle before checking identity. A reused PID
            # must never turn a later Kill() into a stop of another process.
            $null = $candidate.Handle
            if (-not $candidate.HasExited -and
                [string]::Equals($candidate.Path, $serverPath, [StringComparison]::OrdinalIgnoreCase) -and
                $candidate.StartTime.ToUniversalTime().Ticks -eq $savedStartTicks) {
                return $candidate
            }
        }
        catch {
            $candidate.Dispose()
            throw "Cannot verify the saved server process. No process will be stopped. $($_.Exception.Message)"
        }
        $candidate.Dispose()
    }

    # The old server exited, or Windows reused its PID. Discard only our stale
    # metadata; do not touch any currently running process with that PID.
    Remove-Item -LiteralPath $statePath -Force
    Write-Host 'The previously recorded server is no longer running.'
    return $null
}

function Stop-OwnedServer([Diagnostics.Process]$ServerProcess) {
    if (-not $ServerProcess.HasExited) {
        # The caller obtained and pinned this exact process through an identity
        # check, or just created it. Never search or stop processes by name.
        $ServerProcess.Kill()
        if (-not $ServerProcess.WaitForExit(5000)) {
            throw "The owned server did not stop within five seconds (PID $($ServerProcess.Id))."
        }
    }
    if (Test-Path -LiteralPath $statePath -PathType Leaf) {
        Remove-Item -LiteralPath $statePath -Force
    }
}

function Assert-MatchmakerPortFree {
    $probe = New-Object Net.Sockets.UdpClient([Net.Sockets.AddressFamily]::InterNetwork)
    try {
        $probe.ExclusiveAddressUse = $true
        $probe.Client.Bind((New-Object Net.IPEndPoint([Net.IPAddress]::Any, $serverPort)))
    }
    catch {
        throw "UDP port $serverPort is already in use by a server this package does not own. Close that server yourself, or use its existing package. No process was stopped."
    }
    finally { $probe.Dispose() }
}

function Wait-ForServer([Diagnostics.Process]$ServerProcess) {
    $timer = [Diagnostics.Stopwatch]::StartNew()
    while ($timer.Elapsed.TotalSeconds -lt 15) {
        if ($ServerProcess.HasExited) {
            throw "LobbyServer.exe exited before becoming ready (exit $($ServerProcess.ExitCode)). Check '$logDirectory'. A required UDP port may already be in use."
        }
        $probe = New-Object Net.Sockets.UdpClient([Net.Sockets.AddressFamily]::InterNetwork)
        $reply = $null
        try {
            $probe.Client.ReceiveTimeout = 500
            # Connect filters incoming datagrams to this exact loopback peer.
            $probe.Connect('127.0.0.1', $serverPort)
            $request = [Text.Encoding]::ASCII.GetBytes('{"op":"ping"}')
            $null = $probe.Send($request, $request.Length)
            $remote = New-Object Net.IPEndPoint([Net.IPAddress]::Any, 0)
            $bytes = $probe.Receive([ref]$remote)
            $reply = [Text.Encoding]::UTF8.GetString($bytes) | ConvertFrom-Json
        }
        catch [Net.Sockets.SocketException] { }
        catch [ArgumentException] { }
        finally { $probe.Dispose() }

        if ($null -ne $reply) {
            if (-not ($reply.PSObject.Properties.Name -contains 'ok') -or $reply.ok -ne $true -or
                -not ($reply.PSObject.Properties.Name -contains 'version') -or $reply.version -ne $expectedVersion) {
                throw "UDP port $serverPort answered with an unexpected server version; this package requires $expectedVersion."
            }
            if ($ServerProcess.HasExited) { throw 'The owned server exited while readiness was being checked.' }
            return
        }
        Start-Sleep -Milliseconds 100
    }
    throw "The server did not answer its local ping within 15 seconds. Check '$logDirectory'."
}

try {
    if ($ServerOnly -and $Stop) { throw 'Choose either -ServerOnly or -Stop.' }

    # Serialize startup/stop across packages in this Windows session. Different
    # sessions are still protected against taking over an occupied UDP port.
    $mutex = New-Object Threading.Mutex($false, 'Local\SF4Enhanced.RematchTest.Udp23400')
    try { $ownsMutex = $mutex.WaitOne(25000) }
    catch [Threading.AbandonedMutexException] { $ownsMutex = $true }
    if (-not $ownsMutex) { throw 'Another host helper is still starting or stopping the server. Try again shortly.' }

    $serverProcess = Get-OwnedServer
    if ($Stop) {
        if ($null -eq $serverProcess) {
            Write-Host 'No server owned by this package is running.'
        }
        else {
            try {
                Stop-OwnedServer $serverProcess
                Write-Host 'Stopped this package''s test server.'
            }
            finally { $serverProcess.Dispose() }
        }
    }
    else {
        if (-not (Test-Path -LiteralPath $serverPath -PathType Leaf)) {
            throw 'LobbyServer.exe is missing. Extract the entire test package before running this helper.'
        }
        if (-not $ServerOnly -and -not (Test-Path -LiteralPath $launcherPath -PathType Leaf)) {
            throw 'SF4Enhanced.exe is missing. Extract the entire test package before running this helper.'
        }

        $createdServer = $false
        try {
            if ($null -eq $serverProcess) {
                Assert-MatchmakerPortFree
                $null = New-Item -ItemType Directory -Path $logDirectory -Force
                $logName = 'server-' + (Get-Date -Format 'yyyyMMdd-HHmmss-fff')
                $serverProcess = Start-Process -FilePath $serverPath -WorkingDirectory $packageDirectory -WindowStyle Hidden -PassThru `
                    -RedirectStandardOutput (Join-Path $logDirectory ($logName + '.stdout.log')) `
                    -RedirectStandardError (Join-Path $logDirectory ($logName + '.stderr.log'))
                $createdServer = $true
                $null = $serverProcess.Handle
                $record = [ordered]@{
                    ProcessId = $serverProcess.Id
                    ExecutablePath = $serverPath
                    StartTimeUtcTicks = $serverProcess.StartTime.ToUniversalTime().Ticks.ToString()
                }
                $temporaryState = $statePath + '.' + [Guid]::NewGuid().ToString('N') + '.tmp'
                try {
                    $record | ConvertTo-Json | Set-Content -LiteralPath $temporaryState -Encoding UTF8
                    Move-Item -LiteralPath $temporaryState -Destination $statePath -Force
                }
                finally {
                    if (Test-Path -LiteralPath $temporaryState) { Remove-Item -LiteralPath $temporaryState -Force }
                }
            }

            Wait-ForServer $serverProcess
            if ($createdServer) { Write-Host "Started test server $expectedVersion (PID $($serverProcess.Id))." }
            else { Write-Host "Reusing this package's test server $expectedVersion (PID $($serverProcess.Id))." }
        }
        catch {
            # Roll back only a server created by this invocation. An existing
            # owned server may still be serving players if its ping timed out.
            if ($createdServer -and $null -ne $serverProcess) {
                try { Stop-OwnedServer $serverProcess }
                catch { Write-Warning "Could not stop the newly created server: $($_.Exception.Message)" }
            }
            throw
        }
        finally {
            if ($null -ne $serverProcess) { $serverProcess.Dispose() }
        }

        Write-Host 'Local server: 127.0.0.1:23400. Keep this PC awake while friends play.'
        Write-Host 'Use Stop-Rematch-Test.cmd when finished; logs are in test-server-logs.'
        if (-not $ServerOnly) {
            $null = Start-Process -FilePath $launcherPath -WorkingDirectory $packageDirectory `
                -ArgumentList '--server', '127.0.0.1:23400', '--instant-rematch'
        }
    }
}
catch {
    [Console]::Error.WriteLine('Host test: ' + $_.Exception.Message)
    $exitCode = 1
}
finally {
    if ($ownsMutex) { $mutex.ReleaseMutex() }
    if ($null -ne $mutex) { $mutex.Dispose() }
}
exit $exitCode
