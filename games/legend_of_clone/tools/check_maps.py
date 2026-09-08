"""Validates both maps against their room tables and the two doorway constants.

    python check_maps.py [path/to/legend_of_clone]

Run it after editing a map in art_source.py. The failures it catches are the
ones that do not look like bugs from the code: a room border that declares a
connection the tiles wall off, a spawn cell that lands on the very tile that
triggers a scene change, an edge opening the room graph knows nothing about.
Each of those produces a game that runs and misbehaves, which is the expensive
kind.

Reads the real sources - art_source.py for the maps, the room headers for the
tables, GameConstants.h for the doorway cells - so drift between the three shows
up here rather than on screen.
"""
import pathlib
import re
import sys

sys.path.insert(0, str(pathlib.Path(__file__).parent))
import art_source as art  # noqa: E402
from locate import locate_example  # noqa: E402

SRC = locate_example() / "src"

COLS, ROWS = art.ROOM_COLS, art.ROOM_ROWS
WCOLS, WROWS = art.MAP_COLS, art.MAP_ROWS
DIRS = ["Up", "Down", "Left", "Right"]

fail = []


def parse_rooms(path, marker):
    """The room table, as (originCol, originRow, [connections]) per room."""
    src = (SRC / path).read_text(encoding="utf-8")
    table = src.split(marker)[1]
    table = table[: table.index("};")]
    rooms = []
    for line in table.splitlines():
        m = re.search(r"\{\s*(\d+),\s*(\d+),\s*kRoomCols,\s*kRoomRows,\s*\{([^}]*)\}", line)
        if not m:
            continue
        conns = [t.strip() for t in m.group(3).split(",")]
        conns = [None if t == "kWall" else int(t) for t in conns]
        rooms.append((int(m.group(1)), int(m.group(2)), conns))
    return rooms


def check(name, rows, rooms, legend, tiles):
    """One map: shape, sealed border, and every declared connection walkable."""
    solid_by_tile = {t[0]: t[1] for t in tiles}
    open_chars = {c for c, tile in legend.items() if not solid_by_tile[tile]}
    solid_chars = set(legend) - open_chars

    if len(rows) != WROWS:
        fail.append(f"{name}: expected {WROWS} rows, got {len(rows)}")
        return
    for i, r in enumerate(rows):
        if len(r) != WCOLS:
            fail.append(f"{name} row {i}: length {len(r)}, expected {WCOLS}")
        bad = set(r) - open_chars - solid_chars
        if bad:
            fail.append(f"{name} row {i}: unknown chars {sorted(bad)}")
    if len(rooms) != 4:
        fail.append(f"{name}: expected 4 rooms, parsed {len(rooms)}")
        return

    # The outer border must be sealed. A gap there is a player walking off the
    # world, and the room graph has nothing on the other side to catch them.
    for c in range(WCOLS):
        if rows[0][c] in open_chars:
            fail.append(f"{name}: top edge open at col {c}")
        if rows[WROWS - 1][c] in open_chars:
            fail.append(f"{name}: bottom edge open at col {c}")
    for r in range(WROWS):
        if rows[r][0] in open_chars:
            fail.append(f"{name}: left edge open at row {r}")
        if rows[r][WCOLS - 1] in open_chars:
            fail.append(f"{name}: right edge open at row {r}")

    for idx, (oc, orow, conns) in enumerate(rooms):
        for d, target in enumerate(conns):
            if target is None:
                continue
            toc, tor, _ = rooms[target]
            nm = DIRS[d]
            expect = {"Up": (oc, orow - ROWS), "Down": (oc, orow + ROWS),
                      "Left": (oc - COLS, orow), "Right": (oc + COLS, orow)}[nm]
            if (toc, tor) != expect:
                fail.append(f"{name} room {idx} {nm} -> {target} is not the adjacent room")

            # A declared connection needs at least one pair of facing open cells
            # across the seam, or it is decorative and the player hits a wall.
            pairs = 0
            if nm in ("Left", "Right"):
                near = oc if nm == "Left" else oc + COLS - 1
                far = near - 1 if nm == "Left" else near + 1
                for r in range(orow, orow + ROWS):
                    if rows[r][near] in open_chars and rows[r][far] in open_chars:
                        pairs += 1
            else:
                near = orow if nm == "Up" else orow + ROWS - 1
                far = near - 1 if nm == "Up" else near + 1
                for c in range(oc, oc + COLS):
                    if rows[near][c] in open_chars and rows[far][c] in open_chars:
                        pairs += 1
            if pairs == 0:
                fail.append(f"{name} room {idx} {nm} -> {target}: declared but the seam is walled")

        # And the reverse: an opening with no connection behind it. Usually a
        # forgotten entry in the room table rather than a deliberate dead end.
        edges = [
            ("Up", 0, [(orow, c) for c in range(oc, oc + COLS)]),
            ("Down", 1, [(orow + ROWS - 1, c) for c in range(oc, oc + COLS)]),
            ("Left", 2, [(r, oc) for r in range(orow, orow + ROWS)]),
            ("Right", 3, [(r, oc + COLS - 1) for r in range(orow, orow + ROWS)]),
        ]
        for nm, d, cells in edges:
            if any(rows[r][c] in open_chars for r, c in cells) and conns[d] is None:
                fail.append(f"{name} room {idx} {nm} edge has open cells but no connection")


check("overworld", art.OVERWORLD_MAP,
      parse_rooms("assets/OverworldRooms.h", "OVERWORLD_ROOMS[] = {"),
      art.OVERWORLD_MAP_LEGEND, art.OVERWORLD_TILES)

check("dungeon", art.DUNGEON_MAP,
      parse_rooms("assets/DungeonRooms.h", "DUNGEON_ROOMS[] = {"),
      art.DUNGEON_MAP_LEGEND, art.DUNGEON_TILES)

# --- the doorway: both ends must be what the constants claim -----------------

constants = (SRC / "GameConstants.h").read_text(encoding="utf-8")


def const(nm):
    return int(re.search(nm + r"\s*=\s*(\d+)", constants).group(1))


def cell(rows, cr):
    c, r = cr
    return rows[r][c] if 0 <= r < WROWS and 0 <= c < WCOLS else "?"


start = (const("kPlayerStartCol"), const("kPlayerStartRow"))
cave_exit = (const("kCaveExitCol"), const("kCaveExitRow"))
entry = (const("kDungeonEntryCol"), const("kDungeonEntryRow"))

if cell(art.OVERWORLD_MAP, start) not in ".,":
    fail.append(f"player start {start} is '{cell(art.OVERWORLD_MAP, start)}', not plain walkable")
if cell(art.OVERWORLD_MAP, cave_exit) == "C":
    fail.append("cave exit lands ON the cave - the player would re-enter forever")
elif cell(art.OVERWORLD_MAP, cave_exit) not in ".,":
    fail.append(f"cave exit {cave_exit} is '{cell(art.OVERWORLD_MAP, cave_exit)}', not plain walkable")
if cell(art.DUNGEON_MAP, entry) == "S":
    fail.append("dungeon entry lands ON the stairs - the player would bounce straight out")
elif cell(art.DUNGEON_MAP, entry) != ".":
    fail.append(f"dungeon entry {entry} is '{cell(art.DUNGEON_MAP, entry)}', not plain floor")

if "C" not in "".join(art.OVERWORLD_MAP):
    fail.append("no cave mouth in the overworld map")
if "S" not in "".join(art.DUNGEON_MAP):
    fail.append("no stairs in the dungeon map")

# Leaving should be one step away from where you arrive, or the entrance room
# needs a reason to be walked across that it does not have yet.
adjacent = [(entry[0], entry[1] + 1), (entry[0], entry[1] - 1),
            (entry[0] + 1, entry[1]), (entry[0] - 1, entry[1])]
if not any(cell(art.DUNGEON_MAP, a) == "S" for a in adjacent):
    fail.append("dungeon entry is not adjacent to the stairs")

if fail:
    print("FAILURES:")
    for f in fail:
        print("  -", f)
    sys.exit(1)
print("OK: both maps, both room tables, and the doorway cells are consistent")
