# Arcade lobby

Source baseline: fabeloper/sf4e commit `7d47241` (1.0.2). This fork changes the native
Dear ImGui / Direct3D 9 overlay in `Sidecar.dll`; it is not an external web launcher.

## Repository architecture

`Launcher` locates the Steam installation and loads `Sidecar.dll` into the game.
The `src/Dimps` library describes the reverse-engineered engine interfaces and
offsets. `src/sf4e` supplies the game hooks, GGPO integration, and native overlay.
`SessionClient` sends selections/readiness to `SessionServer`; the standalone
`LobbyServer` also supplies room-code matchmaking, relay traffic and spectators.

The fork adds a game-independent `LobbyView` renderer and a native portrait loader,
then adapts the existing lobby state and controls to that renderer. Selection
randomization runs only in the menu. The shared session protocol carries live
selection ownership, room records, and match IDs; the server updates records
once per accepted result. Combat routines and the `src/Dimps` offset mirror
remain untouched. Adding gameplay abilities is a separate asset/engine project,
described in `MODDING-FEASIBILITY.md`.

## Using the room

The room shows official roster icons for all 44 fighters, large portraits of both
players, their ready states, and each player's room wins and losses. Artwork is
read from the installed game. The central score follows the current seating;
names and records remain attached to the same
members when the winner takes P1.

Portraits load automatically from the game files. Players do not need to extract
images, install Python, or copy artwork into the mod folder. The loader leaves
game files unchanged and uses a silhouette when a portrait is unavailable.
See [official portrait loading](PORTRAITS.md) for details.

Use the d-pad/arrows to move, A/Enter to select, and LB/RB (Page Up/Page Down) to
change an option. Start/F1 readies the player. Mouse clicks select fighters,
options, and actions. While ready, B/Escape cancels readiness before editing.
The room keeps labels and status text minimal; controls are documented here.

The action row has three independent random buttons: fighter, Ultra, and stage.
Each click resolves immediately to a real selection; randomness is not evaluated
inside rollback simulation. A random fighter resets costume/color to their
defaults and retains a compatible edition, or falls back to Ultra if the new
fighter did not exist in the old edition. Random stages exclude the car and
barrel bonus stages. Stage cycling excludes them too.

The stage panel names the shared match stage. P1 chooses it. P2 may store a
different proposal for when they become P1; the panel labels that proposal
separately. Readying and immediate rematches preserve the inherited stage.

Ultra I and II show their names and directional inputs, with orange/blue accents;
Ultra Double uses purple. Directions assume the fighter faces right. See the
[command catalog](LOBBY_CATALOG.md) for notation, character-specific variants,
source evidence and edition differences.
Original SFIV exposes only Ultra I; Super and AE editions expose I/II. Ultra and
Omega expose Double as well. Omega commands are labeled as unverified USFIV
references, rather than presented as verified Omega inputs.

Room scores need the fork's `LobbyServer` as well as the fork's `Sidecar.dll` on
both machines. Older servers remain readable but display “room score unavailable”.
Scores cover the member's current room visit. Leaving/rejoining starts a new
record; these are not persistent account statistics. Draws, aborts and desyncs do
not award wins or losses. See `room-scores.md` for the result protocol and limits.

## Build

Use the upstream Windows x86 / MSVC / vcpkg build instructions. Build `Sidecar`,
`Launcher`, and `LobbyServer`. The portrait loader reads the installed game's
portrait archives directly, using native code and D3D9. Its zlib dependency is
supplied by the CMake/vcpkg build. No game files or extracted portrait assets are
committed or included in packages. Windows WIC is used by the preview tool to
write PNG screenshots.
The package also includes `LobbyServer`, dependency DLLs, the x86 Visual C++
release runtime, and a `server.txt` template with no active endpoint. Configure
the server address before launching; no public server is supplied by this fork.

Do not replace a live installation while it is running. Use a separate package
folder and set its `server.txt` or launcher's `--server` argument to your fork's
server (see `SERVER.md`). A new binary hash requires matching clients.

## Verification

Validated on 1 October 2026 with MSVC 2019, Windows x86, RelWithDebInfo: client,
server, preview and test targets built successfully; CTest passed 4/4 tests.
The installed-asset check decoded all 44 official portraits and all 44 icons.
Native D3D9 previews loaded all 88 images at 1600x1000 and 1280x720; missing-game
resources displayed silhouettes and left the random-button hit regions usable.

`ctest --test-dir <build> -C Release --output-on-failure` runs the catalog, room
score policy, protocol roundtrip and portrait-parser checks. Catalog regeneration can be
checked against an owned game installation using `tools/build_lobby_catalog.py`.

`LobbyPreview` renders the **same** lobby renderer as the game, through Direct3D9
into an offscreen target. The window remains hidden; it exports a PNG and can
assert real hit regions without attaching to the game:

```bat
set "SF4E_PREVIEW_GAME_DIR=C:\Games\SteamLibrary\steamapps\common\Super Street Fighter IV - Arcade Edition"
LobbyPreview lobby.png 1600 1000 --game-dir "%SF4E_PREVIEW_GAME_DIR%" --require-portraits
LobbyPreview lobby-720.png 1280 720 --game-dir "%SF4E_PREVIEW_GAME_DIR%" --require-portraits
LobbyPreview random.png --game-dir "%SF4E_PREVIEW_GAME_DIR%" --click 690 890 --expect-hit 2 2
LobbyPreview missing.png --no-portraits
LobbyPreview spectator.png --game-dir "%SF4E_PREVIEW_GAME_DIR%" --spectator
LobbyPreview original-sf4.png --game-dir "%SF4E_PREVIEW_GAME_DIR%" --edition 13
LobbyPreview omega-reference.png --game-dir "%SF4E_PREVIEW_GAME_DIR%" --edition 16
```

Replace the example game directory with your Steam installation. `--game-dir`
is needed for the standalone preview because it runs outside the game process;
normal play discovers the game directory automatically. Preview rendering uses
the same centered, proportionally scaled canvas as the game overlay.

Preview names, readiness, scores and room codes are fixtures. They demonstrate
layout and do not represent a real online session.

Before calling this a tested multiplayer release, run two game clients and a
spectator against the upgraded server: live-select, randomize, ready/unready,
complete both P1/P2 wins, rematch, change characters, draw/abort, disconnect,
rejoin and alt-tab/change resolution. Unit tests and native rendered previews
do not substitute for that end-to-end check.
