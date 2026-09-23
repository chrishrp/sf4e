# Security

SF4Enhanced is a community project: a mod that runs inside a commercial game,
and a small lobby server that anyone can host. This page says how to report a
problem and what the design does and does not protect against, so you can
judge what a report is worth before sending it.

## Reporting

Use **Security → Report a vulnerability** on the GitHub repository. Include
the version (shown at the bottom of the lobby screen), what you did, and what
happened; a log from `%APPDATA%\sf4e\logs\` helps. Reports are acknowledged
within a week and fixed in a normal release, credited if you want.

Please do not probe the public servers in ways that affect other players.
`SERVER.md` explains how to run your own in a few minutes; test against that.

## Scope

* `SF4Enhanced.exe` (launcher) and `Sidecar.dll` (the code injected into the
  game).
* The lobby server and the session protocol between it and the game.
* The scripts under `deploy/`.

Bugs in the game itself, in Steam, or in third-party libraries belong with
their authors; a report here is still welcome if the mod makes them reachable.

## How the pieces trust each other

**Game ↔ lobby server.** The session link uses GameNetworkingSockets, which
encrypts the connection but does not authenticate the server: there is no
certificate. Whoever controls the address the launcher uses (the built-in
list, `server.txt`, or `--server`) controls the lobby and can decide which
address the game sends match traffic to. Treat a release from anywhere but
the project's own page as untrusted.

**Lobby codes.** A code is issued together with a random secret, and the
session only admits a client that presents the secret. Guessing a lobby's
port is not enough to enter it. Codes and secrets die with the lobby.

**Match traffic.** With the server relay, each player's traffic is forwarded
to the address the other player joined from. Two players behind the same
public address are told apart by arrival order, so someone sharing a public
IP with a player (the same household, a campus network, a carrier-grade NAT)
could in principle take that player's seat on the relay. With a direct
connection, the two games exchange packets themselves and each learns the
other's address; the lobby's CONNECTION option turns that off.

**Values from the other player.** Character, costume, colour, ultra and stage
choices are range-checked on the server and again in the game before they
index anything. Handicap is forced to neutral.

**Abuse limits on the server.** The matchmaker rate-limits per address, one
address may hold two lobbies, lobbies expire, connections per lobby and per
address are capped, and desync reports are truncated and counted. These are
limits, not an anti-cheat: a determined attacker can still keep a small
server busy, and there are no accounts to ban.

## What the mod does not do

No telemetry, no automatic uploads, no elevation, no writes into the game
directory except `steam_appid.txt`, no Steam credentials or Steam ID read
(only the display name). Crash dumps and logs stay on your disk until you
choose to send them.
