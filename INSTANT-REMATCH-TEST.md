# Two-PC instant-rematch test

This is an experimental 1.2.0 build. The automated GGPO and protocol tests pass;
the real USF4 state restoration still needs live validation. Keep the released
1.1.0 package in its own folder so you can switch back.

## Setup

For a private internet test hosted on one player's PC, follow the
[Tailscale setup in SERVER.md](SERVER.md#private-test-with-tailscale).
`Host-Rematch-Test.cmd` starts the server and game on the host without a
configuration file. The other PC uses the host's Tailscale address in its
launcher shortcut. Then continue with **First test** below.

1. Extract the same complete preview ZIP on both PCs. Keep all included DLLs.
2. Run this package's `LobbyServer.exe` on one reachable machine. For two PCs
   on the same network, one of those PCs can host it. See `SERVER.md` for the
   UDP ports; an Internet test needs a reachable server and the listed ports.
3. Put the server machine's address in `server.txt` on both PCs. On a LAN, use
   its LAN IPv4 address on both, for example `192.168.1.50:23400` (replace the
   example). An older public server does not support this experiment.
4. With Steam running, launch **`SF4Enhanced.exe` on both PCs** from a preview
   built with `SF4E_DEFAULT_INSTANT_REMATCH=ON`. The experiment is enabled for
   that launch when the server supports it. `Try-Instant-Rematch.cmd` also
   enables it for older preview packages. To use ordinary lobby rematches,
   launch `SF4Enhanced.exe --no-instant-rematch`.
5. Open Multiplayer Battle, create a private room, and join its code on the
   other PC. Pick characters, Ultras and a stage, then ready both players.

A package with built-in public servers needs no `server.txt` for ordinary
play. That does not upgrade those servers: the instant-rematch test and shared
room scores still require the matching fork server. On an older server the
client uses ordinary rematches automatically, even with the experiment enabled.

## First test

- Finish a match. A small result menu should appear over the still-loaded
  battle. Rematch becomes available after the final inputs are confirmed.
- Choose Rematch on just one PC. Neither game should restart yet.
- Choose Rematch on the other PC. Both should return to the starting match
  state without visiting character select or reloading the stage. A brief
  GGPO synchronization wait is expected.
- Repeat at least five times, alternating winners. Check starting positions,
  full health, empty meters, fresh rounds/timer, correct controls, and room
  scores increasing exactly once. Player sides stay fixed on the fast path.
- After another match, select Change character. Both should return through
  the ordinary lobby flow, where selections can change normally.

Next, try time-over, a draw, different fighters, and direct versus relay play.
If you can add a third PC, have its spectator join before the first match and
use the same experimental launcher. Existing spectators follow the restart;
new spectators wait for the next normally loaded match. A missing spectator
or failed handshake must fall back rather than deadlock.

All buttons are temporarily disabled during the prepare/start handshake.
Do not assume a successful first rematch proves long-session stability. If
anything freezes, diverges, retains a meter/round, or counts a win twice, note
the characters, stage, winner, and rematch number, and keep both game logs.
The game logs are in `%APPDATA%\sf4e\logs\` on each PC. Search them for
`Instant rematch:` to find the matching epochs and final
input frames. The source/design record is in `docs/INSTANT-REMATCH.md`.
