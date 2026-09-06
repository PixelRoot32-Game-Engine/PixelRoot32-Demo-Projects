"""Downtown buildings, set B: bar, blue shop, the shop you can walk into,
glass office, police station and a teal suburban house. Every entry is a multi-tile stamp for the Items
layer: the whole building on a transparent surround with a 1 px outline,
its plinth and drop shadow drawn inside the stamp, and none of the ground.

Light comes from the top-left; drop shadows are offset (+3, +2) and drawn
on the right and bottom sides. Buildings on pavement use the dark grey
shadow, the house sits on grass and uses the grass shadow.
"""
from __future__ import annotations

SLOT = "BUILDINGS_B"

PALETTE = [
    ("OUTLINE",      0x1E, 0x1E, 0x1E),
    ("WHITE",        0xDE, 0xDE, 0xDE),
    ("CONCRETE",     0xA6, 0xA3, 0xA3),
    ("GREY_DARK",    0x6D, 0x6B, 0x6B),
    ("NAVY",         0x0B, 0x22, 0x45),
    ("NAVY_2",       0x11, 0x33, 0x6A),
    ("BLUE_GLASS",   0x51, 0x68, 0xE9),
    ("BLUE_BRIGHT",  0x25, 0x44, 0xE3),
    ("BLUE_DARK",    0x26, 0x5C, 0xA0),
    ("AMBER",        0xD7, 0x91, 0x29),
    ("OLIVE",        0x85, 0x85, 0x2C),
    # A lit window, and the one entry in this slot the daylight art never
    # draws. See the WINDOWS table below for what that buys.
    ("WINDOW_LIT",   0xFF, 0xD6, 0x82),
    ("TEAL",         0x23, 0x9C, 0x98),
    ("TEAL_DEEP",    0x10, 0x98, 0x80),
    ("GRASS_SHADOW", 0x2E, 0x59, 0x28),
]

INK = {
    "#": "OUTLINE",
    "W": "WHITE",
    "c": "CONCRETE",
    "d": "GREY_DARK",
    "n": "NAVY",
    "N": "NAVY_2",
    "g": "BLUE_GLASS",
    "b": "BLUE_BRIGHT",
    "B": "BLUE_DARK",
    "a": "AMBER",
    "o": "OLIVE",
    "p": "TEAL_DEEP",    # was PALM_GREEN, the two planter tops on the bar roof
    "*": "WINDOW_LIT",
    "t": "TEAL",
    "T": "TEAL_DEEP",
    "s": "GRASS_SHADOW",
}

TILES: dict[str, list[str]] = {}

STAMPS = {
    # Corner bar: navy hipped roof, stacked BAR lettering, a cocktail
    # glass, white plinth, 3 px grey shadow, two outlined planters on the
    # right edge with their own small shadows.
    "BAR": (2, 2, [
        "................................",
        ".WWWWWWWWWWWWWWWWWWWW...........",
        ".W##################W...........",
        ".W#Nnnnnnnnnnnnnnnn#Wddd........",
        ".W#NNnnnnnnggggnnnn#Wddd........",
        ".W#NNNnnnnngnnngnnn#Wddd.######.",
        ".W#NNNNnnnnggggnnnn#Wddd.#pppp#d",
        ".W#NNNNNnnngnnngnnn#Wddd.#pppp#d",
        ".W#NNNNNNnnggggnnnn#Wddd.#pppp#d",
        ".W#NNNNNNNnnnnnnnnn#Wddd.#pppp#d",
        ".W#NNNNNNNnngggnnnn#Wddd.######d",
        ".W#NNNNNNNngnnngnnn#Wddd....dddd",
        ".W#NNNNNNNngggggnnn#Wddd........",
        ".W#NNNNNNNngnnngnnn#Wddd........",
        ".W#NNNNNNNngnnngnnn#Wddd........",
        ".W#NNNNNNNnnnnnnnnn#Wddd........",
        ".W#NNNNNNNnggggnnnn#Wddd........",
        ".W#NNNNNNNngnnngnnn#Wddd........",
        ".W#NNNNNNNnggggnnnn#Wddd........",
        ".W#NNNNNNNngnngnnnn#Wddd.######.",
        ".W#NNNNNNNngnnngnnn#Wddd.#pppp#d",
        ".W#NNNNNNNnnnnnnnnn#Wddd.#pppp#d",
        ".W#NNNNNNngbbbgnnnn#Wddd.#pppp#d",
        ".W#NNNNNnnngbgnnnnn#Wddd.#pppp#d",
        ".W#NNNNnnnnngnnnnnn#Wddd.######d",
        ".W#NNNnnnnnngnnnnnn#Wddd....dddd",
        ".W#NNnnnnnngggnnnnn#Wddd........",
        ".W#Nnnnnnnnnnnnnnnn#Wddd........",
        ".W##################Wddd........",
        ".WWWWWWWWWWWWWWWWWWWWddd........",
        "....dddddddddddddddddddd........",
        "....dddddddddddddddddddd........",
    ]),
    # Blue shop: light / bright / dark facets under a dark inner rim, two
    # amber skylights on the left, white porch alcove on the right side.
    "SHOP_BLUE": (2, 2, [
        "................................",
        ".#####################..........",
        ".#BBBBBBBBBBBBBBBBBBB#..........",
        ".#BgbbbbbbbbbbbbbbbBB#ddd.......",
        ".#BggbbbbbbbbbbbbbBBB#ddd.......",
        ".#BgggbbbbbbbbbbbBBBB#ddd.......",
        ".#BggggbbbbbbbbbBBBBB#ddd.......",
        ".#BgggggbbbbbbbBBBBBB#ddd.......",
        ".#BggggggbbbbbBBBBBBB#ddd.......",
        ".#BgggggggbbbBBBBBBBB#ddd.......",
        ".#BgggggggbbbBBBBBBBB#ddd.......",
        ".#BggaaaagbbbBBBB#####ddd.......",
        ".#BggabbagbbbBBBB#WWW#ddd.......",
        ".#BggabbagbbbBBBB#WWW#ddd.......",
        ".#BggaaaagbbbBBBB#WWW#ddd.......",
        ".#BgggggggbbbBBBB#WWW#ddd.......",
        ".#BgggggggbbbBBBB#WWW#ddd.......",
        ".#BggaaaagbbbBBBB#WWW#ddd.......",
        ".#BggabbagbbbBBBB#WWW#ddd.......",
        ".#BggabbagbbbBBBB#WWW#ddd.......",
        ".#BggaaaagbbbBBBB#####ddd.......",
        ".#BggggggbbbbbBBBBBBB#ddd.......",
        ".#BgggggbbbbbbbBBBBBB#ddd.......",
        ".#BggggbbbbbbbbbBBBBB#ddd.......",
        ".#BgggbbbbbbbbbbbBBBB#ddd.......",
        ".#BggbbbbbbbbbbbbbBBB#ddd.......",
        ".#BgbbbbbbbbbbbbbbbBB#ddd.......",
        ".#BBBBBBBBBBBBBBBBBBB#ddd.......",
        ".#####################ddd.......",
        "....ddddddddddddddddddddd.......",
        "....ddddddddddddddddddddd.......",
        "....ddddddddddddddddddddd.......",
    ]),
    # Glass office: 4x2 panel grid, white frame and mullions, diagonal
    # highlight streaks on every panel, thin 2 px shadow.
    "GLASS_BLUE": (3, 2, [
        "................................................",
        ".###########################################....",
        ".#WWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWW#....",
        ".#WbbbggbbbbWbbbggbbbbWbbbggbbbbWbbbggbbbbW#dd..",
        ".#WbbggbbbbbWbbggbbbbbWbbggbbbbbWbbggbbbbbW#dd..",
        ".#WbggbbbbbgWbggbbbbbgWbggbbbbbgWbggbbbbbgW#dd..",
        ".#WggbbbbbggWggbbbbbggWggbbbbbggWggbbbbbggW#dd..",
        ".#WgbbbbbggbWgbbbbbggbWgbbbbbggbWgbbbbbggbW#dd..",
        ".#WbbbbbggbbWbbbbbggbbWbbbbbggbbWbbbbbggbbW#dd..",
        ".#WbbbbggbbbWbbbbggbbbWbbbbggbbbWbbbbggbbbW#dd..",
        ".#WbbbggbbbbWbbbggbbbbWbbbggbbbbWbbbggbbbbW#dd..",
        ".#WbbggbbbbbWbbggbbbbbWbbggbbbbbWbbggbbbbbW#dd..",
        ".#WbggbbbbbgWbggbbbbbgWbggbbbbbgWbggbbbbbgW#dd..",
        ".#WggbbbbbggWggbbbbbggWggbbbbbggWggbbbbbggW#dd..",
        ".#WWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWW#dd..",
        ".#WbbbggbbbbWbbbggbbbbWbbbggbbbbWbbbggbbbbW#dd..",
        ".#WbbggbbbbbWbbggbbbbbWbbggbbbbbWbbggbbbbbW#dd..",
        ".#WbggbbbbbgWbggbbbbbgWbggbbbbbgWbggbbbbbgW#dd..",
        ".#WggbbbbbggWggbbbbbggWggbbbbbggWggbbbbbggW#dd..",
        ".#WgbbbbbggbWgbbbbbggbWgbbbbbggbWgbbbbbggbW#dd..",
        ".#WbbbbbggbbWbbbbbggbbWbbbbbggbbWbbbbbggbbW#dd..",
        ".#WbbbbggbbbWbbbbggbbbWbbbbggbbbWbbbbggbbbW#dd..",
        ".#WbbbggbbbbWbbbggbbbbWbbbggbbbbWbbbggbbbbW#dd..",
        ".#WbbggbbbbbWbbggbbbbbWbbggbbbbbWbbggbbbbbW#dd..",
        ".#WbggbbbbbgWbggbbbbbgWbggbbbbbgWbggbbbbbgW#dd..",
        ".#WggbbbbbggWggbbbbbggWggbbbbbggWggbbbbbggW#dd..",
        ".#WWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWW#dd..",
        ".###########################################dd..",
        "....dddddddddddddddddddddddddddddddddddddddddd..",
        "....dddddddddddddddddddddddddddddddddddddddddd..",
        "................................................",
        "................................................",
    ]),
    # Police station: concrete roof inside a dark ring and white rim,
    # POLICE in olive 3x5 letters, amber/blue shield badge, U-shaped white
    # entrance notch at the bottom centre (the notch is open ground).
    "POLICE": (3, 2, [
        "................................................",
        ".############################################...",
        ".#WWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWW#...",
        ".#WddddddddddddddddddddddddddddddddddddddddW#ddd",
        ".#WdccccccccccccccccccccccccccccccccccccccdW#ddd",
        ".#WdccccccccccccccccccccccccccccccccccccccdW#ddd",
        ".#WdccccccccooocooococccocooocooocccccccccdW#ddd",
        ".#WdccccccccocococococccococccocccccccccccdW#ddd",
        ".#WdccccccccooococococccococccooocccccccccdW#ddd",
        ".#WdccccccccocccocococccococccocccccccccccdW#ddd",
        ".#WdccccccccocccooocooococooocooocccccccccdW#ddd",
        ".#WdccccccccccccccccccccccccccccccccccccccdW#ddd",
        ".#WdccccccccccccccaaaaaaaaacccccccccccccccdW#ddd",
        ".#WdccccccccccccccabbbbbbbacccccccccccccccdW#ddd",
        ".#WdccccccccccccccabbbWbbbacccccccccccccccdW#ddd",
        ".#WdccccccccccccccabbWWWbbacccccccccccccccdW#ddd",
        ".#WdccccccccccccccabbbWbbbacccccccccccccccdW#ddd",
        ".#WdcccccccccccccccabbbbbaccccccccccccccccdW#ddd",
        ".#WdccccccccccccccccabbbacccccccccccccccccdW#ddd",
        ".#WdcccccccccccccccccabaccccccccccccccccccdW#ddd",
        ".#WdccccccccccccccccccacccccccccccccccccccdW#ddd",
        ".#WdccccccccccccccccccccccccccccccccccccccdW#ddd",
        ".#WdccccccccccWWWWWWWWWWWWWWWWWWccccccccccdW#ddd",
        ".#WdccccccccccW################WccccccccccdW#ddd",
        ".#WdccccccccccW#dddddddddddddd#WccccccccccdW#ddd",
        ".#WdccccccccccW#dddddddddddddd#WccccccccccdW#ddd",
        ".#WdddddddddddW#ddd...........#WdddddddddddW#ddd",
        ".#WWWWWWWWWWWWW#ddd...........#WWWWWWWWWWWWW#ddd",
        ".###############ddd...........###############ddd",
        "....ddddddddddddddd..............ddddddddddddddd",
        "....ddddddddddddddd..............ddddddddddddddd",
        "....ddddddddddddddd..............ddddddddddddddd",
    ]),
    # The one shop you can walk into: SHOP_BLUE with its bottom-left
    # corner cut away into an open shopfront. The entrance is drawn as
    # transparent pixels, not as a door tile, so the per-pixel collision
    # the player already uses lets them stand in it -- the same trick
    # the police station's entrance notch plays, and the reason neither
    # needed a fourth layer. Exactly one is placed per city; see
    # City.single_corner_shop().
    "SHOP_OPEN": (2, 2, [
        "................................",
        ".#####################..........",
        ".#bbbbbbbbbBBBBBBBBBB#..........",
        ".#gbbbbbbbbbBBBBBBBBB#ddd.......",
        ".#ggbbbbbbbbbBBBBBBBB#ddd.......",
        ".#gggbbbbbbbbbBBBBBBB#ddd.......",
        ".#ggggbbbbbbbbbBBBBBB#ddd.......",
        ".#gggggbbbbbbbbbBBBBB#ddd.......",
        ".#ggggggbbbbbbbbbBBBB#ddd.......",
        ".#gggggggbbbbbbbbbBBB#ddd.......",
        ".#WWWWWWWWWWWWWWWWWWW#ddd.......",
        ".#WaaWaaWaaWaaWaaWaaW#ddd.......",
        ".#WaaWaaWaaWaaWaaWaaW#ddd.......",
        ".#WWWWWWWWWWWWWWWWWWW#ddd.......",
        ".#BBBBBBBBBBBBBBBBBBB#ddd.......",
        ".#ggggggggggggggggggg#ddd.......",
        ".#ggWWWWgWWWWgWWWWggg#ddd.......",
        ".#ggWWWWgWWWWgWWWWggg#ddd.......",
        ".#ggWWWWgWWWWgWWWWggg#ddd.......",
        ".#ggggggggggggggggggg#ddd.......",
        ".#BBBBBBBBBBBBBBBBBBB#ddd.......",
        ".#BBBBBBBBBBBBBBBBBBB#ddd.......",
        ".################BBBB#ddd.......",
        ".#dddddddddddddd#BBBB#ddd.......",
        ".#dddddddddddddd#BBBB#ddd.......",
        "................#BBBB#ddd.......",
        "................#BBBB#ddd.......",
        "................#BBBB#ddd.......",
        "................######ddd.......",
        "................ddddddddd.......",
        "................ddddddddd.......",
        "................ddddddddd.......",
    ]),
    # Suburban teal house: hipped roof with a light top / left facet and a
    # dark bottom / right facet, white porch block on the left. Sits on
    # grass, so its shadow is the grass shadow colour.
    "HOUSE_TEAL": (2, 1, [
        "................................",
        ".....#######################....",
        ".....#ttttttttttttttttttttT#....",
        ".....#tttttttttttttttttttTT#sss.",
        ".#####ttttttttttttttttttTTT#sss.",
        ".#WWW#tttttttttttttttttTTTT#sss.",
        ".#WWW#ttttttttttttttttTTTTT#sss.",
        ".#WWW#ttttttTTTTTTTTTTTTTTT#sss.",
        ".#WWW#tttttTTTTTTTTTTTTTTTT#sss.",
        ".#WWW#ttttTTTTTTTTTTTTTTTTT#sss.",
        ".#####tttTTTTTTTTTTTTTTTTTT#sss.",
        "....s#ttTTTTTTTTTTTTTTTTTTT#sss.",
        "....s#tTTTTTTTTTTTTTTTTTTTT#sss.",
        ".....#######################sss.",
        "........sssssssssssssssssssssss.",
        "........sssssssssssssssssssssss.",
    ]),
}

SHADOWS: dict[str, list[str]] = {}

DETAILS: dict[str, list[str]] = {}

FAMILY = {
    "BAR": "pave",
    "SHOP_BLUE": "pave",
    "SHOP_OPEN": "pave",
    "GLASS_BLUE": "pave",
    "POLICE": "pave",
    "HOUSE_TEAL": "grass",
}


# --- after dark --------------------------------------------------------------
# Which ink in which object is a WINDOW, and how much of it is lit; see
# art_buildings_a for why a pane is found on the whole object before the cut
# and why the lit pixels go in LIT.
#
# Declared per object and not per palette entry because the entry is not the
# glass: BLUE_GLASS and BLUE_BRIGHT are the glass tower's panes, and they are
# also the blue gradient of SHOP_BLUE's ROOF and the shield on the police
# station's badge. Lighting the entry would light all three.
LIT = "WINDOW_LIT"

WINDOWS = {
    # (ink that is glass, share of panes lit)
    "GLASS_BLUE": ("gb", 0.6),   # 4x2 panel grid; the point of the roll
    "BAR":        ("g",  1.0),   # roof lettering, so all of it or none

}
