# Official lobby portraits

The lobby uses the official character-select artwork from your installed Steam
copy of Ultra Street Fighter IV. It reads the original roster icons and large
selection portraits for all 44 fighters, displaying the icons in the grid and
the larger artwork in the player cards.

## Playing

No portrait setup is required. Start the game through `SF4Enhanced.exe` as usual.
The loader finds the game directory from the running game executable, reads the
portrait files, and creates the display textures in memory. It does not alter
game files or create an extracted portrait folder.

You do not need Python, an extraction script, or separately downloaded artwork.
If one image is unavailable, the lobby can use that fighter's other original
image. If neither loads, it shows a silhouette with the fighter's name. The
lobby controls keep working.

Game files and extracted portrait assets are not committed to this repository
or shipped in its packages. The artwork remains part of the player's local game
installation.

## Standalone preview

`LobbyPreview` runs outside the game, so give it the game installation directory
explicitly. For example:

```bat
LobbyPreview lobby.png 1600 1000 --game-dir "C:\Games\SteamLibrary\steamapps\common\Super Street Fighter IV - Arcade Edition" --require-portraits
```

Replace that path with the directory shown by Steam's **Manage > Browse local
files** command for Ultra Street Fighter IV. The preview reads the same local
portrait resources as the overlay. `--require-portraits` requires all 44 roster
icons and all 44 large portraits to load successfully; `--no-portraits`
deliberately exercises the silhouette fallback.

The output PNG is a screenshot of the preview's own render target. The tool does
not capture the desktop or launch the game. Its player names, scores and room
code are demonstration data.

## Development

The loader and archive decoder are native C++. CMake/vcpkg supplies zlib for
compressed archives, and D3D9 creates the display textures. Windows WIC writes
preview screenshots. Python is not part of the portrait-loading path.
The DDS reader follows Microsoft's [DDS layout documentation](https://learn.microsoft.com/en-us/windows/win32/direct3ddds/dx-graphics-dds-pguide)
and validates the EMZ checksum, dimensions, archive bounds and texture sizes before
uploading the original DXT blocks.

The roster's 64-by-64 icons come from
`dlc/04_ae2/ui/chara_select/chara_select_dlc4.emz`, with entries named `CODE.dds`.
Abel's engine code is `JHA`, while his icon entry uses `JHN.dds`.

Large portraits use `sel_CODE.tex.emz` archives. Their base resource roots follow
the game's additions:

| Character IDs | Base resource root |
| --- | --- |
| 0–34 | `resource` |
| 35–38 | `dlc/03_character_free` |
| 39–43 | `dlc/04_ae2` |

The loader checks patch roots before the DLC/base resources, in this order:
`patch_ae2_tu3`, `patch_ae2_tu2`, `patch_ae2_tu1b`, `patch_ae2_tu1`, then
`patch_ae2`. This lets the game's installed updates take priority.

Use the portrait-parser tests for malformed data and bounds checks, then render
against an owned installation to verify all 44 character IDs and crops. Test a
missing assets as well: the remaining lobby controls should still work and use
the other original image or silhouette fallback. Do not add local game
archives or extracted images to commits or release packages.
