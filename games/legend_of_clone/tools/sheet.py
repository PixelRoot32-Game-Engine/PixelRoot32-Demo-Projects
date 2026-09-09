"""Renders every player sprite from art_source, zoomed, with a pixel grid.

For looking at the character while editing it. Reads the authoring format
directly, so it shows what you are about to export rather than what you last
exported.
"""
import sys, pathlib, struct, zlib
sys.path.insert(0, str(pathlib.Path(__file__).parent))
import art_source as art

Z = 10
GAP = 8
OUT = pathlib.Path(__file__).resolve().parent

def rgb(v):
    r=(v>>11)&0x1F; g=(v>>5)&0x3F; b=v&0x1F
    return (r*255//31, g*255//63, b*255//31)

def png(path, w, h, rows):
    raw = b"".join(b"\x00" + bytes(v for p in row for v in p) for row in rows)
    def chunk(t, d):
        c = t+d
        return struct.pack(">I", len(d)) + c + struct.pack(">I", zlib.crc32(c))
    path.write_bytes(b"\x89PNG\r\n\x1a\n"
        + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
        + chunk(b"IDAT", zlib.compress(raw)) + chunk(b"IEND", b""))

names = [s[0] for s in art.PLAYER_SPRITES]
W = len(names)*(16*Z+GAP)+GAP
H = 16*Z+2*GAP
fb = [[(40,40,48)]*W for _ in range(H)]
for i,(name,doc,rows) in enumerate(art.PLAYER_SPRITES):
    ox = GAP + i*(16*Z+GAP)
    for y in range(16):
        for x in range(16):
            c = rows[y][x]
            col = rgb(art.COLORS[c][0]) if c != ' ' else (24,24,30)
            for dy in range(Z):
                for dx in range(Z):
                    # faint grid so columns are countable
                    edge = (dx == 0 or dy == 0)
                    fb[GAP+y*Z+dy][ox+x*Z+dx] = tuple(min(255,v+18) for v in col) if edge else col
png(OUT/"player_sheet.png", W, H, fb)
print("wrote", OUT/"player_sheet.png", " order:", ", ".join(names))
