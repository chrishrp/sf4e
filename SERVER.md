# Running the sf4e lobby server

Players never forward ports. Both games connect *out* to this server: it hands
out six-character lobby codes, runs the pre-match lobby, and relays the
rollback traffic between the two players unless they connect peer to peer.
One small VPS runs it.

## Linux (recommended)

On a fresh Ubuntu or Debian box, clone the repository and run
`deploy/build-linux.sh`, then `deploy/install-service-linux.sh`. The second
script creates an unprivileged `sf4e` user, installs to `/opt/sf4e`, opens the
UDP ports in `ufw` and registers a hardened systemd unit. Operate it with
`systemctl status sf4e-lobby` and `journalctl -u sf4e-lobby -f`. The
journal contains player display names and IP addresses; set a retention in
`journald.conf` if you keep it. Building needs about 2 GB of RAM and a few
GB of disk for the dependencies; a 1 GB box can run a binary built elsewhere.

The rest of this guide covers the Windows package.

## What you need

* A Windows Server VPS. Any provider works; 1 vCPU and 1 GB of RAM is plenty.
  Pick a region roughly between your players, since every packet takes one hop
  through it.
* Its public IP address.
* In the provider's firewall (often called a security group), allow inbound
  **UDP 23400-23420**, **UDP 24001-24020** and **UDP 25001-25080**.

## Installing

1. Copy the `sf4e-server` folder to the VPS, for example onto the Desktop.
2. Right-click `run-server.cmd` and choose *Run as administrator*.

That opens Windows Firewall for the ports above and starts the server in a
console window. Leave the window open. If the server ever exits, the script
restarts it after five seconds.

You should see a line like:

```
sf4e lobby server up: matchmaker udp/23400, sessions udp/23401-23420, relays udp/24001-24020, spectator pipes udp/25001-25080, 20 lobbies
```

To keep it running after you disconnect from Remote Desktop, sign out instead
of closing the window, or run it as a scheduled task that starts at boot.

## Pointing players at it

In the player package, edit `server.txt` and put the VPS's IP on a line by
itself:

```
203.0.113.5
```

Zip the folder and send it. Players extract, double-click `Launcher.exe`, and
see *Create lobby* and *Join with code*.

## Updating a running server

Extract the new `sf4e-server.zip` over the folder the server runs from,
replacing the files, then run `update-server.cmd`. It refreshes the firewall
rule for any new ports and restarts the server. Players must update too:
lobbies only accept the build they were created with.

## Capacity and limits

* 20 lobbies at once. Change `NUM_LOBBIES` in `src/server/lobby_server.cxx` to
  raise it; the port ranges grow with it.
* Two players and up to two spectators per lobby. Each spectator gets a
  relay pipe of its own (two ports), fed by player 1's game.
* Empty lobbies are released after 90 seconds.
* Both players must run the same sf4e build, checked automatically.

## Checking it from your PC

From any machine, this prints how many lobbies are in use:

```powershell
$u = New-Object System.Net.Sockets.UdpClient
$b = [Text.Encoding]::ASCII.GetBytes('{"op":"ping"}')
$u.Send($b, $b.Length, 'YOUR.VPS.IP', 23400) | Out-Null
$ep = New-Object System.Net.IPEndPoint([Net.IPAddress]::Any, 0)
[Text.Encoding]::ASCII.GetString($u.Receive([ref]$ep))
```
