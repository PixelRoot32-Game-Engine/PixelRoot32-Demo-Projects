"""Houses and a shop front for the Items layer (palette slot BUILDINGS_C).

Five multi-tile stamps seen straight from above: two small red hip-roof
houses (porch right / porch left), the larger suburban red house on a lawn,
the beige beach house with its skylight, and a tan shop roof with two glass
panels. Every stamp is the whole building on a transparent surround: a 1 px
black outline, a light plinth where the reference has one, and the drop
shadow (offset down-right) inside the stamp rows. Nothing of the ground is
baked in.

The building itself is drawn as literal rows; ``_stamp`` places it on the
stamp canvas with a 1 px transparent margin top/left and fills the cast
shadow into the transparent cells the building would cover when shifted.
"""
import art_dsl

SLOT = "BUILDINGS_C"

PALETTE = [
    ("OUTLINE",      0x1E, 0x1E, 0x1E),
    ("WHITE",        0xDE, 0xDE, 0xDE),
    ("LIGHT_GREY",   0xC9, 0xC7, 0xC5),
    ("SHADOW_GREY",  0x6D, 0x6B, 0x6B),
    ("SHADOW_GRASS", 0x2E, 0x59, 0x28),
    ("RED",          0xD7, 0x29, 0x29),
    ("RED_FACET",    0xAF, 0x20, 0x20),
    ("RED_LIGHT",    0xDB, 0x3E, 0x3E),
    ("RED_DARK",     0x99, 0x16, 0x16),
    ("TAN",          0xA6, 0x72, 0x59),
    # A lit window, and the one entry in this slot the daylight art never
    # draws. See the WINDOWS table below for what that buys.
    ("WINDOW_LIT",   0xFF, 0xD6, 0x82),
    ("BROWN",        0x7C, 0x4A, 0x1E),
    ("WOOD_DEEP",    0x5B, 0x36, 0x16),
    ("CYAN",         0x63, 0xDC, 0xD7),
    ("CYAN_LIGHT",   0x79, 0xCE, 0xD6),
]

INK = {
    "#": "OUTLINE",
    "W": "WHITE",
    "C": "LIGHT_GREY",
    "S": "SHADOW_GREY",
    "G": "SHADOW_GRASS",
    "R": "RED",          # roof facet facing up (top)
    "D": "RED_FACET",    # roof facet facing down (bottom)
    "L": "RED_LIGHT",    # lit left facet and ridge highlight
    "K": "RED_FACET",    # shaded right facet (mirrored with L, see below)
    "h": "RED_DARK",     # hip lines: dark red creases, as on the reference
    "T": "TAN",
    "t": "TAN",          # was TAN_DARK, an inner shade on the SHOP_WINDOWS roof
    "*": "WINDOW_LIT",
    "B": "BROWN",
    "b": "WOOD_DEEP",
    "Y": "CYAN",
    "y": "CYAN_LIGHT",
}


def _stamp(body, w_tiles, h_tiles, shadow, dx=3, dy=2, ox=1, oy=1):
    """Place ``body`` at (ox, oy) on a transparent w x h tile canvas and cast
    its shadow: every transparent cell covered by the body shifted (dx, dy)
    becomes ``shadow``. The shadow is clipped at the canvas edge."""
    w, h = 16 * w_tiles, 16 * h_tiles
    grid = [["."] * w for _ in range(h)]
    for y, row in enumerate(body):
        for x, ch in enumerate(row):
            if ch != ".":
                grid[oy + y][ox + x] = ch
    for y, row in enumerate(body):
        for x, ch in enumerate(row):
            sx, sy = ox + x + dx, oy + y + dy
            if ch != "." and sx < w and sy < h and grid[sy][sx] == ".":
                grid[sy][sx] = shadow
    return (w_tiles, h_tiles, ["".join(r) for r in grid])


def _mirror_house(body):
    """Mirror a hip-roof house and swap the lit/shaded side facets so the
    light still comes from the top-left."""
    swap = str.maketrans("LK", "KL")
    return [row.translate(swap) for row in art_dsl.hflip(body)]


# --- HOUSE_RED: 26x12 hip roof, ridge horizontal, white porch on the right
HOUSE_RED_BODY = [
    "#####################.....",
    "##RRRRRRRRRRRRRRRRR##.....",
    "#LhRRRRRRRRRRRRRRRhK#.....",
    "#LLhRRRRRRRRRRRRRhKK######",
    "#LLLhRRRRRRRRRRRhKKK#WWWC#",
    "#LLLLhLLLLLLLLLhKKKK#WWWC#",
    "#LLLLhDDDDDDDDDhKKKK#WWWC#",
    "#LLLhDDDDDDDDDDDhKKK#CCCC#",
    "#LLhDDDDDDDDDDDDDhKK######",
    "#LhDDDDDDDDDDDDDDDhK#.....",
    "##DDDDDDDDDDDDDDDDD##.....",
    "#####################.....",
]

# --- HOUSE_RED_G: 26x26 suburban house, grey porch block with a white step
#     at the bottom-right corner, chimney dot on the top facet
HOUSE_RED_G_BODY = [
    "##########################",
    "##RRRRRRRRRRRRRRRRRRRRRR##",
    "#LhRRRRRRRRRRRRRRRRRRRRhK#",
    "#LLhRRRRRRRRRRRRR#RRRRhKK#",
    "#LLLhRRRRRRRRRRRRRRRRhKKK#",
    "#LLLLhRRRRRRRRRRRRRRhKKKK#",
    "#LLLLLhRRRRRRRRRRRRhKKKKK#",
    "#LLLLLLhRRRRRRRRRRhKKKKKK#",
    "#LLLLLLLhRRRRRRRRhKKKKKKK#",
    "#LLLLLLLLhLLLLLLhKKKKKKKK#",
    "#LLLLLLLLhDDDDDDhKKKKKKKK#",
    "#LLLLLLLhDDDDDDDDhKKKKKKK#",
    "#LLLLLLhDDDDDDDDDDhKKKKKK#",
    "#LLLLLhDDDDDDDDDDDDhKKKKK#",
    "#LLLLhDDDDDDDDDDDDDDhKKKK#",
    "#LLLhDDDDDDDDDDDDDDDDhKKK#",
    "#LLhDDDDDDDDDDDDDDDDDDhKK#",
    "#LhDDDDDDDDDDDDDDDDDDDDhK#",
    "##DDDDDDDDDDDDDDDDDDDDDD##",
    "##########################",
    "...............#SSSSSWWWW#",
    "...............#SSSSSWWWW#",
    "...............#SSSSSWWWW#",
    "...............#SSSSSWWWW#",
    "...............#SSSSSWWWW#",
    "...............###########",
]

# --- HOUSE_BEIGE: 22x28 beach house, brown frame bevelled dark on the
#     right/bottom, light rim, tan deck, framed skylight, door at the bottom
HOUSE_BEIGE_BODY = [
    "######################",
    "#BBBBBBBBBBBBBBBBBBBB#",
    "#BBBBBBBBBBBBBBBBBBbb#",
    "#BBBBBBBBBBBBBBBBBBbb#",
    "#BBBCCCCCCCCCCCCCCBbb#",
    "#BBBCTTTTTTTTTTTTCBbb#",
    "#BBBCTTTTTTTTTTTTCBbb#",
    "#BBBCTTWWWWWWWWTTCBbb#",
    "#BBBCTTWyyyyyyWTTCBbb#",
    "#BBBCTTWyYYYYYWTTCBbb#",
    "#BBBCTTWyYYYYYWTTCBbb#",
    "#BBBCTTWyYYyYYWTTCBbb#",
    "#BBBCTTWyYYYYYWTTCBbb#",
    "#BBBCTTWyYYYYYWTTCBbb#",
    "#BBBCTTWyYyYYYWTTCBbb#",
    "#BBBCTTWyYYYYYWTTCBbb#",
    "#BBBCTTWyYYYYYWTTCBbb#",
    "#BBBCTTWyYYYYYWTTCBbb#",
    "#BBBCTTWWWWWWWWTTCBbb#",
    "#BBBCTTTTTTTTTTTTCBbb#",
    "#BBBCTTTbbbbTTTTTCBbb#",
    "#BBBCTTTbbbbTTTTTCBbb#",
    "#BBBCTTTbbbbTTTTTCBbb#",
    "#BBBCCCCCCCCCCCCCCBbb#",
    "#BBBBBBBBBBBBBBBBBBbb#",
    "#Bbbbbbbbbbbbbbbbbbbb#",
    "#Bbbbbbbbbbbbbbbbbbbb#",
    "######################",
]

# --- SHOP_WINDOWS: 29x24 tan roof inside a white plinth ring, dark rim,
#     two brown-framed glass panels with a diagonal reflection
SHOP_WINDOWS_BODY = [
    "#############################",
    "#WWWWWWWWWWWWWWWWWWWWWWWWWWW#",
    "#W#########################W#",
    "#W#ttttttttttttttttttttttt#W#",
    "#W#tTTTTTTTTTTTTTTTTTTTTTt#W#",
    "#W#tTTTTTTTTTTTTTTTTTTTTTt#W#",
    "#W#tTBBBBBBBBTTTBBBBBBBBTt#W#",
    "#W#tTBYYYYyyBTTTBYYYYyyBTt#W#",
    "#W#tTBYYYYyyBTTTBYYYYyyBTt#W#",
    "#W#tTBYYYyyYBTTTBYYYyyYBTt#W#",
    "#W#tTBYYYyyYBTTTBYYYyyYBTt#W#",
    "#W#tTBYYyyYYBTTTBYYyyYYBTt#W#",
    "#W#tTBYYyyYYBTTTBYYyyYYBTt#W#",
    "#W#tTBYyyYYYBTTTBYyyYYYBTt#W#",
    "#W#tTBYyyYYYBTTTBYyyYYYBTt#W#",
    "#W#tTByyYYYYBTTTByyYYYYBTt#W#",
    "#W#tTByyYYYYBTTTByyYYYYBTt#W#",
    "#W#tTBBBBBBBBTTTBBBBBBBBTt#W#",
    "#W#tTTTTTTTTTTTTTTTTTTTTTt#W#",
    "#W#tTTTTTTTTTTTTTTTTTTTTTt#W#",
    "#W#ttttttttttttttttttttttt#W#",
    "#W#########################W#",
    "#WWWWWWWWWWWWWWWWWWWWWWWWWWW#",
    "#############################",
]

TILES = {}

STAMPS = {
    "HOUSE_RED":    _stamp(HOUSE_RED_BODY, 2, 1, "S"),
    "HOUSE_RED_B":  _stamp(_mirror_house(HOUSE_RED_BODY), 2, 1, "S"),
    "HOUSE_RED_G":  _stamp(HOUSE_RED_G_BODY, 2, 2, "G"),
    "HOUSE_BEIGE":  _stamp(HOUSE_BEIGE_BODY, 2, 2, "G"),
    "SHOP_WINDOWS": _stamp(SHOP_WINDOWS_BODY, 2, 2, "S", dx=2),
}

SHADOWS = {}
DETAILS = {}

FAMILY = {
    "HOUSE_RED":    "pave",
    "HOUSE_RED_B":  "pave",
    "HOUSE_RED_G":  "grass",
    "HOUSE_BEIGE":  "grass",
    "SHOP_WINDOWS": "pave",
}


# --- after dark --------------------------------------------------------------
# Which ink in which object is a WINDOW, and how much of it is lit; see
# art_buildings_a for why a pane is found on the whole object before the cut
# and why the lit pixels go in LIT.
#
# Declared per object and not per palette entry because the entry is not the
# glass: CYAN and CYAN_LIGHT are the shopfront and the house skylight here, but
# the same two names are a swimming pool one slot over.
LIT = "WINDOW_LIT"

WINDOWS = {
    # (ink that is glass, share of panes lit)
    "SHOP_WINDOWS": ("Yy", 0.5),   # two panels, so one of them
    "HOUSE_BEIGE":  ("Yy", 1.0),   # one skylight

}
