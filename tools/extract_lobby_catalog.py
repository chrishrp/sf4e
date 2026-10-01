"""Read installed USFIV tables/command-list resources; does not modify the game.

Run with the USFIV install directory as the sole argument. Output is a compact
JSON evidence document, not game assets. Requires only Python's standard library.
"""
import hashlib
import json
from pathlib import Path
import struct
import sys


def read_m4s(path):
    data = path.read_bytes()
    if data[:8] != b"#M4S#ENG":
        raise ValueError("Expected English M4S resource: " + str(path))
    count, base = struct.unpack_from("<II", data, 16)
    result = {}
    for index in range(count):
        key_off, value_off = struct.unpack_from("<II", data, base + index * 8)
        key_off += base + index * 8
        value_off += base + index * 8
        key = data[key_off:data.index(b"\0", key_off)].decode("ascii")
        value_end = value_off
        while data[value_end:value_end + 2] != b"\0\0":
            value_end += 2
        result[key] = data[value_off:value_end].decode("utf-16le")
    return result


def read_exe_tables(path):
    data = path.read_bytes()
    pe = struct.unpack_from("<I", data, 0x3C)[0]
    section_count = struct.unpack_from("<H", data, pe + 6)[0]
    optional_size = struct.unpack_from("<H", data, pe + 20)[0]
    image_base = struct.unpack_from("<I", data, pe + 52)[0]
    sections = []
    for index in range(section_count):
        off = pe + 24 + optional_size + index * 40
        vsize, va, size, raw = struct.unpack_from("<IIII", data, off + 8)
        sections.append((va, max(vsize, size), raw))

    def file_offset(rva):
        for va, size, raw in sections:
            if va <= rva < va + size:
                return raw + rva - va
        raise ValueError("RVA outside image")

    def strings(rva, count):
        start = file_offset(rva)
        result = []
        for index in range(count):
            va = struct.unpack_from("<I", data, start + index * 4)[0]
            offset = file_offset(va - image_base)
            result.append(data[offset:data.index(b"\0", offset)].decode("ascii"))
        return result

    return {
        "sha256": hashlib.sha256(data).hexdigest(),
        "characterCodes": strings(0x66A8A8, 44),
        "characterNames": strings(0x66A958, 44),
        "stageCodes": strings(0x66B678, 30),
        "stageNames": strings(0x66B600, 30),
        "validEditions": [list(data[file_offset(0x539BA8) + index * 7:file_offset(0x539BA8) + (index + 1) * 7]) for index in range(44)],
    }


def extract(root):
    result = read_exe_tables(root / "SSFIV.exe")
    result["commands"] = {}
    # Later official title updates override earlier localized resources.
    paths = ["dlc/04_ae2", "patch_ae2", "patch_ae2_tu1", "patch_ae2_tu1b", "patch_ae2_tu2", "patch_ae2_tu3"]
    for code in result["characterCodes"]:
        path = None
        for layer in paths:
            candidate = root / layer / "ui/command_list/localize/ENG" / ("command_list_" + code.lower() + "_swan.m4s")
            if candidate.is_file():
                path = candidate
        if path is None:
            raise FileNotFoundError("No Ultra command list for " + code)
        entries = read_m4s(path)
        result["commands"][code] = {
            "source": path.relative_to(root).as_posix(),
            "sha256": hashlib.sha256(path.read_bytes()).hexdigest(),
            "entries": {k: v for k, v in entries.items() if ("CMD_" + code.upper() + "_") in k},
        }
    return result


if __name__ == "__main__":
    print(json.dumps(extract(Path(sys.argv[1])), indent=2, ensure_ascii=True))
