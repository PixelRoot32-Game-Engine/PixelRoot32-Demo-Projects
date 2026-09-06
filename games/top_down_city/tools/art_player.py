"""The walking figures of the top-down city: nine 16x16 player frames --
three facings (down, up, side), a standing pose and two walk poses each --
plus the squashed pose and the pedestrian recolours that share them.

The figure follows the reference pedestrians: a compact silhouette that is
mostly outline, a dark hair blob on top, one or two skin pixels for the face,
a single patch of jacket colour on the torso, short legs and dark shoes. It
is 7 px wide and 10 px tall standing (11 px with a foot forward), centred in
the cell with the feet on rows 12-14, so the collision box over rows 9-14
covers the legs. ``side`` faces right; the engine mirrors it for left.

This is not a scene slot: the frames go through the engine's sprite palette
bank, so ``PALETTE`` is fixed to the 15 entries the C++ side names, and the
tile dictionaries are empty.

Pedestrians reuse these exact frames and cost no extra flash for them: each
one draws through a different SPRITE PALETTE SLOT, and ``PEDESTRIAN_TINTS``
below is the list of recolours those slots hold. Only the squashed pose is
new pixels.
"""
import art_dsl

SLOT = "PLAYER"

# Order and names are referenced by C++ (PlayerSprites.h) -- do not reorder.
PALETTE = [
    ("OUTLINE",    0x1E, 0x1E, 0x1E),
    ("WHITE",      0xDE, 0xDE, 0xDE),
    ("SKIN",       0xF2, 0xC0, 0x9A),
    ("SKIN_DARK",  0xC0, 0x80, 0x60),
    ("HAIR",       0x2B, 0x2B, 0x2B),
    ("SHIRT",      0xD7, 0x29, 0x29),
    ("SHIRT_DARK", 0x99, 0x16, 0x16),
    ("PANTS",      0x25, 0x44, 0xE3),
    ("PANTS_DARK", 0x26, 0x5C, 0xA0),
    ("SHOE",       0x4E, 0x4E, 0x4E),
    # Never worn by the character: a red, a grey, a sand, a grass and a water
    # blue. These five were added so the old minimap could nearest-match
    # terrain into them -- which it never did: the overlay is drawn with
    # PRIMITIVES, and a primitive resolves against the engine's palette, never
    # against this one. They stay because reordering this list would renumber
    # every sprite, and because GREY still earns its place in the check below.
    ("RED",        0xD7, 0x29, 0x29),
    ("GREY",       0x88, 0x86, 0x86),
    ("SAND",       0xF6, 0xBC, 0x69),
    ("GRASS",      0x24, 0x8C, 0x17),
    ("BLUE",       0x27, 0x95, 0xF2),
]

INK = {
    "#": "OUTLINE",
    "W": "WHITE",
    "S": "SKIN",
    "s": "SKIN_DARK",
    "H": "HAIR",
    "J": "SHIRT",
    "d": "SHIRT_DARK",
    "P": "PANTS",
    "p": "PANTS_DARK",
    "O": "SHOE",
    "R": "RED",
    # Gunmetal is SHOE and the sheen is WHITE, both already in the palette:
    # arming the player costs no palette entry, and the bank has none to give.
    #
    # NOT "GREY". That entry is 0x888686, the URBAN palette's ASPHALT to the
    # byte -- a leftover from the era when the minimap nearest-matched tiles
    # into this palette, which it no longer does (see minimap_swatches). The
    # coincidence outlived the reason: a pistol drawn in it is a pistol the
    # exact colour of the street it lies on, which is how the first version
    # shipped: invisible on the ground, findable by nobody.
    # generate_city_assets.py fails the build if a gun uses it again.
}

# Every frame is authored as a 7x11 block and placed at (5, 4) inside the
# 16x16 cell: columns 5..11, rows 4..14. Standing poses leave row 14 empty.
BLOCK_W, BLOCK_H = 7, 11
BLOCK_X, BLOCK_Y = 5, 4


def _cell(block: list[str]) -> list[str]:
    """Pad a 7x11 block into a 16x16 frame."""
    if len(block) != BLOCK_H or any(len(r) != BLOCK_W for r in block):
        raise ValueError("player block must be 7x11")
    blank = "." * art_dsl.TILE
    rows = [blank] * BLOCK_Y
    for r in block:
        rows.append("." * BLOCK_X + r + "." * (art_dsl.TILE - BLOCK_X - BLOCK_W))
    rows += [blank] * (art_dsl.TILE - BLOCK_Y - BLOCK_H)
    return rows


# --- facing down ----------------------------------------------------------
# Head and torso are shared by the three frames; only rows 7..10 of the block
# (legs) and one sleeve pixel change, so the walk does not jitter.
DOWN_BODY = [
    "..###..",   # crown
    ".#HHH#.",   # hair
    ".#HHH#.",   # hair
    ".#SSs#.",   # face, shadow side on the right
    "##JJJ##",   # shoulders
    "#JJJJd#",   # jacket, far sleeve in shade
    "#JJJJd#",
]
DOWN_LEGS = [
    [   # standing
        ".#PPP#.",
        ".#O#O#.",
        ".#####.",
        ".......",
    ],
    [   # left foot forward: drops a pixel, right foot stays planted
        ".#PPP#.",
        ".#P#O#.",
        ".#O###.",
        ".###...",
    ],
    [   # right foot forward
        ".#PPP#.",
        ".#O#P#.",
        ".###O#.",
        "...###.",
    ],
]


def _swing(body: list[str], side: int) -> list[str]:
    """Shorten one sleeve by a pixel: -1 = left arm back, +1 = right arm back."""
    body = list(body)
    last = body[-1]
    if side < 0:
        body[-1] = "." + last[1:]
    elif side > 0:
        body[-1] = last[:-1] + "."
    return body


DOWN = [
    _cell(DOWN_BODY + DOWN_LEGS[0]),
    _cell(_swing(DOWN_BODY, +1) + DOWN_LEGS[1]),
    _cell(_swing(DOWN_BODY, -1) + DOWN_LEGS[2]),
]

# --- facing up --------------------------------------------------------------
# Same body seen from behind: all hair, no face. Walking away, the foot that
# steps forward is the one hidden behind the body, so the leg patterns swap.
UP_BODY = [
    "..###..",
    ".#HHH#.",
    ".#HHH#.",
    ".#HHH#.",
    "##JJJ##",
    "#JJJJd#",
    "#JJJJd#",
]
UP = [
    _cell(UP_BODY + DOWN_LEGS[0]),
    _cell(_swing(UP_BODY, +1) + DOWN_LEGS[2]),
    _cell(_swing(UP_BODY, -1) + DOWN_LEGS[1]),
]

# --- facing right -----------------------------------------------------------
# Profile: 5 px wide, hair at the back of the head, two skin pixels at the
# front, the near arm as a dark stripe down the jacket. The walk frames put
# the front foot forward and low, the trailing leg back in the darker pants.
SIDE_HEAD = [
    "..###..",
    ".#HHH#.",
    ".#HHS#.",
    ".#HSS#.",
    ".##JJ#.",
]
SIDE_ARM = {
    0: [".#JdJ#.", ".#JdJ#."],   # arm hanging
    1: [".#JJd#.", ".#JJd#."],   # arm swung forward
    2: [".#dJJ#.", ".#dJJ#."],   # arm swung back
}
SIDE_LEGS = [
    [   # standing: both feet together, toe pointing right
        ".#PPP#.",
        ".#OOO#.",
        ".#####.",
        ".......",
    ],
    [   # left (far) foot forward and low, right foot back with the heel up
        ".#pPP#.",
        "#O#PP#.",
        ".##OO#.",
        "..####.",
    ],
    [   # right (near) foot forward and lifted, left leg trailing on the ground
        ".#PPp#.",
        ".#pp#O#",
        "#OO####",
        "####...",
    ],
]
SIDE = [
    _cell(SIDE_HEAD + SIDE_ARM[0] + SIDE_LEGS[0]),
    _cell(SIDE_HEAD + SIDE_ARM[1] + SIDE_LEGS[1]),
    _cell(SIDE_HEAD + SIDE_ARM[2] + SIDE_LEGS[2]),
]

# --- squashed pedestrian ----------------------------------------------------
# What a pedestrian becomes under a car: the same figure seen from above with
# nothing left standing. Authored as an 11x7 block -- wider and flatter than
# the 7x11 walking one, which is the whole read -- and centred low in the cell
# so it lands where the feet were rather than where the head was. It draws
# through the pedestrian's own palette slot, so the body keeps the colours that
# pedestrian was wearing; only RED is shared, the puddle not being an outfit.
SQUASH_W, SQUASH_H = 11, 7
SQUASH_X, SQUASH_Y = 3, 6

SQUASHED_BLOCK = [
    "....###....",
    "..#RJJJR#..",
    ".#RJHHHJR#.",
    "#RJJdJJdJR#",
    ".#RJPPPJR#.",
    "..#RpppR#..",
    "...#####...",
]


def _squash_cell(block: list[str]) -> list[str]:
    """Pad the 11x7 squashed block into a 16x16 frame."""
    if len(block) != SQUASH_H or any(len(r) != SQUASH_W for r in block):
        raise ValueError("squashed block must be 11x7")
    blank = "." * art_dsl.TILE
    rows = [blank] * SQUASH_Y
    for r in block:
        rows.append("." * SQUASH_X + r + "." * (art_dsl.TILE - SQUASH_X - SQUASH_W))
    rows += [blank] * (art_dsl.TILE - SQUASH_Y - SQUASH_H)
    return rows


SQUASHED = _squash_cell(SQUASHED_BLOCK)

# --- pedestrian recolours ---------------------------------------------------
# One entry per sprite palette slot the pedestrians get. Each is the player
# palette with these entries replaced, so index meanings never move: the frames
# are the player's frames and the C++ side only changes the slot it draws them
# through. None of them wears the player's red-over-blue, which is the point:
# on a 240x240 panel the player must stay findable in a crowd at a glance.
PEDESTRIAN_TINTS = [
    ("GREEN", {
        "SKIN":       (0xC6, 0x8B, 0x62),
        "SKIN_DARK":  (0x9A, 0x66, 0x44),
        "HAIR":       (0x4A, 0x2E, 0x18),
        "SHIRT":      (0x2E, 0x9B, 0x4C),
        "SHIRT_DARK": (0x1C, 0x6B, 0x34),
        "PANTS":      (0xB0, 0x83, 0x6D),
        "PANTS_DARK": (0x81, 0x5D, 0x4C),
    }),
    ("VIOLET", {
        "SKIN":       (0xF6, 0xD2, 0xB0),
        "SKIN_DARK":  (0xCE, 0xA2, 0x80),
        "HAIR":       (0xD7, 0xB0, 0x4A),
        "SHIRT":      (0x8E, 0x44, 0xC8),
        "SHIRT_DARK": (0x63, 0x2C, 0x8E),
        "PANTS":      (0x4E, 0x4E, 0x4E),
        "PANTS_DARK": (0x33, 0x33, 0x33),
    }),
    ("TEAL", {
        "SKIN":       (0x8A, 0x5A, 0x3C),
        "SKIN_DARK":  (0x63, 0x3E, 0x28),
        "HAIR":       (0x1E, 0x1E, 0x1E),
        "SHIRT":      (0x23, 0x9C, 0x98),
        "SHIRT_DARK": (0x14, 0x6E, 0x6B),
        "PANTS":      (0x7C, 0x4A, 0x1E),
        "PANTS_DARK": (0x5B, 0x36, 0x16),
    }),
    ("SAND", {
        "SKIN":       (0xF2, 0xC0, 0x9A),
        "SKIN_DARK":  (0xC0, 0x80, 0x60),
        "HAIR":       (0xC9, 0xC7, 0xC5),
        "SHIRT":      (0xE6, 0xC4, 0x6A),
        "SHIRT_DARK": (0xB4, 0x92, 0x3E),
        "PANTS":      (0x1C, 0x6B, 0x34),
        "PANTS_DARK": (0x12, 0x4A, 0x24),
    }),
]

# --- the uniform -------------------------------------------------------------
# Deliberately NOT in PEDESTRIAN_TINTS. The crowd picks its recolour at random
# and an officer who turns up on a street corner because the dice said so is a
# police force by accident; this one is spawned by the station and only by it.
#
# It is the same substitution as any other tint, so an officer is still the
# player's nine frames through one more palette slot -- 32 bytes, not a sprite
# sheet. HAIR carries the cap, which is what makes the silhouette read as a
# uniform at 16x16: navy over navy, one shade apart, with the shirt light
# enough to separate from the trousers.
POLICE_TINT = ("POLICE", {
    "SKIN":       (0xE8, 0xB4, 0x8C),
    "SKIN_DARK":  (0xB8, 0x86, 0x60),
    # The peaked cap. Bluer and a shade darker than it looks like it wants to
    # be, because at (0x14, 0x22, 0x4E) it quantised to the same 12-bit value
    # as the trouser shadow in broad daylight.
    "HAIR":       (0x10, 0x18, 0x52),
    "SHIRT":      (0x2C, 0x52, 0x9E),
    "SHIRT_DARK": (0x1B, 0x33, 0x68),
    # Lighter than the first draft of them, and not for taste: at
    # (0x16, 0x1C, 0x2C) the trouser shadow and the OUTLINE both quantised to
    # 0x001 under the night tint on the 12-bit panel, so the legs lost their
    # shape after dark on hardware and kept it in the simulator.
    # `python tools/check_rgb444.py --ambient 88,104,160` is what says so.
    "PANTS":      (0x2E, 0x3A, 0x62),
    "PANTS_DARK": (0x1C, 0x24, 0x42),
})


FRAMES = {
    "down": DOWN,
    "up": UP,
    "side": SIDE,
}

# --- armed variants ---------------------------------------------------------
# The same nine frames with a pistol stamped into them, rather than nine more
# bodies authored by hand: two hand-authored sets would put the walk cycle in
# two places, and the day one is nudged the other silently stops matching. The
# body has one source; the gun is an overlay on top of it.
#
# The stamp is placed in the 16x16 cell and not in the 7x11 block, because the
# whole point of the gun is that it sticks out past the silhouette -- an armed
# figure has to read from an unarmed one at 16 px on a 240 px panel.
GUN_STAMPS = {
    # facing: (x, y, block). "." leaves whatever is underneath alone.
    #
    # All three sit at row 10, where the hand hangs in every facing. Each
    # starts on the column immediately outside the body, because a gun with a
    # gap between it and the sleeve reads as floating rather than as held.
    "down": (2, 10, ["#W#",
                     ".#."]),       # the character's right hand, viewer's left
    "up":   (12, 10, ["#W#",
                      ".#."]),      # same hand seen from behind, so mirrored
    "side": (10, 10, [".WO#",
                      "##.."]),     # arm extended, barrel out past the body
}


def _stamp(cell: list[str], x: int, y: int, block: list[str]) -> list[str]:
    """Overlay `block` onto a 16x16 cell. '.' in the block is transparent."""
    rows = list(cell)
    for dy, line in enumerate(block):
        row = list(rows[y + dy])
        for dx, ch in enumerate(line):
            if ch != ".":
                row[x + dx] = ch
        rows[y + dy] = "".join(row)
    return rows


ARMED_FRAMES = {
    facing: [_stamp(cell, *GUN_STAMPS[facing]) for cell in cells]
    for facing, cells in FRAMES.items()
}

# --- the pistol on the ground -----------------------------------------------
# What the weapon looks like lying in the street, before anybody has picked it
# up. Drawn larger than the one in the character's hand and side-on, because a
# two-pixel blob on the pavement is scenery and this has to read as an object
# worth walking to. Two-tone on purpose: the frame is SHOE, darker than any
# ground in the city, and the top of the slide is WHITE, brighter than all of
# them. Between the two it reads on asphalt, on the brown sidewalk it actually
# lies on, on beach sand and on grass -- and it keeps reading under the night
# tint, which scales colours but cannot close the gap between the two.
PICKUP_BLOCK = [
    "..........",
    "..######..",
    ".#WWWWWW#.",
    ".#OOOO##..",
    ".#OO#.....",
    ".#OO#.....",
    ".####.....",
    "..........",
]
PICKUP_X, PICKUP_Y = 3, 4

# The shotgun on the ground. Longer than the pistol and drawn lying flat -- the
# silhouette is the whole message at 16 px, and a player who has walked past
# three pistols has to tell at a glance that this one is not. No GREY anywhere,
# for the same reason nothing else here uses it: the player palette's GREY is
# the asphalt colour to the byte, and a weapon in it vanishes in the street.
SHOTGUN_BLOCK = [
    "..........",
    "..######..",
    ".#WWWWWW#.",
    ".#OOOOOO#.",
    "#OOOO#####",
    "#OO##.....",
    "####......",
    "..........",
]
SHOTGUN_X, SHOTGUN_Y = 2, 4


def _pickup_cell(block: list[str]) -> list[str]:
    blank = "." * art_dsl.TILE
    rows = [blank] * PICKUP_Y
    for r in block:
        rows.append("." * PICKUP_X + r
                    + "." * (art_dsl.TILE - PICKUP_X - len(r)))
    while len(rows) < art_dsl.TILE:
        rows.append(blank)
    return rows


PICKUP = _pickup_cell(PICKUP_BLOCK)


def _shotgun_cell(block: list[str]) -> list[str]:
    blank = "." * art_dsl.TILE
    rows = [blank] * SHOTGUN_Y
    for r in block:
        rows.append("." * SHOTGUN_X + r
                    + "." * (art_dsl.TILE - SHOTGUN_X - len(r)))
    while len(rows) < art_dsl.TILE:
        rows.append(blank)
    return rows[:art_dsl.TILE]


SHOTGUN_PICKUP = _shotgun_cell(SHOTGUN_BLOCK)

# Not a tile module; kept so art_dsl.preview still accepts it.
TILES = {}
STAMPS = {}
SHADOWS = {}
DETAILS = {}
FAMILY = {}


def frame_grids() -> dict[str, list[list[list[int]]]]:
    """Resolve FRAMES to palette-index grids, validating every row."""
    lookup = art_dsl.palette_lookup(PALETTE)
    return {facing: [art_dsl.parse(rows, INK, lookup, label=f"{facing}[{i}]")
                     for i, rows in enumerate(frames)]
            for facing, frames in FRAMES.items()}


def armed_frame_grids() -> dict[str, list[list[list[int]]]]:
    """The nine walk frames with the pistol stamped in."""
    lookup = art_dsl.palette_lookup(PALETTE)
    return {facing: [art_dsl.parse(rows, INK, lookup,
                                   label=f"armed {facing}[{i}]")
                     for i, rows in enumerate(frames)]
            for facing, frames in ARMED_FRAMES.items()}


def shotgun_pickup_grid() -> list[list[int]]:
    """The shotgun lying in the street, as palette indices."""
    lookup = {name: i + 1 for i, (name, _r, _g, _b) in enumerate(PALETTE)}
    return art_dsl.parse(SHOTGUN_PICKUP, INK, lookup, label="shotgun pickup")


def pickup_grid() -> list[list[int]]:
    """The pistol lying on the ground, waiting to be walked over."""
    lookup = art_dsl.palette_lookup(PALETTE)
    return art_dsl.parse(PICKUP, INK, lookup, label="pickup")


def preview_frames(path: str, scale: int = 8, background=(0x88, 0x86, 0x86)) -> None:
    """Render the nine frames as a 3x3 grid (one facing per row) for review."""
    from PIL import Image  # review-only dependency

    rgb = [background] + [(r, g, b) for _n, r, g, b in PALETTE]
    grids = frame_grids()
    gap = 2
    t = art_dsl.TILE
    im = Image.new("RGB", (3 * (t + gap), 3 * (t + gap)), background)
    px = im.load()
    for fy, facing in enumerate(("down", "up", "side")):
        for fx, grid in enumerate(grids[facing]):
            for y in range(t):
                for x in range(t):
                    v = grid[y][x]
                    if v:
                        px[fx * (t + gap) + x, fy * (t + gap) + y] = rgb[v]
    im = im.resize((im.width * scale, im.height * scale), Image.NEAREST)
    im.save(path)
    print(f"art_player: 9 frames -> {path}")
