"""Renders a room from the GENERATED assets to PNG, so you can look at it.

    python preview.py [path/to/legend_of_clone]

Writes start_room.png and dungeon_room.png next to this script. No dependencies -
the PNG writer is twelve lines of zlib and struct.

It resolves the palette the way the engine does, in two hops: 4bpp pixel value
-> Color slot -> RGB565. That second hop is the point. A mapping that looks
reasonable can put Color::White at black, and the result is a picture that is
plausible and wrong rather than a crash - which is exactly what happened while
this example was being ported, and exactly what this caught.

Look at the render. Do not reason about the palette.
"""
import re
import sys
import pathlib
import struct
import zlib

sys.path.insert(0, str(pathlib.Path(__file__).parent))
from locate import locate_example  # noqa: E402

BASE = locate_example()
A = BASE / "src/assets"
OUT = pathlib.Path(__file__).resolve().parent

def block(src, marker, end="};"):
    b = src.split(marker)[1]
    return b[: b.index(end)]

def rgb565(v):
    r = (v >> 11) & 0x1F; g = (v >> 5) & 0x3F; b = v & 0x1F
    return (r*255//31, g*255//63, b*255//31)

SLOTS = {'Black':0,'White':1,'Navy':2,'Blue':3,'Cyan':4,'DarkGreen':5,'Green':6,
         'LightGreen':7,'Yellow':8,'Orange':9,'LightRed':10,'Red':11,'DarkRed':12,
         'Purple':13,'Magenta':14,'Gray':15}

def palette(header, data_name, map_name):
    src = (A / header).read_text(encoding="utf-8")
    data = [int(x,16) for x in re.findall(r"0x([0-9A-Fa-f]{4})", block(src, f"{data_name}[16] = {{"))]
    mapping = [SLOTS[m] for m in re.findall(r"Color::(\w+)", block(src, f"{map_name}[16] = {{"))]
    # pixel value -> RGB
    return [rgb565(data[s]) for s in mapping]

def tiles_of(cpp):
    src = (A / cpp).read_text(encoding="utf-8")
    pool = bytes(int(x,16) for x in re.findall(r"0x([0-9A-Fa-f]{2})", block(src, "TILESET_DATA_POOL[] = {")))
    idx_txt = block(src, "TERRAIN_INDICES[] = {")
    idx = []
    for line in idx_txt.splitlines():
        code = line.split("//")[0].strip()
        if code: idx.extend(int(x) for x in re.findall(r"\d+", code))
    n = len(pool)//128
    return [pool[i*128:(i+1)*128] for i in range(n)], idx

def px(tile, x, y):
    b = tile[y*8 + (x>>1)]
    return (b & 0x0F) if (x & 1) == 0 else (b >> 4)

def sprite_of(name):
    src = (A / "PlayerSprites.h").read_text(encoding="utf-8")
    words = [int(x,16) for x in re.findall(r"0x([0-9A-Fa-f]{4})", block(src, f"{name}_4BPP[] = {{"))]
    return bytes(b for w in words for b in (w & 0xFF, w >> 8))

def png(path, w, h, rows):
    raw = b"".join(b"\x00" + bytes(v for p in row for v in p) for row in rows)
    def chunk(t, d):
        c = t + d
        return struct.pack(">I", len(d)) + c + struct.pack(">I", zlib.crc32(c))
    path.write_bytes(b"\x89PNG\r\n\x1a\n"
        + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
        + chunk(b"IDAT", zlib.compress(raw)) + chunk(b"IEND", b""))

def render(cpp, header, room_col, room_row, out, player=None):
    tpal = palette("TilemapPalette.h", "TILEMAP_PALETTE_DATA", "TILEMAP_PALETTE_MAPPING")
    spal = palette("PlayerPalette.h", "PLAYER_SPRITE_PALETTE_RGB565", "PLAYER_PALETTE_MAPPING")
    tiles, idx = tiles_of(cpp)
    W, H = 15*16, 11*16
    fb = [[(255,0,255)]*W for _ in range(H)]
    for r in range(11):
        for c in range(15):
            t = idx[(room_row*11 + r)*30 + (room_col*15 + c)]
            if t == 0: continue
            for y in range(16):
                for x in range(16):
                    v = px(tiles[t], x, y)
                    if v: fb[r*16+y][c*16+x] = tpal[v]
    if player:
        name, pxx, pyy = player
        s = sprite_of(name)
        for y in range(16):
            for x in range(16):
                v = px(s, x, y)
                if v: fb[pyy+y][pxx+x] = spal[v]
    png(out, W, H, fb)
    print("wrote", out)

render("OverworldTileMap.cpp", "TilemapPalette.h", 0, 1,
       OUT/"start_room.png", ("PLAYER_DOWN", 7*16, (18-11)*16))
render("DungeonTileMap.cpp", "TilemapPalette.h", 0, 1,
       OUT/"dungeon_room.png", ("PLAYER_UP", 7*16, (18-11)*16))
