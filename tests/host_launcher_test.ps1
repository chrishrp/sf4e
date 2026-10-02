# Runs the production helper's functions and main block with process/network
# cmdlets mocked. No real game, server, UDP port, or process is changed.
[CmdletBinding()]
param()
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$helperPath = Join-Path (Split-Path -Parent $PSScriptRoot) 'Host-Rematch-Test.ps1'
$tokens = $null
$parseErrors = $null
$ast = [Management.Automation.Language.Parser]::ParseFile($helperPath, [ref]$tokens, [ref]$parseErrors)
if ($parseErrors.Count -ne 0) { throw ($parseErrors | Out-String) }
foreach ($statement in $ast.EndBlock.Statements) {
    if ($statement -is [Management.Automation.Language.FunctionDefinitionAst]) {
        . ([ScriptBlock]::Create($statement.Extent.Text))
    }
}
$mainText = ($ast.EndBlock.Statements | Where-Object { $_ -is [Management.Automation.Language.TryStatementAst] }).Extent.Text
$mainText = $mainText.Replace('Local\SF4Enhanced.RematchTest.Udp23400', ('Local\SF4Enhanced.HelperRegression.' + [Guid]::NewGuid().ToString('N')))
$mainBlock = [ScriptBlock]::Create($mainText)
$fixtureRoot = Join-Path ([IO.Path]::GetTempPath()) ('sf4e-helper-test-' + [Guid]::NewGuid().ToString('N'))
$null = New-Item -ItemType Directory -Path $fixtureRoot
$script:caseNumber = 0
$script:fixture = $null

function Require([bool]$Condition, [string]$Message) {
    if (-not $Condition) { throw ('FAIL: ' + $Message) }
}
function New-Fixture {
    $script:caseNumber += 1
    $caseDirectory = Join-Path $fixtureRoot $script:caseNumber
    $newPackage = Join-Path $caseDirectory 'new'
    $oldPackage = Join-Path $caseDirectory 'old'
    $null = New-Item -ItemType Directory -Path $newPackage, $oldPackage
    Set-Content -LiteralPath (Join-Path $newPackage 'LobbyServer.exe') -Value 'matching server fixture'
    Set-Content -LiteralPath (Join-Path $oldPackage 'LobbyServer.exe') -Value 'matching server fixture'
    Set-Content -LiteralPath (Join-Path $newPackage 'SF4Enhanced.exe') -Value 'launcher fixture'
    $script:fixture = @{
        Package = $newPackage; OldPackage = $oldPackage; Endpoints = @(8123)
        NextEndpoints = $null; EndpointCalls = 0; HandlePins = 0
        HandleFails = $false; Stops = 0; Disposes = 0; Pings = 0
        PingFails = $false; Starts = 0; Launches = @(); PortProbes = 0
    }
    $process = [pscustomobject]@{
        Id = 8123; Path = (Join-Path $oldPackage 'LobbyServer.exe')
        HasExited = $false; StartTime = [datetime]'2026-01-01T01:02:03Z'
    }
    $process | Add-Member ScriptProperty Handle {
        $script:fixture.HandlePins += 1
        if ($script:fixture.HandleFails) { throw 'Fixture process access denied.' }
        return 123
    }
    $process | Add-Member ScriptMethod Dispose { $script:fixture.Disposes += 1 }
    $script:fixture.Process = $process
}
function Get-NetUDPEndpoint {
    [CmdletBinding()] param([int]$LocalPort)
    Require ($LocalPort -eq 23400) 'Production port should remain unchanged.'
    $script:fixture.EndpointCalls += 1
    $owners = $script:fixture.Endpoints
    if ($script:fixture.EndpointCalls -gt 1 -and $null -ne $script:fixture.NextEndpoints) {
        $owners = $script:fixture.NextEndpoints
    }
    foreach ($owner in $owners) { [pscustomobject]@{ OwningProcess = $owner } }
}
function Get-Process {
    [CmdletBinding()] param([int]$Id)
    if ($Id -eq $script:fixture.Process.Id) { return $script:fixture.Process }
}
function Wait-ForServer($ServerProcess) {
    $script:fixture.Pings += 1
    Require ($script:fixture.HandlePins -gt 0) 'Health check must follow a pinned-process identity check.'
    if ($script:fixture.PingFails) { throw 'Fixture version/readiness check rejected the server.' }
}
function Assert-MatchmakerPortFree { $script:fixture.PortProbes += 1 }
function Stop-OwnedServer($ServerProcess) {
    $script:fixture.Stops += 1
    if (Test-Path -LiteralPath $statePath) { Remove-Item -LiteralPath $statePath }
}
function Start-Process {
    [CmdletBinding()] param(
        [string]$FilePath, [string]$WorkingDirectory, [string]$WindowStyle,
        [switch]$PassThru, [string]$RedirectStandardOutput,
        [string]$RedirectStandardError, [string[]]$ArgumentList
    )
    if ($FilePath -eq (Join-Path $script:fixture.Package 'LobbyServer.exe')) {
        $script:fixture.Starts += 1
        $script:fixture.Process.Path = $FilePath
        return $script:fixture.Process
    }
    $script:fixture.Launches += [pscustomobject]@{ Path = $FilePath; Args = $ArgumentList }
}
function Invoke-Helper([switch]$Stop, [switch]$ServerOnly) {
    $expectedVersion = '1.2.0'
    $serverPort = 23400
    $packageDirectory = $script:fixture.Package
    $serverPath = Join-Path $packageDirectory 'LobbyServer.exe'
    $launcherPath = Join-Path $packageDirectory 'SF4Enhanced.exe'
    $logDirectory = Join-Path $packageDirectory 'test-server-logs'
    $statePath = Join-Path $logDirectory 'server-state.json'
    $mutex = $null
    $ownsMutex = $false
    $exitCode = 0
    . $mainBlock
    return $exitCode
}
function Assert-Unowned {
    Require ($script:fixture.Stops -eq 0) 'Borrowed or rejected processes must never be stopped.'
    Require ($script:fixture.Starts -eq 0) 'Borrowing or rejecting must not create another server.'
    Require (-not (Test-Path -LiteralPath (Join-Path $script:fixture.Package 'test-server-logs/server-state.json'))) 'Borrowed process must not acquire ownership metadata.'
}
try {
    New-Fixture
    Require ((Invoke-Helper) -eq 0) 'Identical server in another folder should be reused.'
    Require ($script:fixture.Pings -eq 1) 'Reused server must pass the normal version/readiness check.'
    Require ($script:fixture.EndpointCalls -eq 2) 'UDP ownership must be checked again after hashing.'
    Require ($script:fixture.Disposes -eq 1) 'Borrowed process handle must be disposed.'
    Require ($script:fixture.Launches.Count -eq 1) 'Current client should launch exactly once.'
    Require ($script:fixture.Launches[0].Path -eq (Join-Path $script:fixture.Package 'SF4Enhanced.exe')) 'Launch the new package client.'
    Require (($script:fixture.Launches[0].Args -join ' ') -eq '--server 127.0.0.1:23400 --instant-rematch') 'Launch with the local server and instant-rematch flag.'
    Assert-Unowned
    $checksBeforeStop = $script:fixture.EndpointCalls
    Require ((Invoke-Helper -Stop) -eq 0) 'Stop with no owned server should succeed harmlessly.'
    Require ($script:fixture.EndpointCalls -eq $checksBeforeStop) 'Stop must not search for a server to borrow.'
    Assert-Unowned

    New-Fixture
    Set-Content -LiteralPath $script:fixture.Process.Path -Value 'different executable, same name'
    Require ((Invoke-Helper) -eq 1) 'A different executable must not be reused.'
    Require ($script:fixture.Pings -eq 0 -and $script:fixture.Launches.Count -eq 0) 'Reject incompatible server before health check or launch.'
    Require ($script:fixture.Disposes -eq 1) 'Rejected process handle must be disposed.'
    Assert-Unowned

    New-Fixture
    $script:fixture.Endpoints = @(8123, 8124)
    Require ((Invoke-Helper) -eq 1) 'Multiple port owners must be rejected.'
    Assert-Unowned

    New-Fixture
    $script:fixture.NextEndpoints = @(8124)
    Require ((Invoke-Helper) -eq 1) 'Port ownership changing during verification must be rejected.'
    Require ($script:fixture.Disposes -eq 1) 'Changed-owner rejection must dispose the pinned handle.'
    Assert-Unowned

    New-Fixture
    $script:fixture.HandleFails = $true
    Require ((Invoke-Helper) -eq 1) 'An unpinnable process must be rejected.'
    Assert-Unowned

    New-Fixture
    $script:fixture.PingFails = $true
    Require ((Invoke-Helper) -eq 1) 'Borrowed server readiness failure must abort launch.'
    Require ($script:fixture.Launches.Count -eq 0) 'An unhealthy borrowed server must not launch the client.'
    Assert-Unowned

    New-Fixture
    $script:fixture.Endpoints = @()
    Require ((Invoke-Helper -ServerOnly) -eq 0) 'An unoccupied port should still start an owned server.'
    Require ($script:fixture.Starts -eq 1 -and $script:fixture.PortProbes -eq 1) 'New owned startup must retain the exclusive port check.'
    $ownedState = Join-Path $script:fixture.Package 'test-server-logs/server-state.json'
    Require (Test-Path -LiteralPath $ownedState) 'New server must get ownership metadata.'
    Require ((Invoke-Helper -Stop) -eq 0 -and $script:fixture.Stops -eq 1) 'Matching owned PID, path, and start time must remain stoppable.'

    New-Fixture
    $script:fixture.Endpoints = @()
    Require ((Invoke-Helper -ServerOnly) -eq 0) 'Create an owned fixture for stale-start-time check.'
    $script:fixture.Process.StartTime = $script:fixture.Process.StartTime.AddSeconds(1)
    Require ((Invoke-Helper -Stop) -eq 0 -and $script:fixture.Stops -eq 0) 'A reused PID with a different start time must not be stopped.'

    New-Fixture
    $script:fixture.Endpoints = @()
    Require ((Invoke-Helper -ServerOnly) -eq 0) 'Create an owned fixture for stale-path check.'
    $script:fixture.Process.Path = Join-Path $script:fixture.OldPackage 'LobbyServer.exe'
    Require ((Invoke-Helper -Stop) -eq 0 -and $script:fixture.Stops -eq 0) 'A process with a different executable path must not be stopped.'
    Write-Host 'PASS: borrowed-server reuse, SHA256/owner checks, ownership isolation, failure cleanup, current-client launch, and owned PID/path/start-time safeguards.'
}
finally {
    # This exact GUID-named directory was created above solely for this test.
    $resolvedFixture = [IO.Path]::GetFullPath($fixtureRoot)
    $resolvedTemp = [IO.Path]::GetFullPath([IO.Path]::GetTempPath()).TrimEnd('\') + '\'
    if ($resolvedFixture.StartsWith($resolvedTemp, [StringComparison]::OrdinalIgnoreCase) -and
        (Split-Path -Leaf $resolvedFixture) -like 'sf4e-helper-test-*') {
        Remove-Item -LiteralPath $resolvedFixture -Recurse -Force
    }
}