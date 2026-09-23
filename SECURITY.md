# Security

SF4Enhanced is a hobby project: a game mod plus a small lobby server run by
volunteers. Please read this before reporting.

## Reporting a vulnerability

Open a private report through GitHub's **Security → Report a vulnerability**
on this repository, or email the maintainer address shown on the GitHub
profile. Please include the version (shown in the lobby), what you did, and
what happened. Expect an acknowledgement within a week; fixes ship as a normal
release and are noted in the release notes.

Do not test against the public lobby server in ways that affect other
players. Run your own server (see `SERVER.md`) for anything beyond a ping.

## What is in scope

* The launcher (`SF4Enhanced.exe`) and the injected `Sidecar.dll`.
* The lobby server (`LobbyServer.exe` / `LobbyServer`) and the session
  protocol between it and the game.
* The deploy scripts under `deploy/`.

## Known limitations

These are known and not considered new reports:

* The lobby server has no accounts and no abuse protection. Lobby codes are a
  convenience, not an access control, and the server relays raw GGPO traffic
  between whoever it believes are the two players. Anyone able to send UDP to
  the server can create lobbies or exhaust them.
* The connection between the game and the server is encrypted but not
  authenticated; there is no certificate pinning. Whoever controls the
  address the launcher uses (baked in, `server.txt`, or `--server`) controls
  the match.
* In peer-to-peer mode (the default) the two players learn each other's IP
  address. The lobby's CONNECTION option switches to the server relay.
* The mod runs inside a third-party game process and is only as robust as
  the game's own memory safety.

## What the mod does not do

No telemetry, no automatic uploads, no elevation, no writes to the game
directory other than `steam_appid.txt`, no Steam credentials or Steam ID are
read (only the display name).
