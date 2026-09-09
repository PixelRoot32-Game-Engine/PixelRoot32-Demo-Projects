"""Unpacks the GENERATED C++ back to characters and diffs it against art_source.

    python verify_export.py [path/to/legend_of_clone]

Proves the exporter is lossless in the only way that counts: by reading the real
files it wrote, not an in-memory copy of what it thought it wrote. Every tile,
every sprite, every map cell and every collision flag has to survive the round
trip.

Worth running because the failure it catches is silent. A wrong nibble order
mirrors each PAIR of pixels, which produces art that looks almost right - much
harder to spot by eye than art that looks broken.
"""
import re
import sys
import pathlib

sys.path.insert(0, str(pathlib.Path(__file__).parent))
import art_source as art  # noqa: E402
from locate import locate_example  # noqa: E402

BASE = locate_example()

fail = []

def palette_of(header, name):
    """4bpp pixel value -> art character, read from the MAPPING table's comments."""
    src = (BASE / "src/assets" / header).read_text(encoding="utf-8")
    blk = src.split(f"{name}[16] = {{")[1]
    blk = blk[: blk.index("};")]
    out = {}
    for line in blk.splitlines():
        m = re.search(r"//\s*(\d+)\s+'(.)' -> (\w+)", line)
        if m:
            out[int(m.group(1))] = m.group(2)
    return out

def unpack(data, idx2char):
    rows = []
    for r in range(16):
        row = data[r*8:(r+1)*8]
        line = ""
        for b in row:
            line += idx2char.get(b & 0x0F, " ")
            line += idx2char.get(b >> 4, " ")
        rows.append(line)
    return rows

# ---- tilesets -------------------------------------------------------------
tpal = palette_of("TilemapPalette.h", "TILEMAP_PALETTE_MAPPING")
for cppname, tiles in (("OverworldTileMap.cpp", art.OVERWORLD_TILES),
                       ("DungeonTileMap.cpp", art.DUNGEON_TILES)):
    src = (BASE / "src/assets" / cppname).read_text(encoding="utf-8")
    blk = src.split("TILESET_DATA_POOL[] = {")[1]
    blk = blk[: blk.index("};")]
    data = bytes(int(x, 16) for x in re.findall(r"0x([0-9A-Fa-f]{2})", blk))
    if len(data) != len(tiles) * 128:
        fail.append(f"{cppname}: pool is {len(data)} bytes, expected {len(tiles)*128}")
        continue
    for i, (name, solid, doc, rows) in enumerate(tiles):
        got = unpack(data[i*128:(i+1)*128], tpal)
        if got != list(rows):
            fail.append(f"{cppname} {name} does not round-trip")
            for a, b in zip(rows, got):
                if a != b: fail.append(f"    want {a!r}\n     got {b!r}")

# ---- sprites --------------------------------------------------------------
spal = palette_of("PlayerPalette.h", "PLAYER_PALETTE_MAPPING")
src = (BASE / "src/assets/PlayerSprites.h").read_text(encoding="utf-8")
for name, doc, rows in art.PLAYER_SPRITES:
    blk = src.split(f"{name}_4BPP[] = {{")[1]
    blk = blk[: blk.index("};")]
    words = [int(x, 16) for x in re.findall(r"0x([0-9A-Fa-f]{4})", blk)]
    data = bytes(b for w in words for b in (w & 0xFF, w >> 8))
    got = unpack(data, spal)
    if got != list(rows):
        fail.append(f"{name} does not round-trip")
        for a, b in zip(rows, got):
            if a != b: fail.append(f"    want {a!r}\n     got {b!r}")

# ---- map indices ----------------------------------------------------------
for cppname, tiles, rows, legend in (
        ("OverworldTileMap.cpp", art.OVERWORLD_TILES, art.OVERWORLD_MAP, art.OVERWORLD_MAP_LEGEND),
        ("DungeonTileMap.cpp", art.DUNGEON_TILES, art.DUNGEON_MAP, art.DUNGEON_MAP_LEGEND)):
    src = (BASE / "src/assets" / cppname).read_text(encoding="utf-8")
    blk = src.split("TERRAIN_INDICES[] = {")[1]
    blk = blk[: blk.index("};")]
    names = [t[0] for t in tiles]
    got = []
    for line in blk.splitlines():
        code = line.split("//")[0].strip()
        if code:
            got.extend(int(x) for x in re.findall(r"\d+", code))
    want = [names.index(legend[ch]) for line in rows for ch in line]
    if got != want:
        fail.append(f"{cppname}: indices differ from the map ({len(got)} vs {len(want)} cells)")

# ---- solidity -------------------------------------------------------------
for cppname, tiles in (("OverworldTileMap.cpp", art.OVERWORLD_TILES),
                       ("DungeonTileMap.cpp", art.DUNGEON_TILES)):
    src = (BASE / "src/assets" / cppname).read_text(encoding="utf-8")
    blk = src.split("TILE_SOLID[TILE_COUNT] = {")[1]
    blk = blk[: blk.index("};")]
    got = [t == "true" for t in re.findall(r"\b(true|false)\b", blk)]
    want = [t[1] for t in tiles]
    if got != want:
        fail.append(f"{cppname}: TILE_SOLID {got} != {want}")

if fail:
    print("FAILURES:")
    for f in fail: print(" -", f)
    sys.exit(1)
print("OK: every tile, sprite, map cell and collision flag round-trips through the export")
