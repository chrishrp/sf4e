"""Generate the lobby's factual move reference from the installed game.

Usage: python tools/build_lobby_catalog.py GAME_DIRECTORY [--check]
The curated table selects Ultra I/II resources, not supers or special moves.
"""
import json
from pathlib import Path
import re
import sys

from extract_lobby_catalog import extract, read_m4s


# Runtime character order differs from localization's ID_CHA order.
# code, English name, Ultra I name/input resource suffix, Ultra II name/input.
SELECTION = [
    ("RYU", "Ryu", "0018", "0019", "0020", "0021"),
    ("KEN", "Ken", "0023", "0024", "0025", "0026"),
    ("CNL", "Chun-Li", "0035", "0036", "0037", "0038"),
    ("HND", "E. Honda", "0018", "0019", "0020", "0021"),
    ("BLK", "Blanka", "0026", "0027", "0034", "0030"),
    ("ZGF", "Zangief", "0025", "0026", "0027", "0028"),
    ("GUL", "Guile", "0028", "0029", "0030", "0031"),
    ("DSM", "Dhalsim", "0021", "0022", "0023", "0024"),
    ("BSN", "Balrog", "0021", "0022", "0023", "0024"),
    ("BLR", "Vega", "0036", "0037", "0038", "0039"),
    ("SGT", "Sagat", "0022", "0023", "0024", "0025"),
    ("VEG", "M. Bison", "0020", "0021", "0022", "0023"),
    ("AGL", "C. Viper", "0021", "0022", "0023", "0024"),
    ("CHB", "Rufus", "0028", "0029", "0031", "0032"),
    ("RIC", "El Fuerte", "0047", "0048", "0049", "0050"),
    ("JHA", "Abel", "0026", "0027", "0028", "0029"),
    ("BOS", "Seth", "0027", "0028", "0029", "0030"),
    ("GKI", "Akuma", "0034", "0035", "0036", "0037"),
    ("GKN", "Gouken", "0031", "0032", "0033", "0034"),
    ("HWK", "T. Hawk", "0021", "0022", "0023", "0024"),
    ("CMY", "Cammy", "0025", "0026", "0036", "0028"),
    ("FLN", "Fei Long", "0020", "0021", "0022", "0023"),
    ("DJY", "Dee Jay", "0016", "0017", "0018", "0019"),
    ("SKR", "Sakura", "0020", "0021", "0022", "0023"),
    ("ROS", "Rose", "0018", "0019", "0020", "0021"),
    ("GEN", "Gen", "0061", "0014", "0062", "0016"),
    ("DAN", "Dan", "0020", "0021", "0022", "0023"),
    ("GUY", "Guy", "0032", "0033", "0034", "0035"),
    ("CDY", "Cody", "0037", "0038", "0039", "0040"),
    ("IBK", "Ibuki", "0060", "0061", "0062", "0063"),
    ("MKT", "Makoto", "0024", "0025", "0026", "0027"),
    ("DDL", "Dudley", "0052", "0053", "0054", "0055"),
    ("ADN", "Adon", "0020", "0021", "0022", "0023"),
    ("HKN", "Hakan", "0032", "0033", "0034", "0035"),
    ("JRI", "Juri", "0022", "0023", "0024", "0025"),
    ("YUN", "Yun", "0034", "0035", "0036", "0037"),
    ("YAN", "Yang", "0029", "0030", "0031", "0032"),
    ("RYX", "Evil Ryu", "0026", "0027", "0028", "0029"),
    ("GKX", "Oni", "0036", "0037", "0038", "0039"),
    ("RLN", "Rolento", "NAME14", "0028", "NAME15", "0030"),
    ("ELN", "Elena", "NAME15", "0030", "NAME16", "0032"),
    ("PSN", "Poison", "NAME12", "0024", "NAME13", "0026"),
    ("HUG", "Hugo", "NAME14", "0028", "NAME15", "0030"),
    ("DCP", "Decapre", "NAME16", "0032", "NAME21", "0034"),
]

NOTES = {
    ("BLK", 0): "Hold PPP to delay the roll.",
    ("BLK", 1): "PPP: anti-air. KKK: anti-ground. Hold PPP to delay.",
    ("JHA", 1): "Hold KKK to delay; P cancels the charge.",
    ("GKN", 1): "Hold KKK to charge.",
    ("CMY", 1): "Counter: requires an eligible incoming attack.",
    ("FLN", 1): "Counter: requires an eligible incoming attack.",
    ("SKR", 1): "KKK instead: Shinku Tengyo Hadoken (anti-air).",
    ("GEN", 0): "Mantis: Zetsuei. Crane: same motion + KKK for Ryukoha.",
    ("GEN", 1): "Mantis: Shitenketsu. Crane: AIR D DF F D DF F + KKK for Teiga.",
    ("YUN", 0): "Unavailable during Genei Jin.",
    ("YUN", 1): "Unavailable during Genei Jin.",
    ("RYX", 0): "Hold PPP to charge.",
    ("GKX", 0): "AIR + PPP: Messatsu-Gozanku. Ground + KKK: Messatsu-Gotenha.",
    ("ELN", 1): "Press PPP again to stop healing.",
    ("DCP", 1): "Anti-air: CHARGE DB DF DB UF + KKK. Heavy trajectory: same + PPP.",
}


def normalize_input(value):
    directions = {"1": "DB", "2": "D", "3": "DF", "4": "B", "5": "N", "6": "F", "7": "UB", "8": "U", "9": "UF"}

    def image(match):
        token = match[1]
        if token.startswith("CMD_"):
            motion = token[4:]
            if motion == "0":
                return " 360 "
            charge = motion.endswith("C")
            if charge:
                motion = motion[:-1]
            return " " + ("CHARGE " if charge else "") + " ".join(directions[d] for d in motion) + " "
        if token in ("P", "K", "+"):
            return " " + token + " "
        raise ValueError("Unhandled command icon " + token)

    text = re.sub(r"<img src='img://([^']+)'[^>]*>", image, value)
    text = text.replace("(in air)", "AIR ").replace("(near opponent)", "NEAR ")
    text = re.sub(r"\b([LMH])\s+([PK])\b", r"\1\2", text)
    text = " ".join(text.split())
    text = text.replace("360 360", "720").replace("P P P", "PPP").replace("K K K", "KKK")
    if "<" in text or ">" in text:
        raise ValueError("Unparsed markup in " + text)
    return text


def build(root):
    evidence = extract(root)
    assert evidence["characterCodes"] == [entry[0] for entry in SELECTION], "Executable roster changed"
    fighters = []
    for code, name, n1, i1, n2, i2 in SELECTION:
        raw = evidence["commands"][code]["entries"]
        prefix = "ID_TU3_CMD_" if code in ("YUN", "YAN", "RYX", "GKX") else "ID_CMD_"
        key = lambda suffix: prefix + code + "_" + suffix
        ultra = []
        for number, (name_id, input_id) in enumerate(((n1, i1), (n2, i2))):
            ultra.append([raw[key(name_id)].strip(), normalize_input(raw[key(input_id)]), NOTES.get((code, number), "")])
        fighters.append((code, name, ultra))
    edition_overrides = []
    edition_ids = [13, 1, 2, 0, 4, 14, 16]
    for edition in (1, 2, 4):
        for index, row in enumerate(SELECTION):
            code, name, n1, i1, n2, i2 = row
            if not evidence["validEditions"][index][edition_ids.index(edition)]:
                continue
            suffix = "_ssfiv" if edition == 1 else ""
            source = None
            for layer in ("resource", "dlc/04_ae2", "patch_ae2", "patch_ae2_tu1", "patch_ae2_tu1b", "patch_ae2_tu2", "patch_ae2_tu3"):
                candidate = root / layer / "ui/command_list/localize/ENG" / ("command_list_" + code.lower() + suffix + ".m4s")
                if candidate.exists():
                    source = candidate
            if source is None:
                raise ValueError("Missing historical command source for " + code)
            historical = read_m4s(source)
            prefix = ("ID_TU3_CMD_" if code in ("YUN", "YAN", "RYX", "GKX") else "ID_CMD_") + code + "_"
            for u, input_id in enumerate((i1, i2)):
                key = prefix + input_id
                # Arcade Edition introduced separate command-text IDs for
                # these moves. The old text remains in the same M4S file.
                if edition in (2, 4) and u == 1 and code in ("HND", "BSN", "VEG"):
                    key = "ID_TU3_CMD_" + code + "_0000"
                command = normalize_input(historical[key])
                # NEAR is explanatory; preserve the latest proximity hint.
                if command == fighters[index][2][u][1].replace("NEAR ", ""):
                    continue
                if command != fighters[index][2][u][1]:
                    edition_overrides.append((edition, index, u, fighters[index][2][u][0], command, fighters[index][2][u][2]))
    stage_localization = read_m4s(root / "dlc/04_ae2/ui/common/simple_chara_select/localize/ENG/default.m4s")
    stages = []
    # The six Ultra stages use graphical title labels; these are their public
    # English names, in the executable's verified DET/ELV/HFP/MAD/BFU/JUR order.
    ultra_stages = ["The Pitstop 109", "Cosmic Elevator", "The Half Pipe", "Mad Gear Hideout", "Blast Furnace", "Jurassic Era Research Facility"]
    for index, code in enumerate(evidence["stageCodes"]):
        name = stage_localization["ID_STG_%04d" % index] if index < 22 else evidence["stageNames"][index]
        if index >= 24:
            name = ultra_stages[index - 24]
        stages.append((code, name, index not in (22, 23)))
    quote = lambda text: json.dumps(text, ensure_ascii=True)
    lines = ["#pragma once", "", "// Generated by tools/build_lobby_catalog.py from the installed English USFIV", "// command lists. See docs/LOBBY_CATALOG.md for evidence and input notation.", "// No Windows, ImGui, or game-runtime dependency; valid standalone C++11.", "namespace sf4e { namespace LobbyCatalog {", "", "static const int CharacterCount = 44;", "static const int StageCount = 30;", "", "struct Ultra { const char* name; const char* input; const char* note; };", "struct Fighter { const char* code; const char* name; Ultra ultra[2]; };", "struct Stage { const char* code; const char* name; bool versus; };", "", "inline const Fighter* Find(int characterId) {", "    static const Fighter fighters[CharacterCount] = {"]
    for index, (code, name, ultras) in enumerate(fighters):
        fields = ["{" + ", ".join(quote(value) for value in u) + "}" for u in ultras]
        lines.append("        {" + quote(code) + ", " + quote(name) + ", {" + ", ".join(fields) + "}}, // " + str(index))
    lines += ["    };", "    return characterId >= 0 && characterId < CharacterCount ? &fighters[characterId] : nullptr;", "}", "", "// Edition values are the engine IDs: SFIV=13, Super=1, AE=2, 2012=4, Ultra=14.", "// Omega (16) is deliberately unverified; nullptr permits an explicit reference label.", "inline const Ultra* FindUltra(int characterId, int ultraIndex, int edition = 14) {", "    const Fighter* fighter = Find(characterId);", "    if (!fighter || ultraIndex < 0 || ultraIndex > 1) return nullptr;", "    if (edition != 13 && edition != 1 && edition != 2 && edition != 4 && edition != 14) return nullptr;", "    if (edition == 13 && ultraIndex != 0) return nullptr;", "    static const unsigned int validEditionMask[CharacterCount] = {"]
    masks = [sum((1 << edition_ids[i]) for i, valid in enumerate(row) if valid and edition_ids[i] != 0) for row in evidence["validEditions"]]
    for index in range(0, len(masks), 8):
        lines.append("        " + ", ".join(str(mask) + "u" for mask in masks[index:index+8]) + ",")
    lines += ["    };", "    if (!(validEditionMask[characterId] & (1u << edition))) return nullptr;", "    struct Override { int edition, character, ultra; Ultra move; };", "    static const Override overrides[] = {"]
    for edition, index, u, name, command, note in edition_overrides:
        lines.append("        {" + ", ".join(str(value) for value in (edition, index, u)) + ", {" + ", ".join(quote(value) for value in (name, command, note)) + "}},")
    lines += ["    };", "    for (const Override& value : overrides)", "        if (value.edition == edition && value.character == characterId && value.ultra == ultraIndex) return &value.move;", "    return &fighter->ultra[ultraIndex];", "}", "", "inline const Stage* FindStage(int stageId) {", "    static const Stage stages[StageCount] = {"]
    for index, (code, name, versus) in enumerate(stages):
        lines.append("        {" + quote(code) + ", " + quote(name) + ", " + ("true" if versus else "false") + "}, // " + str(index))
    lines += ["    };", "    return stageId >= 0 && stageId < StageCount ? &stages[stageId] : nullptr;", "}", "", "// Shared UI palette; these are fork design tokens, not sampled game pixels.", "struct RGB { unsigned char r, g, b; };", "inline RGB UltraColor(int selection) {", "    static const RGB colors[3] = { {255, 117, 51}, {67, 173, 255}, {187, 115, 255} };", "    return colors[selection >= 0 && selection < 3 ? selection : 0];", "}", "", "}} // namespace sf4e::LobbyCatalog", ""]
    return "\n".join(lines)


if __name__ == "__main__":
    target = Path(__file__).resolve().parents[1] / "src/sf4e/sf4e__LobbyCatalog.hxx"
    generated = build(Path(sys.argv[1]))
    if "--check" in sys.argv:
        assert target.read_text(encoding="utf-8") == generated, "Catalog differs from installed command-list evidence"
        print("44 fighters, 88 Ultra selections, and 30 stage IDs match catalog source evidence.")
    else:
        target.write_text(generated, encoding="utf-8", newline="\n")
        print("Wrote", target)
