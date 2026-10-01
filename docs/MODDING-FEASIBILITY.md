# Custom characters and abilities: feasibility

Assessment: 1 October 2026. Source baseline: `7d47241656ae0821da177af081e118622ae34ebb`.

**New abilities on an existing fighter are a realistic next project. A genuinely additional fighter slot is an engine research project.** The lobby fork provides presentation and networking; it is not the source code of Ultra Street Fighter IV itself.

## What is practical

| Goal | Difficulty | Main work |
| --- | --- | --- |
| Lobby portraits, move instructions, stage display | Low relative to gameplay changes | Overlay assets, metadata, input handling. No combat simulation change. |
| Change damage, recovery, commands, cancels, armor, hitboxes | Moderate | Edit an existing fighter's BAC/BCM data; verify every affected interaction. |
| Add a special move using existing animation/projectile behavior | Moderate to high | Duplicate and adapt scripts, map commands, connect animation and effects, tune collisions and meter cost. |
| Create a new fighter by replacing an existing slot | High | Moveset plus model, rigging, animations, effects, voice, portraits and matchup testing. Reusing a compatible rig reduces the first experiment's scope. |
| Add a fighter beyond the existing roster | Very high / research | Establish engine support for new IDs and all tables/loaders using them; extend lobby, protocol validation, assets and rollback coverage. |
| Add a novel mechanic with new runtime state or actors | Very high / research | Engine hooks, deterministic simulation, complete state saving/restoration, effects and audio behavior during rollback. |

These are engineering judgments, not schedule estimates. I did not establish a maintained, general-purpose tool that adds arbitrary extra USF4 roster slots. That is an unresolved investigation, not evidence that expansion is impossible.

Ono! already exposes move inputs and meter requirements, animation/physics/cancel scripts, hitboxes, armor and fireball behavior. Its [move documentation](https://ono.frametrapped.com/doku.php?id=moves), [script documentation](https://ono.frametrapped.com/doku.php?id=scripts), [attack table](https://ono.frametrapped.com/doku.php?id=attack_table), [invincibility/armor documentation](https://ono.frametrapped.com/doku.php?id=invincibility) and [VFX documentation](https://ono.frametrapped.com/doku.php?id=vfx) support the existing-slot approach. The [tool's setup page](https://ono.frametrapped.com/doku.php?id=start) describes loading modified BAC/BCM files. Its loader's coexistence with this rollback launcher still needs to be tested.

The editor has documented quirks: duplicate an existing script, save, then reopen before editing the copy. The script documentation warns that its empty-script creation button can crash the tool. Keep editable source and known-good exports under version control.

## Constraints confirmed in this source

- **The protocol accepts 44 character IDs.** `CHARA_COUNT = 0x2c` in `src/session/sf4e__SessionProtocol.hxx`; `CharaConditionsValid` rejects higher IDs and `SanitizeCharaConditions` resets them. Changing this constant alone cannot create a new engine character. [Protocol bounds and validation](https://github.com/fabeloper/sf4e/blob/7d47241656ae0821da177af081e118622ae34ebb/src/session/sf4e__SessionProtocol.cxx#L34).
- **Build equality does not establish moveset equality.** The join request carries `sidecarHash`, and the server compares it with its expected DLL hash. `GetHash` hashes the loaded DLL file. This is not a BAC/BCM content-manifest check. A gameplay-mod edition should negotiate a separate content hash before readying players or spectators. [DLL hashing](https://github.com/fabeloper/sf4e/blob/7d47241656ae0821da177af081e118622ae34ebb/src/sf4e/sf4e.cxx#L48), [join validation](https://github.com/fabeloper/sf4e/blob/7d47241656ae0821da177af081e118622ae34ebb/src/session/sf4e__SessionServer.cxx#L895).
- **New state must survive rollback.** This project already had to repair the engine's shadow-move save states: their actor animation, collision and action state was omitted. That is a concrete example of a move working locally but diverging after rollback. [Afterimage repair](https://github.com/fabeloper/sf4e/blob/7d47241656ae0821da177af081e118622ae34ebb/src/sf4e/sf4e__Game__Battle__Chara.cxx#L1).
- **Useful diagnostics already exist.** The source has an offline rollback sync test, a save/load idempotence check and automated match testing. It distinguishes raw memory differences from gameplay divergence; heap addresses make raw-byte equality a poor standalone verdict. [Debug panel](https://github.com/fabeloper/sf4e/blob/7d47241656ae0821da177af081e118622ae34ebb/src/sf4e/sf4e__Overlay.cxx#L1146), [test configuration](https://github.com/fabeloper/sf4e/blob/7d47241656ae0821da177af081e118622ae34ebb/src/sf4e/sf4e__Platform.cxx#L338).

The architectural requirement follows GGPO's own API: the game supplies complete save/load callbacks and advances the simulation again during rollback. A new timer, projectile owner, resource meter or RNG state must participate in that mechanism. [GGPO callback contract](https://github.com/pond3r/ggpo/blob/master/src/include/ggponet.h#L178).

## Recommended first experiment

1. Create a separate experimental ruleset that uses Ryu's existing slot and assets. First confirm one damage-only change loads through the chosen BAC/BCM loader alongside SF4Enhanced.
2. Add one new fireball variant by adapting existing scripts and effects. Give it a distinct input and clear meter cost. Keep its runtime behavior within existing engine mechanisms for this first attempt.
3. Verify it offline: input recognition on both sides, hit/block/whiff, projectile clashes, interruption, KO, round transition and rematch.
4. Run the existing offline sync test at one-frame and longer rollback distances, including rollback across projectile creation and destruction. Run the save/load check while the projectile is active. Inspect gameplay divergence plus diagnostic state differences; do not dismiss an unexplained mismatch merely because the move looks correct.
5. Test two machines and a spectator with identical content. Include latency-induced rollback, repeated rematches and character changes. Add a ruleset ID and content hash to room negotiation before sharing the gameplay mod for general use.
6. If that passes, build a replacement-slot prototype character. Defer an extra roster slot until the asset pipeline and rollback behavior are proven.

Suggested acceptance gate: the new move behaves as designed in the interaction cases above, restores correctly, produces no unexplained gameplay divergence, and mismatched content is refused before the match. No modified game assets or new gameplay abilities are implemented by this lobby UI fork.
