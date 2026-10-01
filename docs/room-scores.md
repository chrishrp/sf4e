# Shared room scores

The lobby server owns each connected member's `roomScore.wins` and
`roomScore.losses`. Both players and spectators receive these fields in normal
lobby updates. Totals follow the member through winner-stays seat changes,
character changes and rematches. They cover completed matches in this room visit;
they are not account ratings or permanent player records.

A disconnected member loses their room record. Rejoining gets a fresh
`roomMemberId`, even when the same display name or network connection number is
reused. There is no authenticated account or resume token in the upstream
protocol, so restoring a score by display name would let another visitor claim
it. Closing/resetting the room clears all member totals.

## Results and compatibility

At all-ready, the server assigns a nonzero `MatchData.matchId` and freezes both
members' identities. The original P1 reports that ID and the losing side, exactly
once. Only that P1's result for the current seating can update scores or rotate
the players. Duplicated results, spectators, P2, stale results from earlier
matches and results after a player leaves are ignored.

`Lobby_ReportResults(-1)` ends a draw or aborted match without a win, loss or seat
rotation. The caller must use this for any game that did not produce a decisive
winner. A server-received desync report also marks the active match inconclusive.
Result validation relies on P1's game; the lobby server does not simulate matches
or provide anti-cheat verification.

Both the fork's client and server must be deployed for shared scores. The client
reads older server updates with `LobbyData.roomScoresAvailable == false` and can
display that room scores are unavailable. An older client without match IDs
cannot finalize matches on the upgraded server; normal sidecar hash matching
should keep builds together.

## Rendering the room

- Render each member's `roomScore.wins` / `roomScore.losses` only when
  `LobbyData.roomScoresAvailable` is true.
- Bind scores to the member record rather than to P1/P2 indices.
- A live character/ultra pick belongs to a seat when the nonzero
  `MemberData.roomMemberId` equals `MatchData.charaMemberId[side]`. This includes
  Ryu, whose character ID is zero. Character ownership is cleared when a seat is
  vacated and swapped with the players after a P2 win.
- On an older server, a ready flag remains the available signal for a confirmed
  opponent pick.

## Tests

The result policy has no game, graphics, JSON or networking dependencies. From
the repository root, use a C++11 compiler:

```sh
c++ -std=c++11 -Wall -Wextra -pedantic tests/room_score_test.cxx -o room_score_test
./room_score_test
```

With an MSVC developer prompt:

```bat
cl /nologo /EHsc /W4 tests\room_score_test.cxx /Fe:room_score_test.exe
room_score_test.exe
```

The test covers authority, duplicate and stale reports, winner-stays rotation,
rematches, draws, desyncs, disconnects, a replacement opponent and room reuse.
`tests/room_protocol_test.cxx`, linked against `Session`, additionally verifies
JSON broadcasts and old-server defaults for scores, selection owners and match
IDs. Run the registered CMake tests with `ctest --test-dir <build> -C Release`.
Integration testing still requires two game clients and the upgraded server;
include a spectator and verify that all three displays agree after each match.
