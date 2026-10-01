# Lobby character and command reference

`src/sf4e/sf4e__LobbyCatalog.hxx` is a standalone C++11 reference for the
English Ultra Street Fighter IV roster, Ultra selections, and stages.
It has no game, graphics, or Windows dependency.

`Find(characterId)` returns a `Fighter` with `code`, `name`, and two `Ultra`
records (`name`, `input`, `note`). `FindStage(stageId)` returns `code`, `name`,
and `versus`. Both reject invalid IDs with `nullptr`.
`FindUltra(characterId, ultraIndex, edition)` adds edition-aware commands
using engine edition IDs (13/SFIV, 1/Super, 2/AE, 4/2012, 14/Ultra).
It rejects characters unavailable in that edition, Ultra II in original
SFIV, and unverified Omega input references with `nullptr`.

## Verified sources

The roster IDs and stage IDs were read directly from the installed Steam
`SSFIV.exe`, using the same RVA tables already located by
`src/Dimps/Dimps.cxx`: character codes `0x66A8A8`, character names `0x66A958`,
stage codes `0x66B678`, stage names `0x66B600`, and per-character edition
availability `0x539BA8`.
The executable SHA-256 is
`5d724595a8ab3c6c6d6f4959187f756f5be35bb497e51e5233c4e73b18b0b9eb`.

All 88 primary Ultra names and input sequences are generated from the
game's English `command_list_*_swan.m4s` files. Historical overrides use
the corresponding `_ssfiv.m4s` (Super) and unsuffixed `.m4s` (AE/2012)
resources, including the separate `ID_TU3_CMD_*` keys for AE Honda, Balrog,
and Bison. The extractor checks the
English M4S signature, reads each relative-offset string-table entry, and
decodes the UTF-16LE text and embedded command icons. Later title-update
directories override earlier resources. `docs/lobby-catalog-sources.json`
records the exact files and hashes used. No binary game assets are included.

The executable uses Japanese/internal boss names; English localized names
are deliberately used for BSN/Balrog, BLR/Vega, VEG/M. Bison, and GKI/Akuma.
Runtime character order is also different from the `ID_CHA_*` text-resource
order: for example, T. Hawk is runtime slot 19, Cammy slot 20, and Dee Jay
slot 22. The catalog is indexed by runtime ID, never by localized string ID.

Stage names 0-21 use English strings from
`dlc/04_ae2/ui/common/simple_chara_select/localize/ENG/default.m4s`.
Slots 22 and 23 are bonus games and are marked `versus=false`.
Slots 24-29 have graphical English title labels; their public names are
listed in the [Ultra Street Fighter IV stage reference](https://streetfighter.fandom.com/wiki/Ultra_Street_Fighter_IV#Stages).
Their mapping to `DET`, `ELV`, `HFP`, `MAD`, `BFU`, and `JUR` follows the
executable table. In particular, text key `ID_STG_0024` means **Random** in
the old localization table, while runtime stage 24 is **The Pitstop 109**.
The public-name mapping for those six stages was not read from graphical
game assets.

## Input notation

Inputs are ASCII, space separated, and shown with the character facing
right: `D` down, `DF` down-forward, `F` forward, `DB` down-back, `B` back,
`U` up, `UF` up-forward, and `UB` up-back. Directions are relative to the
opponent; mirror forward/back when facing left. `CHARGE` applies to the
next direction. `720` is two full rotations. `AIR` requires jumping;
`NEAR` requires proximity to the opponent.

`P` and `K` are punch and kick; `LP`/`LK` are light punch/kick and `HP` is
heavy punch. `PPP` or `KKK` means all three buttons together. `+` combines
the final direction and button press. Other tokens are entered in order.
`OR` gives an alternative button set after the same motion.

Notes preserve variants that a single primary command cannot express:
Blanka's anti-air/ground buttons, Sakura's anti-air projectile, Gen's two
stances, Oni's aerial/vertical projectiles, and Decapre's three DCM
trajectories. These notes should be visible with the command, not dropped.
`Find` describes the **USFIV edition**. A UI supporting edition selection
should use `FindUltra` for commands: historical differences include Honda
and Balrog's original 720 inputs, Bison's AE charge command, Dhalsim,
T. Hawk and Rose's punch/kick changes, and Oni's half-circle direction.
Omega command routing has not been verified; show `Find` commands as
**USFIV reference** for that edition, or omit them, rather than claiming
they are Omega commands.

`UltraColor` supplies the fork's consistent I/orange, II/blue, W/purple
palette. These RGB values are design choices, not sampled original-game
pixel values. W means both existing Ultras; it is not a third move.

## Reproduction and validation

Run from the repository root with an installed copy of the game:

```powershell
python tools/build_lobby_catalog.py 'F:/SteamLibrary/steamapps/common/Super Street Fighter IV - Arcade Edition' --check
```

Omit `--check` to regenerate the header. `tools/extract_lobby_catalog.py`
prints source evidence as JSON without writing to or modifying the game.
The generation check covers every base command and runtime character code;
it does not simulate combat or verify frame windows.

`tests/lobby_catalog_test.cxx` checks safe lookups, English naming, the
runtime/localization order distinction, unusual inputs, variant notes,
and the 28-stage versus pool. It can be compiled directly with a C++11
compiler and needs no project dependencies:

```text
g++ -std=c++11 -Wall -Wextra -pedantic tests/lobby_catalog_test.cxx -o lobby_catalog_test
```
