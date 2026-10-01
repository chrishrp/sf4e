# Running your own lobby server

The lobby server is what turns a six-character code into a match. Both games
connect out to it, so no player forwards a port. It hands out codes, runs the
pre-match lobby, checks that both players run the same build, and relays the
match traffic when the two players cannot reach each other directly.

Use this fork's server to enable the shared room records. No hosted server
address is included in the development package. You can run a server for
your own group on a machine reachable by all players and spectators.

## Before you start

**Pick a machine.** A rented Linux VPS is the right choice: it has a fixed
public IP, sits on a fast network, and stays up. One core and 1 GB of RAM are
enough to run it. Building it needs about 2 GB of RAM and a few GB of disk; a
smaller box can run a binary built on a bigger one. Ubuntu 24.04 or Debian 12
are what the scripts are tested on.

**Pick a region.** Every relayed packet travels player → server → player, so
the server's distance counts twice. Put it near the players who will use it,
not in the middle of the world.

**Open the ports.** The server listens on UDP only:

| Ports (UDP)   | Purpose                                   |
|---------------|-------------------------------------------|
| 23400         | matchmaker: create, join, ping            |
| 23401-23420   | one lobby session per port                |
| 24001-24020   | one match relay per lobby                 |
| 25001-25080   | spectator pipes, four ports per lobby     |

Most providers have their own firewall in front of the machine (a security
group, a network firewall, or a control-panel toggle). Those ranges must be
allowed there as well as on the machine itself. A server that only answers
its own `ping` and nothing from outside is almost always this.

## Install on Linux

```
sudo apt-get install -y git
git clone https://github.com/chrishrp/sf4e.git
cd sf4e
deploy/build-linux.sh
deploy/install-service-linux.sh Europe
```

`build-linux.sh` installs the build tools, bootstraps vcpkg and compiles
`build-linux/LobbyServer`. The first build takes a while because it compiles
GameNetworkingSockets and its dependencies; later builds are fast.

`install-service-linux.sh` creates an unprivileged `sf4e` user, installs the
binary and its libraries under `/opt/sf4e`, opens the UDP ranges in `ufw` if it
is present, and registers a systemd service named `sf4e-lobby` that starts at
boot and restarts itself if it ever exits. The optional argument is a label
for this server in the usage statistics.

Day to day:

```
systemctl status sf4e-lobby       # is it running
journalctl -u sf4e-lobby -f       # live log
sudo systemctl restart sf4e-lobby
```

To update, pull the repository, run the two scripts again, and tell your
players to update too: a lobby only accepts the build it was created with.

## Install on Windows

The combined arcade-lobby package includes `LobbyServer.exe` and its runtime
DLLs. Run it from a console on a machine reachable by the players:

1. Extract the whole package; keep the DLLs beside the executable.
2. Allow the UDP ranges above in Windows Firewall and any provider firewall.
   A server behind a router also needs those ports forwarded to it for
   internet play. For a local-network test, use the server machine's LAN IP.
3. Open PowerShell in that folder and run `.\LobbyServer.exe`. Leave the
   console open. Ctrl+C stops it; this direct command does not auto-restart it.
4. Put that machine's reachable address in each client's `server.txt`.

For Remote Desktop, disconnect the session without signing out if you want
the console process to keep running. For unattended operation, configure a
service or a scheduled task under a limited user account. Signing out or
closing the console terminates the console server.

To update, stop the server, extract the new matching fork package, and start
it again. The source repository also has deployment scripts under `deploy/`;
the combined package uses the direct command above.

## Check that it answers

From any other machine, with the server's public address in place of the
example:

In PowerShell:

```powershell
$probe = [Net.Sockets.UdpClient]::new()
$probe.Client.ReceiveTimeout = 3000
try {
    $bytes = [Text.Encoding]::ASCII.GetBytes('{"op":"ping"}')
    $probe.Send($bytes, $bytes.Length, '198.51.100.7', 23400) | Out-Null
    $peer = [Net.IPEndPoint]::new([Net.IPAddress]::Any, 0)
    [Text.Encoding]::ASCII.GetString($probe.Receive([ref]$peer))
} finally {
    $probe.Dispose()
}
```

or on Linux:

```
echo -n '{"op":"ping"}' | nc -u -w2 198.51.100.7 23400
```

A working server answers with something like
`{"capacity":20,"lobbies":0,"ok":true,"version":"1.1.0"}`. If your own
machine answers but a remote one gets nothing, the provider firewall is the
place to look.

## Point players at it

Players never type an address. The launcher reads the server from, in order:

1. `--server host[:port]` on the command line.
2. A file named `server.txt` next to `SF4Enhanced.exe`, with the address on a
   line by itself. Lines starting with `#` are ignored.
3. The address list built into the launcher at packaging time.

For your own group the simplest route is to ship the release folder with a
`server.txt` in it. If you build your own client, `deploy/pack-all.cmd` bakes
the address from `deploy/server.private`, a one-line file you create locally
and never commit. The format is `Name=host[:port]`, and several regions can
be listed separated by `;`; the lobby then lets the player pick one.

Both players must use the same server: a code only exists on the server that
issued it.

## What it stores

* The log (journal on Linux, console on Windows) records lobby creation and
  joins with the player's display name, and at debug level the address each
  connection came from. Set a retention in `journald.conf` if you keep logs.
* If `SF4E_STATS_FILE` is set (the Linux service sets it to
  `/var/lib/sf4e/stats.jsonl`), one JSON line per event is appended: matches
  started and finished, durations, desync reports. No names, no addresses.

## Limits and behaviour

* 20 lobbies per server. Raise `NUM_LOBBIES` in `src/server/lobby_server.cxx`
  and the port ranges grow with it.
* Two players and two spectators per lobby. Players can connect directly to
  each other; spectators are always fed through the server.
* A lobby is released 90 seconds after its last member leaves, and after 12
  hours regardless. One address may hold at most two lobbies at a time.
* The matchmaker answers at most 5 requests per second per address (with a
  burst of 20); anything above is dropped silently.
* Codes are only issued to clients that present a build hash, and joining
  requires the secret handed out with the code, so a lobby cannot be entered
  by guessing its session port.
