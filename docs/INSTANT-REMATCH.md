# Instant rematch design and validation

The proposed fast path keeps the loaded battle and restores a local starting
snapshot, but creates a **fresh GGPO session for every match**. It is restricted
to the same players, character conditions, rules, and stage. A character change,
invalid snapshot, disconnect, or failed handshake returns through the normal
lobby/loading path.

This is a design and validation record. Source inspection establishes the GGPO
requirements below; it does not establish that restoring a USF4 battle after a
complete match is safe on every character or that live multiplayer has passed.

The experiment is off in default source builds; preview packages can enable it
on double-click with `SF4E_DEFAULT_INSTANT_REMATCH=ON`. It still requires the
server's instant-rematch capability. It keeps the previous direct or relay endpoints through
the short prepare/start gap. The result overlay does not try to bind a second
hole-punch socket while GGPO owns the port. Starting a fresh session still
performs GGPO's peer handshake.

Result detection uses remaining vitality as a fallback because the authoritative
game winner field is not mapped here. It compares full fixed-point health
percentages, rather than raw hit points, so unequal maximum health does not
reverse a time-over result. Equal percentages and double knockouts are treated
as draws. This needs in-game verification alongside round-history edge cases;
the ratio unit test is not proof of USF4's complete result flow.

## Try it on two PCs

Use the **same instant-rematch preview package on every participant**, including
spectators, and a lobby server built from the same fork version. Both PCs must
connect to that server. See [server setup](../SERVER.md) and
[installation](../INSTALL.md) for the normal connection setup.

1. Launch `SF4Enhanced.exe` from a preview built with
   `SF4E_DEFAULT_INSTANT_REMATCH=ON` on both PCs. Other builds can use
   `Try-Instant-Rematch.cmd` or `SF4Enhanced.exe --instant-rematch` to opt in.
   Use `--no-instant-rematch` to disable it for one launch. These explicit
   options override `SF4E_INSTANT_REMATCH`; otherwise an inherited value is
   preserved, including `0` to disable the preview default.
2. Join the same room, choose a character on each side and a stage, and start
   the match. The first match loads normally. Keep the same players, seats,
   characters, costumes, colors, ultras, edition, and stage for this test.
3. Alternate which player wins by knockout. After each result, both players
   choose **Rematch**. Complete at least three rematches. Each should restart
   with fresh health, meters, timer, and round count without reloading the
   battle assets. A brief network synchronization pause is expected.
4. Check that the room win/loss totals count each completed match once, then
   choose **Change character**, make a different selection, and start again.
   This should use the normal lobby/loading path and remain playable.
5. Keep the logs from **both PCs** in `%APPDATA%\sf4e\logs\`. Note which match
   and action failed, whether the screens agreed, and whether it fell back to
   the lobby. Relevant lines begin with `Instant rematch:`. If a restart fails,
   keep both logs before repeating with `--no-instant-rematch`.

This first test checks the loaded-battle path across two real machines. Longer
matches, different characters, time-over/draw results, and spectators are the
next checks; passing the short test alone does not cover them.

## Why use a new session?

A persistent GGPO session would require the rematch itself to be a deterministic,
replayable simulation transition. Its transport frame counter and input queues
would continue increasing, while reset metadata would have to survive save/load
and rollback. Loading a starting snapshot out of band while retaining the old
GGPO history is insufficient: late input could restore an old match snapshot.
The [GGPO developer guide](https://github.com/pond3r/ggpo/blob/master/doc/DeveloperGuide.md)
requires deterministic simulation and complete restorable state.

This engine already contains diagnostics for round transitions that occur outside
the rollback timeline and for memento state that does not restore identically.
A new session prevents rollback across the rematch boundary. It retains GGPO's
normal startup handshake; “instant” means skipping battle asset loading, not
eliminating all network synchronization. See the engine's
[battle update and snapshot implementation](../src/sf4e/sf4e__Game__Battle__System.cxx).

## Frame accounting and the final-input barrier

Use distinct names for these quantities:

| Quantity | Meaning |
| --- | --- |
| `G` | Current GGPO state frame, before consuming input `G` |
| `C` | Highest input frame confirmed by the local GGPO backend |
| `F` | Fixed GGPO state frame held at the result boundary |
| `gameFrame` | USF4's own simulation counter, with an explicitly tracked relationship to `G` |

GGPO saves state zero before the first local input. After input `G` advances the
simulation, `ggpo_advance_frame` increments the counter and saves state `G + 1`.
Consequently, a held state `F` needs confirmation through **input `F - 1`**.
The drain condition is `C >= F - 1`, not `C >= F` and not an unconfirmed depth
equal to zero. The implementation is visible in
[GGPO `sync.cpp`](https://raw.githubusercontent.com/pond3r/ggpo/master/src/lib/ggpo/sync.cpp),
especially `AddLocalInput`, `IncrementFrame`, `SaveCurrentFrame`, and `LoadFrame`.

Do not assume `gameFrame == G`. The existing engine can simulate startup/idle
frames outside GGPO. Prefer the frame passed to `save_game_state`, retained in
the saved metadata and restored on load, or a checked epoch offset. If the
relationship is unknown or changes, refuse the fast path. Frame delay can put
confirmed inputs ahead of the current simulation, so use an inequality.

On the first result transition, stop ordinary forward simulation and keep the
old session alive. Continue the existing `Steam_PostUpdate` network pump and
`ggpo_idle`. `Peer2PeerBackend::DoPoll` receives packets, corrects predictions,
then refreshes confirmation and sends confirmed inputs to spectators. A hold
that also stops pumping cannot drain. See
[GGPO `p2p.cpp`](https://raw.githubusercontent.com/pond3r/ggpo/master/src/lib/ggpo/backends/p2p.cpp).

Rollback callbacks must remain able to replay the old match while it drains:
synchronize inputs, advance exactly one simulation frame, and call
`ggpo_advance_frame`. Do not let the new forward-simulation hold suppress those
callbacks. These callback contracts are documented in
[`ggponet.h`](https://github.com/pond3r/ggpo/blob/master/src/include/ggponet.h).

A predicted knockout is provisional. Recheck the result after pumping because
rollback can change the winner, move the result boundary, or remove the result
entirely. Release the hold if play must continue. Votes must describe the first
terminal simulation boundary, not an arbitrary later result-screen frame. If
both confirmed votes disagree, abandon the fast path rather than guessing.

## Server-coordinated restart

The selected protocol uses the existing room connection to separate the old and
new GGPO epochs:

1. Both players send confirmed result/rematch votes tagged with the current
   match ID, result frame, and result side. The server validates the same seats,
   matching result, and rematch eligibility. Result processing stays idempotent.
2. The server sends a prepare message to the two players and the spectators
   currently watching that match. This identifies one restart transaction.
3. Each participant stops forwarding old inputs, closes its old GGPO session,
   validates and restores its own local baseline, then acknowledges preparation.
   It must not create the new GGPO session yet.
4. Once all required acknowledgements arrive, the server starts a new match ID
   with the same seats and conditions. Each prepared participant constructs a
   new GGPO session and waits for its running event before advancing.
5. A leave, rejection, mismatched frame/result, unavailable baseline, or timeout
   cancels preparation and follows the normal lobby path. Late or duplicate
   messages cannot revive a cancelled transaction or repeat a score update.

The ordering matters even when reusing UDP ports. GGPO has packet magic and
handshake challenges, but this is not an application-level match ID. Closing
all old sessions before starting new ones avoids old peers participating in a
new handshake. The inspected transport implementation is
[`udp_proto.cpp`](https://github.com/pond3r/ggpo/blob/master/src/lib/ggpo/network/udp_proto.cpp).

## Baseline ownership and engine lifecycle

Capture the baseline at a common, fully initialized simulation phase before
players can influence the fight. A useful candidate is immediately before the
first eligible GGPO simulation frame, after battle objects, tasks, and seeded
state exist. Capturing during initial `BF__IDLE` is unsafe: the engine explicitly
guards GGPO saves there because the battle may not exist yet. Record the phase
and counter metadata so differing capture points are detectable.

Use a dedicated owned `SaveState`, separate from the rotating GGPO save slots.
GGPO frees its slot buffers on session destruction, so retaining a pointer to
one of those slots is not a persistent baseline. A separate snapshot must remain
valid across both that destruction and subsequent frame saves. Spectators need
an explicit capture path because their backend does not call save/load callbacks.

USF4 snapshots contain local object addresses and memento keys. They must never
be sent to another process. Validate the battle owner and object/key generation
before restoring or freeing a retained snapshot. Membership in `trackedKeys`
alone cannot detect an address that was freed and reused. Key release and
reinitialization hooks must invalidate the baseline **before** destruction or
replacement. Also invalidate it on battle teardown or conditions changing. Release its
owned data while the corresponding engine objects are still alive; after their
lifetime ends, do not call `Load` or `Free` through stale addresses. The relevant
paths are `SaveState::Save`, `Load`, `Free`, `Reclaim`, and the
[memento key hooks](../src/sf4e/sf4e__Game.cxx).

Restore all gameplay state together: round/match counters, health and meters,
flow callbacks, timers, input history, RNG, and task/memento state. Reusing the
original baseline also reuses its RNG state; changing the seed requires an
explicit identical reseeding operation on every participant.

The existing `_OnVsBattleTasksRegistered` entry point constructs sessions and
associates the selected controller. Its `StartGGPO`/`StartSpectating` callees also
arm normal battle-start flow and request-seed overrides. Those loading hooks are
not replayed by a snapshot restore, so the fast path must clear or deliberately
consume the priming flags. Also reset per-match snapshot maps, result latches,
frame offsets, pacing, connection statistics, and spectator starvation tracking.
See [UserApp](../src/sf4e/sf4e__UserApp.cxx) and
[GameEvents](../src/sf4e/sf4e__GameEvents.cxx).

## Spectators

GGPO spectators consume confirmed inputs sequentially; they do not predict.
Receiving a frame and simulating it are different events. The fork's
`ggpo_get_last_confirmed_frame` spectator implementation returns the highest
received input, which can be ahead of the displayed fight. The prepare policy
must either wait for the spectator to consume the agreed terminal boundary or
explicitly skip its remaining playback. It must not mistake receipt for
simulation. The backend is documented by its
[`spectator.cpp`](https://raw.githubusercontent.com/pond3r/ggpo/master/src/lib/ggpo/backends/spectator.cpp)
implementation.

Only existing viewers with a valid local baseline qualify for this fast path.
A viewer who joined mid-match has neither the running input history nor the
loaded baseline. Keep that viewer waiting or use normal loading to admit them;
do not silently mark them prepared. A fresh session permits a new spectator list,
but preparing the game state remains the application's responsibility.

## Validation before release

| Check | Required evidence |
| --- | --- |
| Protocol phases | Tests cover both vote orders, duplicate/stale messages, disagreement, cancellation, timeouts, and membership changes in every phase. Each match scores once. |
| Frame barrier | A callback harness demonstrates state `F` waits only for inputs through `F - 1`, with zero and positive delay, delayed final packets, and game/GGPO offsets. A false predicted KO resumes correctly. |
| Local restore | Save/load/save gameplay comparison at the chosen baseline, then first-frame simulation comparison. Verify RNG, round counters, input history, task lists, HUD, and sound; do not treat pointer/checksum noise as gameplay equivalence. |
| Lifetime guard | Destroy/reinitialize a saved key and reuse its address; the baseline must be rejected without dereferencing stale owners. Check separate snapshot ownership through repeated GGPO closes. |
| Multiplayer | Two game processes complete repeated same-selection rematches over relay and direct paths, with unequal frame delay, latency, jitter, packet loss, and different rendering rates. Compare epoch/frame/flow/result logs and gameplay snapshots. |
| Spectators | Existing viewer follows successive rematches; delayed viewer drains or follows the defined skip policy; new viewer waits correctly; leaving or stalled viewer cannot deadlock the room. |
| Fallback | Character/stage change, disconnect, missing baseline, failed restart, and ordinary return to lobby still work without stale seed/start flags. |
| Endurance | Repeated rounds and matches, including time-over, draw, supers/ultras, and varied characters. Record memory/key counts and fail on gameplay divergence or accumulating owned snapshots. |

Passing a native build and protocol tests does not validate the in-game restore.
Keep live two-player and spectator results separate from those checks.

### Production room-server evidence

`InstantRematchServerTest` drives the real `SessionServer` with raw JSON over
GameNetworkingSockets UDP loopback socket pairs. It checks player votes,
retained versus late spectators, prepare acknowledgements, fresh match IDs,
unchanged seats/settings, exactly-once scores, rejected stale result/snapshot/
loaded packets, and cancellation crossing a committed start. It also checks
that spectator departure preserves the current match and its scoring, both
before and after a fast rematch. This exercises the production message handlers;
it does not launch USF4 or the public matchmaker service.

The complete native CTest suite has eight targets, including this fixture, the
GGPO fixture below, protocol/coordinator/outcome tests, and the existing lobby,
score and portrait tests.

### GGPO integration evidence

[`tests/ggpo_rematch_test.cxx`](../tests/ggpo_rematch_test.cxx) runs the installed
patched GGPO library with two player sessions and one spectator on loopback UDP.
It reuses the same three ports for seven epochs, with three epochs each at input
delays of one and three frames, followed by a predicted-knockout retraction case.
A deterministic toy game ends at state frame 90.
The test withholds endpoint pumping to force rollback and leave the final result
temporarily unconfirmed, then requires confirmation through input 89 before a
player can vote. Spectator readiness requires its own consumed state frame 90.

The retraction case pauses the second endpoint after sending a hit for input
frame 39. GGPO predicts the hit repeats on frame 40, producing a provisional
knockout and holding forward simulation at state frame 41. When the endpoint
resumes, its actual input for frame 40 is different. The test requires a real
GGPO load/replay to remove the knockout at the held frame before forward play
resumes, and checks that no result vote was sent for that provisional knockout.
Both players and the spectator must subsequently reach the same independently
replayed final state.

The successful native run checked seven matching independent replays and
2,526 rollback frames. Every session released its saved buffers on close; the
separate starting baseline survived. Inputs include an epoch marker so any old
epoch entering a new simulation fails the test. This checks a real GGPO stream
across fresh sessions, although it does not inject every possible delayed UDP
packet or exercise the room server's message phases.

The final toy-game boundary is fixed, and the predicted knockout uses a small
test simulation. This does not prove the USF4 path correctly retracts a predicted
knockout, reproduces a baseline after a long match, or restores every character's
hidden state. A save/load round trip
at match start is partial evidence only: object lifetimes and omitted state can
change later. Those cases remain requirements of the in-game validation above.

## Inspected dependency

This repository pins `adanducci/ggpo` at `c88b667` and applies the patch list in
[the GGPO port](../vcpkg-ports/ggpo/portfile.cmake). The local generated source
examined was `.tools/vcpkg/buildtrees/ggpo/src/c88b667-0d125de77e.clean`.
The public GGPO sources linked above explain the underlying contract; the local
patched source is authoritative for this build.

In particular,
[sf4e-assert-confirmed-frame.patch](../vcpkg-ports/ggpo/sf4e-assert-confirmed-frame.patch)
adds confirmed-frame/depth queries, and
[spectator-robustness.patch](../vcpkg-ports/ggpo/spectator-robustness.patch)
adds a spectator startup deadline, a 1024-frame buffer, and receive/backlog
tracking. `AddSpectator` still rejects additions once the P2P session is running.
None of these patches supplies a battle reset or application match-epoch API.
