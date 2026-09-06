"""Downtown buildings for the top-down city demo (slot BUILDINGS_A).

Six flat-roof buildings seen straight from above, each a multi-tile stamp on a
transparent surround: a 1 px black outline around the footprint, a white plinth
inside it, a darker 1 px inner rim, flat roof colour with roof furniture, and a
grey drop shadow (light from the top-left) on the right and bottom sides. The
shadow is part of the stamp, so no separate SHADOWS tiles exist. Every stamp
keeps 1-2 px of transparent margin on the top/left; the right/bottom margin is
taken by the shadow so neighbouring stamps never touch.
"""

SLOT = "BUILDINGS_A"

PALETTE = [
    ("OUTLINE",        0x1E, 0x1E, 0x1E),
    ("WHITE",          0xDE, 0xDE, 0xDE),
    ("CONCRETE_LIGHT", 0xC9, 0xC7, 0xC5),
    ("CONCRETE",       0xA6, 0xA3, 0xA3),
    ("GREY_DARK",      0x6D, 0x6B, 0x6B),
    ("TAN",            0xA6, 0x72, 0x59),
    ("TAN_DARK",       0x95, 0x67, 0x50),
    ("TAN_LIGHT",      0xB0, 0x83, 0x6D),
    ("PAVE_SHADOW",    0x5C, 0x49, 0x40),
    ("BROWN_DARK",     0x7C, 0x4A, 0x1E),
    ("WOOD_DEEP",      0x5B, 0x36, 0x16),
    ("MAUVE_GREY",     0x72, 0x64, 0x64),
    # A lit window, and the one entry in this slot the daylight art never
    # draws. See the WINDOWS table below for what that buys.
    ("WINDOW_LIT",     0xFF, 0xD6, 0x82),
    ("CYAN",           0x63, 0xDC, 0xD7),
    ("CYAN_LIGHT",     0x79, 0xCE, 0xD6),
]

INK = {
    "o": "OUTLINE",
    "W": "WHITE",
    "w": "CONCRETE_LIGHT",
    "g": "CONCRETE",
    "d": "GREY_DARK",
    "#": "GREY_DARK",     # drop shadow only; remap to PAVE_SHADOW for a darkened-brick look
    "t": "TAN",
    "T": "TAN_DARK",
    "l": "TAN_LIGHT",
    "s": "PAVE_SHADOW",
    "b": "BROWN_DARK",
    "B": "WOOD_DEEP",
    "m": "MAUVE_GREY",
    "p": "CONCRETE",     # was PINK_GREY, a 20 px shading strip on BLOCK_BEIGE
    "*": "WINDOW_LIT",
    "c": "CYAN",
    "C": "CYAN_LIGHT",
}

TILES = {}
SHADOWS = {}
DETAILS = {}

# Tall tan office: raised penthouse block top-left, vertical seam with rivets,
# AC unit, skylight lower-left, white plinth, grey drop shadow right/bottom.
OFFICE_TAN = [
    "................................",
    "..oooooooooooooooooooooo........",
    "..oWWWWWWWWWWWWWWWWWWWWo........",
    "..oWTTTTTTTTTTTTTTTTTTWo####....",
    "..oWTllllllllTtttttttTWo####....",
    "..oWTllllllllTtttttttTWo####....",
    "..oWTllllllllTtttttttTWo####....",
    "..oWTllllllllTtttttttTWo####....",
    "..oWTllllllllTtttttttTWo####....",
    "..oWTllllllllTtttttttTWo####....",
    "..oWTTTTTTTTTTtttttttTWo####....",
    "..oWTttttttttTtttttttTWo####....",
    "..oWTttttttttTtstttttTWo####....",
    "..oWTttwggdttTtttttttTWo####....",
    "..oWTttgggdssTtstttttTWo####....",
    "..oWTttgggdssTtttttttTWo####....",
    "..oWTttddddssTtstttttTWo####....",
    "..oWTtttsssssTtttttttTWo####....",
    "..oWTttttttttTtstttttTWo####....",
    "..oWTttttttttTtttttttTWo####....",
    "..oWTttttttttTtstttttTWo####....",
    "..oWTttttttttTtttttttTWo####....",
    "..oWTttttttttTTTTTTTTTWo####....",
    "..oWTttttttttTtttttttTWo####....",
    "..oWTttttttttTtstttttTWo####....",
    "..oWTttttttttTtttttttTWo####....",
    "..oWTttttttttTtstttttTWo####....",
    "..oWTtTTTTTTtTtttttttTWo####....",
    "..oWTtTcCCcTtTtstttttTWo####....",
    "..oWTtTCCccTtTtttttttTWo####....",
    "..oWTtTCccCTtTtstttttTWo####....",
    "..oWTtTccCCTtTtttttttTWo####....",
    "..oWTtTcCCcTtTtstttttTWo####....",
    "..oWTtTCCccTtTtttttttTWo####....",
    "..oWTtTCccCTtTtstttttTWo####....",
    "..oWTtTccCCTtTtttttttTWo####....",
    "..oWTtTTTTTTtTtstttttTWo####....",
    "..oWTttttttttTtttttttTWo####....",
    "..oWTttttttttTtstttttTWo####....",
    "..oWTttttttttTtttttttTWo####....",
    "..oWTttttttttTtstttttTWo####....",
    "..oWTttttttttTtttttttTWo####....",
    "..oWTTTTTTTTTTTTTTTTTTWo####....",
    "..oWWWWWWWWWWWWWWWWWWWWo####....",
    "..oooooooooooooooooooooo####....",
    ".....#######################....",
    ".....#######################....",
    "................................",
]

# Wide dark-brown hall: hip roof (lit top/left facets, dark right/bottom),
# brick courtyard inset in a white frame at the centre.
HALL_BROWN = [
    "................................................",
    ".ooooooooooooooooooooooooooooooooooooooooooo....",
    ".oWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWo....",
    ".oWBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBWo####",
    ".oWBbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbBBWo####",
    ".oWBbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbBBBWo####",
    ".oWBbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbBBBBWo####",
    ".oWBbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbBBBBBWo####",
    ".oWBbbbbbbWWWWWWWWWWWWWWWWWWWWWWWWWbBBBBBBWo####",
    ".oWBbbbbbbWtttTtttTtttTtttTtttTtttWBBBBBBBWo####",
    ".oWBbbbbbbWtttTtttTtttTtttTtttTtttWBBBBBBBWo####",
    ".oWBbbbbbbWTTTTTTTTTTTTTTTTTTTTTTTWBBBBBBBWo####",
    ".oWBbbbbbbWtTtttTtttTtttTtttTtttTtWBBBBBBBWo####",
    ".oWBbbbbbbWtTtttTtttTtttTtttTtttTtWBBBBBBBWo####",
    ".oWBbbbbbbWTTTTTTTTTTTTTTTTTTTTTTTWBBBBBBBWo####",
    ".oWBbbbbbbWtttTtttTtttTtttTtttTtttWBBBBBBBWo####",
    ".oWBbbbbbbWtttTtttTtttTtttTtttTtttWBBBBBBBWo####",
    ".oWBbbbbbbWTTTTTTTTTTTTTTTTTTTTTTTWBBBBBBBWo####",
    ".oWBbbbbbbWtTtttTtttTtttTtttTtttTtWBBBBBBBWo####",
    ".oWBbbbbbBWtTtttTtttTtttTtttTtttTtWBBBBBBBWo####",
    ".oWBbbbbBBWWWWWWWWWWWWWWWWWWWWWWWWWBBBBBBBWo####",
    ".oWBbbbBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBWo####",
    ".oWBbbBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBWo####",
    ".oWBbBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBWo####",
    ".oWBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBWo####",
    ".oWBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBWo####",
    ".oWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWo####",
    ".ooooooooooooooooooooooooooooooooooooooooooo####",
    "....############################################",
    "....############################################",
    "................................................",
    "................................................",
]

# Mauve-grey block with a double white rim and two tan roof squares.
BLOCK_GREY = [
    "................................",
    "..oooooooooooooooooooooo........",
    "..oWWWWWWWWWWWWWWWWWWWWo........",
    "..oWwwwwwwwwwwwwwwwwwwWo####....",
    "..oWwmmmmmmmmmmmmmmmmwWo####....",
    "..oWwmmmmmmmmmmmmmmmmwWo####....",
    "..oWwmmmmmmmmmmmmmmmmwWo####....",
    "..oWwmmmmmltttttmmmmmwWo####....",
    "..oWwmmmmmtttttTmmmmmwWo####....",
    "..oWwmmmmmtttttTmmmmmwWo####....",
    "..oWwmmmmmtttttTmmmmmwWo####....",
    "..oWwmmmmmtTTTTTmmmmmwWo####....",
    "..oWwmmmmmmmmmmmmmmmmwWo####....",
    "..oWwmmmmmmmmmmmmmmmmwWo####....",
    "..oWwmmmmmmmmmmmmmmmmwWo####....",
    "..oWwmmmmmmmmmmmmmmmmwWo####....",
    "..oWwmmmmmmmmmmmmmmmmwWo####....",
    "..oWwmmmmmltttttmmmmmwWo####....",
    "..oWwmmmmmtttttTmmmmmwWo####....",
    "..oWwmmmmmtttttTmmmmmwWo####....",
    "..oWwmmmmmtttttTmmmmmwWo####....",
    "..oWwmmmmmtTTTTTmmmmmwWo####....",
    "..oWwmmmmmmmmmmmmmmmmwWo####....",
    "..oWwmmmmmmmmmmmmmmmmwWo####....",
    "..oWwmmmmmmmmmmmmmmmmwWo####....",
    "..oWwmmmmmmmmmmmmmmmmwWo####....",
    "..oWwwwwwwwwwwwwwwwwwwWo####....",
    "..oWWWWWWWWWWWWWWWWWWWWo####....",
    "..oooooooooooooooooooooo####....",
    ".....#######################....",
    ".....#######################....",
    "................................",
]

# Tan block: dark wood frame around a pink-grey inset holding a skylight strip
# and a small dark vent.
BLOCK_BEIGE = [
    "................................",
    "..oooooooooooooooooooooo........",
    "..oWWWWWWWWWWWWWWWWWWWWo........",
    "..oWTTTTTTTTTTTTTTTTTTWo####....",
    "..oWTttttttttttttttttTWo####....",
    "..oWTtBBBBBBBBBBBBBBtTWo####....",
    "..oWTtBBBBBBBBBBBBBBtTWo####....",
    "..oWTtBBwwwwwwwwwwBBtTWo####....",
    "..oWTtBBwmmmmmmmmwBBtTWo####....",
    "..oWTtBBwmWWWWWWpwBBtTWo####....",
    "..oWTtBBwmWCccCWpwBBtTWo####....",
    "..oWTtBBwmWccCCWpwBBtTWo####....",
    "..oWTtBBwmWcCCcWpwBBtTWo####....",
    "..oWTtBBwmWCCccWpwBBtTWo####....",
    "..oWTtBBwmWCccCWpwBBtTWo####....",
    "..oWTtBBwmWccCCWpwBBtTWo####....",
    "..oWTtBBwmWcCCcWpwBBtTWo####....",
    "..oWTtBBwmWCCccWpwBBtTWo####....",
    "..oWTtBBwmWCccCWpwBBtTWo####....",
    "..oWTtBBwmWWWWWWpwBBtTWo####....",
    "..oWTtBBwmppoopppwBBtTWo####....",
    "..oWTtBBwmppoodppwBBtTWo####....",
    "..oWTtBBwwwwwwwwwwBBtTWo####....",
    "..oWTtBBBBBBBBBBBBBBtTWo####....",
    "..oWTtBBBBBBBBBBBBBBtTWo####....",
    "..oWTttttttttttttttttTWo####....",
    "..oWTTTTTTTTTTTTTTTTTTWo####....",
    "..oWWWWWWWWWWWWWWWWWWWWo####....",
    "..oooooooooooooooooooooo####....",
    ".....#######################....",
    ".....#######################....",
    "................................",
]

# Tan block: two lighter panels split by seams with rivets and an AC unit.
BLOCK_TAN = [
    "................................",
    "..oooooooooooooooooooooo........",
    "..oWWWWWWWWWWWWWWWWWWWWo........",
    "..oWTTTTTTTTTTTTTTTTTTWo####....",
    "..oWTttttttttTlllllllTWo####....",
    "..oWTttttttttTlllllllTWo####....",
    "..oWTttttttttTlllllllTWo####....",
    "..oWTttwggdttTlllllllTWo####....",
    "..oWTttgggdssTlllllllTWo####....",
    "..oWTttgggdssTlllllllTWo####....",
    "..oWTttddddssTTTTTTTTTWo####....",
    "..oWTtttsssssTtttttttTWo####....",
    "..oWTttttttttTtstttttTWo####....",
    "..oWTttttttttTtttttttTWo####....",
    "..oWTttttttttTtstttttTWo####....",
    "..oWTttttttttTtttttttTWo####....",
    "..oWTttttttttTtstttttTWo####....",
    "..oWTTTTTTTTTTtttttttTWo####....",
    "..oWTllllllllTtstttttTWo####....",
    "..oWTllllllllTtttttttTWo####....",
    "..oWTllllllllTtstttttTWo####....",
    "..oWTllllllllTtttttttTWo####....",
    "..oWTllllllllTtstttttTWo####....",
    "..oWTllllllllTtttttttTWo####....",
    "..oWTllllllllTtstttttTWo####....",
    "..oWTllllllllTtttttttTWo####....",
    "..oWTTTTTTTTTTTTTTTTTTWo####....",
    "..oWWWWWWWWWWWWWWWWWWWWo####....",
    "..oooooooooooooooooooooo####....",
    ".....#######################....",
    ".....#######################....",
    "................................",
]

# Narrow grey kiosk: white rim, rivet dots, recessed tan door slot.
KIOSK = [
    "................",
    ".ooooooooooooo..",
    ".oWWWWWWWWWWWo..",
    ".oWgggggggggWo##",
    ".oWgggggggggWo##",
    ".oWgdggdggdgWo##",
    ".oWgggggggggWo##",
    ".oWgggggggggWo##",
    ".oWgggggggggWo##",
    ".oWgooooooogWo##",
    ".oWgowwwwwogWo##",
    ".oWgowssswogWo##",
    ".oWgowsttwogWo##",
    ".oWgowsttwogWo##",
    ".oWgowsttwogWo##",
    ".oWgowsttwogWo##",
    ".oWgowsttwogWo##",
    ".oWgowsttwogWo##",
    ".oWgowsttwogWo##",
    ".oWgowwwwwogWo##",
    ".oWgooooooogWo##",
    ".oWgggggggggWo##",
    ".oWgggggggggWo##",
    ".oWgggggggggWo##",
    ".oWgdggdggdgWo##",
    ".oWgggggggggWo##",
    ".oWgggggggggWo##",
    ".oWWWWWWWWWWWo##",
    ".ooooooooooooo##",
    "....############",
    "....############",
    "................",
]

STAMPS = {
    "OFFICE_TAN": (2, 3, OFFICE_TAN),
    "HALL_BROWN": (3, 2, HALL_BROWN),
    "BLOCK_GREY": (2, 2, BLOCK_GREY),
    "BLOCK_BEIGE": (2, 2, BLOCK_BEIGE),
    "BLOCK_TAN": (2, 2, BLOCK_TAN),
    "KIOSK": (1, 2, KIOSK),
}


FAMILY = {name: "pave" for name in STAMPS}


# --- after dark --------------------------------------------------------------
# Which ink in which object is a WINDOW, and how much of it is lit. A pane is a
# 4-connected run of that ink, found on the whole object before it is cut into
# tiles: a pane eleven pixels wide straddles a sixteen-pixel tile boundary, and
# a roll taken per tile would put a seam down the middle of it.
#
# Declared per object and not per palette entry because the entry is not the
# glass -- CYAN and CYAN_LIGHT are a skylight on these two roofs and nothing
# else, but that is luck rather than a rule, and the two slots beside this one
# are not so lucky.
#
# The lit pixels are repainted in LIT, a colour the daylight art never uses, so
# lighting them cannot light anything else; panes that lose the roll keep their
# daylight colour and darken with the city, which is what a window with nobody
# in reads as.
LIT = "WINDOW_LIT"

WINDOWS = {
    # (ink that is glass, share of panes lit)
    "OFFICE_TAN":  ("cC", 1.0),   # one skylight, lower-left of the roof
    "BLOCK_BEIGE": ("cC", 1.0),   # the rooftop atrium

}
