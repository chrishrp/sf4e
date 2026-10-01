# Lobby portraits

`portraits.png` is an AI-generated fan-art atlas created with the built-in imagegen tool for this fork. It contains 44 character faces in an 8-column, 6-row grid; the final four cells are unused. The original pixels are preserved. No portrait art was extracted from the game.

Cells follow game character IDs except cells 12–15, which contain Abel, C. Viper, Rufus and El Fuerte. The renderer explicitly maps game IDs 12, 13, 14, 15 to cells 13, 14, 15, 12. The English names come from the verified command catalog, not the game's internal Japanese naming table.

Street Fighter characters remain Capcom's intellectual property. These generated portraits are unofficial fan art, not official artwork. The source-code MIT license does not grant rights to Capcom characters.

The full generation prompt is preserved in `generation-prompt.txt`. The loader reads this folder relative to the compiled Sidecar DLL, including when the game has a different working directory. A missing or failed texture falls back to silhouettes and names.
