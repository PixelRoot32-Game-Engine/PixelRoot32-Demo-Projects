"""Authoring source for every pixel in the legend_of_clone example.

This file plays the part the Tilemap Editor project file plays in a real
project: it is what a human edits. Nothing here is compiled. `generate_assets.py`
reads it and writes the exported C++ under `src/assets/`, which is what ships.

Art is written as characters, one per pixel, because a bush you can see in a
diff is worth more during review than 128 bytes of hex. The exporter collapses
it to the same packed 4bpp bytes the Sprite Compiler would emit, so the runtime
pays nothing for the readability.
"""

# ---------------------------------------------------------------------------
# Colors
# ---------------------------------------------------------------------------
# One character, one RGB565 value, and the engine Color slot it occupies.
#
# There are TWO indices in play and conflating them is the mistake this table
# exists to prevent:
#
#   4bpp PIXEL VALUE  -- what a nibble in the packed art holds. 0 is
#                        transparent: the blitter treats it as a hole and never
#                        reads palette entry 0. So black cannot live here at 0.
#   Color SLOT        -- what the RGB565 table is indexed by, and what
#                        Color::White and friends name.
#
# The exporter emits both arrays: PALETTE_MAPPING turns a pixel value into a
# slot, PALETTE_DATA turns a slot into a color. Keeping every color in its
# CONVENTIONAL slot is what makes Color::White still mean white when the status
# bar draws text, and Color::Red still mean red when something goes wrong.
# Packing the slots tight instead would save nothing and would silently paint
# the status bar black on black.
#
# Sampled to match the NES first-quest overworld and Link's own sprite palette.

COLORS = {
    'k': (0x0000, 'Black',      '#000000  outlines, cave mouth, dungeon floor'),
    'w': (0xFFFF, 'White',      '#FCFCFC  text'),
    'n': (0x0015, 'Navy',       '#0000A8  drop shadow, dungeon wall mortar'),
    'b': (0x3DFF, 'Blue',       '#3CBCFC  dungeon wall block'),
    's': (0xFCC7, 'Cyan',       "#FC9838  Link's skin"),
    'd': (0x03C0, 'DarkGreen',  '#007800  foliage shading'),
    'g': (0x0540, 'Green',      '#00A800  foliage body'),
    'l': (0x8682, 'LightGreen', "#80D010  Link's tunic"),
    '.': (0xEE94, 'Yellow',     '#E8D0A0  sand - the ground everything sits on'),
    'o': (0xAAA0, 'Orange',     '#A85400  mountain rock, lit face'),
    'h': (0xCA61, 'LightRed',   "#C84C0C  Link's brown - outline, hair, boots, belt"),
    'r': (0x71C0, 'DarkRed',    '#703800  mountain rock, shaded face'),
    'y': (0xBDF7, 'Gray',       '#BCBCBC  dungeon stairs, dimmed text'),
}

# Engine Color slots, by name. Mirrors graphics/Color.h.
SLOTS = {
    'Black': 0, 'White': 1, 'Navy': 2, 'Blue': 3, 'Cyan': 4, 'DarkGreen': 5,
    'Green': 6, 'LightGreen': 7, 'Yellow': 8, 'Orange': 9, 'LightRed': 10,
    'Red': 11, 'DarkRed': 12, 'Purple': 13, 'Magenta': 14, 'Gray': 15,
}

# Slots no art references, filled anyway so anything that reaches for a Color by
# name - error text, debug overlays - lands on something visible rather than on
# 0x0000. A palette with holes in it fails by drawing nothing, which reads as a
# rendering bug rather than as a missing color.
RESERVED_SLOTS = {
    'White': (0xFFFF, 'text and error messages'),
    'Red':   (0xF9C0, 'error text - must never resolve to black'),
    'Gray':  (0xBDF7, 'dimmed text'),
    'Purple': (0x6A3F, 'unused'),
    'Magenta': (0xBDDF, 'unused'),
}

# ' ' is transparent everywhere. The 4bpp blitter treats pixel value 0 as a
# hole and never reads palette entry 0, so no character may map to it.
TRANSPARENT = ' '

ART_SIZE = 16

# ---------------------------------------------------------------------------
# Overworld tileset
# ---------------------------------------------------------------------------
# Order is the tile id order. Id 0 is reserved: drawTileMap skips it, so tile
# ids are 1-based and slot 0 must exist and must never be drawn.

EMPTY = [" " * 16] * 16

OVERWORLD_TILES = [
    # (name, solid, doc, rows)
    ('TILE_EMPTY', True,
     'Reserved slot 0. Never drawn - drawTileMap skips index 0.',
     EMPTY),

    ('TILE_GROUND', False,
     'Sand. Flat, like the NES original - the texture comes from what sits on it.',
     ["." * 16] * 16),

    ('TILE_GRASS', False,
     'Walkable grass patch. A flat green block, sparsely flecked so a field of\n'
     'them does not read as one solid slab.',
     [
        "gggggggggggggggg",
        "gdgggggdggggdggg",
        "gggggggggggggggg",
        "gggdggggggggdggg",
        "gggggggggggggggg",
        "ggggggdggggggggd",
        "gggggggggggggggg",
        "gdggggggdggggggg",
        "gggggggggggggggg",
        "ggggdggggggdgggg",
        "gggggggggggggggg",
        "gggggggdgggggggd",
        "gggggggggggggggg",
        "gdgggdgggggggggg",
        "gggggggggggggggg",
        "ggggggggggdggggg",
     ]),

    ('TILE_BUSH', True,
     'The round bush scattered across the overworld. Blocking.\n'
     'The navy bar beneath it is the drop shadow every NES overworld object casts.',
     [
        "................",
        ".....kkkkkk.....",
        "...kkddddddkk...",
        "..kdggggggggdk..",
        ".kdggggggggggdk.",
        ".kdggggggggggdk.",
        "kdggggggggggggdk",
        "kdggggggggggggdk",
        ".kdggggggggggdk.",
        ".kdggggggggggdk.",
        "..kdggggggggdk..",
        "...kkddddddkk...",
        ".....kkkkkk.....",
        "...nnnnnnnnnn...",
        "................",
        "................",
     ]),

    ('TILE_ROCK', True,
     'Mountain face. Two browns in a masonry bond that tiles seamlessly, so a\n'
     'block of these reads as one mass rather than a grid of stamps.',
     [
        "oooooooooooooooo",
        "orrrooorrrooorrr",
        "orrrooorrrooorrr",
        "orrrooorrrooorrr",
        "oooooooooooooooo",
        "rrooorrrooorrroo",
        "rrooorrrooorrroo",
        "rrooorrrooorrroo",
        "oooooooooooooooo",
        "orrrooorrrooorrr",
        "orrrooorrrooorrr",
        "orrrooorrrooorrr",
        "oooooooooooooooo",
        "rrooorrrooorrroo",
        "rrooorrrooorrroo",
        "rrooorrrooorrroo",
     ]),

    ('TILE_CAVE', False,
     'Cave mouth: solid black. WALKABLE - stepping onto it enters the dungeon.\n'
     '\n'
     'Deliberately featureless. The opening is always laid out as a 2x2 block\n'
     'cut into a mountain, so the arch is drawn by the rock *around* it -\n'
     'exactly how the NES does it. A tile with its own arch would repeat four\n'
     'times.',
     ["k" * 16] * 16),

    ('TILE_TREE', True,
     'Forest. One tree fills the whole tile - crown, then trunk.\n'
     '\n'
     'An earlier version packed two 8px trees per tile. At this scale that read\n'
     'as a green field speckled with black rather than as woods: the gaps were\n'
     'the same size as the foliage. One tree per tile puts the black where a\n'
     'forest actually has it, in the wedges between neighbouring crowns.',
     [
        "kkkkkddddddkkkkk",
        "kkkddggggggddkkk",
        "kkdggggggggggdkk",
        "kdggggggggggggdk",
        "dggggggggggggggd",
        "dggggggggggggggd",
        "dggggggggggggggd",
        "dggggggggggggggd",
        "kdggggggggggggdk",
        "kkdggggggggggdkk",
        "kkkddggggggddkkk",
        "kkkkkddddddkkkkk",
        "kkkkkkddddkkkkkk",
        "kkkkkkdggdkkkkkk",
        "kkkkkkdggdkkkkkk",
        "kkkkkkddddkkkkkk",
     ]),
]

# ---------------------------------------------------------------------------
# Dungeon tileset
# ---------------------------------------------------------------------------

DUNGEON_TILES = [
    ('TILE_EMPTY', True,
     'Reserved slot 0. Never drawn - drawTileMap skips index 0.',
     EMPTY),

    ('TILE_FLOOR', False,
     'Floor: flat black.\n'
     '\n'
     'Featureless on purpose. Dungeon floors in the original are black, and the\n'
     'room reads as an interior because of the wall that frames it, not because\n'
     'the floor is textured.',
     ["k" * 16] * 16),

    ('TILE_WALL', True,
     'Wall: large blue blocks in offset courses, so a two-tile-thick wall reads\n'
     'as masonry rather than as a grid of stamps.',
     [
        "bbbbbbbbbbbbbbbb",
        "bnnnnnnnbnnnnnnn",
        "bnnnnnnnbnnnnnnn",
        "bnnnnnnnbnnnnnnn",
        "bnnnnnnnbnnnnnnn",
        "bnnnnnnnbnnnnnnn",
        "bnnnnnnnbnnnnnnn",
        "bnnnnnnnbnnnnnnn",
        "bbbbbbbbbbbbbbbb",
        "nnnbnnnnnnnbnnnn",
        "nnnbnnnnnnnbnnnn",
        "nnnbnnnnnnnbnnnn",
        "nnnbnnnnnnnbnnnn",
        "nnnbnnnnnnnbnnnn",
        "nnnbnnnnnnnbnnnn",
        "nnnbnnnnnnnbnnnn",
     ]),

    ('TILE_STAIRS', False,
     'Stairs out of the dungeon. Grey treads on the black floor. WALKABLE -\n'
     'stepping onto them leaves the dungeon.',
     [
        "kkkkkkkkkkkkkkkk",
        "kkyyyyyyyyyyyykk",
        "kkyyyyyyyyyyyykk",
        "kkkkkkkkkkkkkkkk",
        "kkyyyyyyyyyyyykk",
        "kkyyyyyyyyyyyykk",
        "kkkkkkkkkkkkkkkk",
        "kkyyyyyyyyyyyykk",
        "kkyyyyyyyyyyyykk",
        "kkkkkkkkkkkkkkkk",
        "kkyyyyyyyyyyyykk",
        "kkyyyyyyyyyyyykk",
        "kkkkkkkkkkkkkkkk",
        "kkyyyyyyyyyyyykk",
        "kkyyyyyyyyyyyykk",
        "kkkkkkkkkkkkkkkk",
     ]),
]

# ---------------------------------------------------------------------------
# Player sprites
# ---------------------------------------------------------------------------
# Three colors and a hole: exactly one NES sprite palette, which is the whole
# budget a 1986 cartridge had for a hero. No black - his outline, hair, boots
# and belt are all the same brown. Outlining a sprite in black is the fastest
# way to make it stop reading as an 8-bit character.
#
# He carries NOTHING in either hand. That is a deliberate design constraint and
# it pays for itself twice: it keeps the character clear of the obvious
# inspiration, and it makes both vertical directions mirror pairs, because a
# flip has no held object to teleport from one hand to the other. Four bitmaps
# cover four directions and a two-frame cycle.
#
# Which frames mirror and which do not is MEASURED, never assumed - run
# check_sprites.py. It also guards the failure that has already shipped here
# once: a sprite that is left-right symmetric makes its own mirrored walk frame
# invisible, which reads as a broken timer rather than as broken art.

PLAYER_SPRITES = [
    ('PLAYER_DOWN',
     'Facing south. Frame 1 is this mirrored.\n'
     '\n'
     'The head and torso are symmetric; the arms and legs are not. The mirror\n'
     'swings the far arm forward and the near one back, which is the whole\n'
     'animation. Measured at 44 differing pixels against its own mirror.', [
        "     llllll     ",
        "    llllllll    ",
        "  s lhhhhhhl s  ",
        "  s hhhhhhhh s  ",
        "  sshslsslshss  ",
        "  sshshsshshss  ",
        "  hssssssssssh  ",
        "  hllsshhsslhh  ",
        " hhhllssssllls  ",
        " hhsllllllllls  ",
        " hsssllhhhllh   ",
        " ssshhhhlhhhl   ",
        "  sllllhhhlll   ",
        "    lllllllh    ",
        "    hhh  hhh    ",
        "         hhh    ",
    ]),
    ('PLAYER_SIDE_A', 'Facing east, frame 0. Mirrored for west.', [
        "     llll       ",
        "   lllllhhhh    ",
        " lllsllhhhhhh   ",
        "llllsshhhhhh    ",
        "l llssshssls    ",
        "  lhhsshsshss   ",
        "   hhhssssss    ",
        "    llllssss    ",
        "  hlllllllhhh   ",
        " hhhlssslllhh   ",
        " hhhhssslllhh   ",
        " hhhhsslllh     ",
        "  lhhllhhhh     ",
        " llllllllll     ",
        "    hhhh        ",
        "    hhhhh       ",
    ]),
    ('PLAYER_SIDE_B',
     'Facing east, frame 1. A pixel lower than frame 0 - the walk bob.\n'
     '\n'
     'A real bitmap, not a mirror: mirroring a side-on pose turns him around\n'
     'rather than animating him. Measured at 146 differing pixels against the\n'
     "mirror of frame 0, where the front pair measures 0.", [
        "                ",
        "     llll       ",
        "   lllllhhhh    ",
        " lllsllhhhhhh   ",
        "llllsshhhhhh    ",
        "l llssshssls    ",
        "  lhhsshsshss   ",
        "   hhhssssss    ",
        "    llllssss    ",
        "  lhhllssshh    ",
        "  hhhhhssslh    ",
        " lhhhhhssllh    ",
        " llhhhhlllh     ",
        "hhlllllhhhhl    ",
        "hhhllllllllhh   ",
        " hhh     hhh    ",
    ]),
    ('PLAYER_UP',
     'Facing north. Frame 1 is this mirrored.\n'
     '\n'
     'An early version of this sprite was perfectly symmetric, which made the\n'
     'mirror a visual no-op and the walk cycle invisible - it looked like a bug\n'
     'in the timer rather than in the art. The arms and trailing foot now carry\n'
     '26 differing pixels against the mirror; check_sprites.py reports the\n'
     'count and fails below MIN_ASYMMETRY.', [
        "     llllll     ",
        "    llllllll    ",
        "  s llllllll s  ",
        "  slllllllllls  ",
        "  shllllllllhs  ",
        "  sshhllllhhss  ",
        "   shhhllhhhs   ",
        "   hlhhhhhhlh   ",
        "   hhlllllllhh  ",
        "  shhlllllllhh  ",
        "  shhllllllhh   ",
        "  sslhhhhhhll   ",
        "   llllllllll   ",
        "    hlllllhhh   ",
        "     hh  hhhh   ",
        "          hh    ",
    ]),
]

# ---------------------------------------------------------------------------
# Maps
# ---------------------------------------------------------------------------
# Four screens of 15x11 tiles each, laid out 2x2, for both scenes:
#
#     +----------------+----------------+
#     | 0  north-west  | 1  north-east  |
#     +----------------+----------------+
#     | 2  START       | 3  south-east  |
#     +----------------+----------------+
#
# Room seams have to line up by hand: the open cells on one side of a border
# must face open cells on the other, or the connection is decorative and the
# player walks into a wall. `check_maps.py` enforces that.

ROOM_COLS, ROOM_ROWS = 15, 11
MAP_COLS, MAP_ROWS = ROOM_COLS * 2, ROOM_ROWS * 2

# Overworld: '.' sand  ',' grass  'B' bush  'T' forest  '#' mountain  'C' cave
#
# Hand-authored to read like the start area of the NES first quest - mountains
# closing off the north, forest walling in the south, the cave set into a rock
# face on the start screen. Not a tile-exact rip of the original ROM.
OVERWORLD_MAP_LEGEND = {
    '.': 'TILE_GROUND',
    ',': 'TILE_GRASS',
    'C': 'TILE_CAVE',
    'B': 'TILE_BUSH',
    'T': 'TILE_TREE',
    '#': 'TILE_ROCK',
}

OVERWORLD_MAP = [
    "###############" "###############",  #  0
    "#....,....##..#" "#..,.......,..#",  #  1
    "#..##.....##..#" "#....BBB......#",  #  2
    "#..##.........." "....BBBBB.....#",  #  3  <- seam 0<->1
    "#.....,........" ".....BBB......#",  #  4  <- seam 0<->1
    "#.........,...." "......B.......#",  #  5  <- seam 0<->1
    "#...B.........#" "#....,........#",  #  6
    "#..BBB........#" "#........###..#",  #  7
    "#...B..,......#" "#..,.....###..#",  #  8
    "#.............#" "#........###..#",  #  9
    "####.....######" "######.....####",  # 10  <- seams 0<->2 and 1<->3
    "TTTT.....TTTTTT" "TTTTTT.....TTTT",  # 11  <- seams 0<->2 and 1<->3
    "T....,....TTTTT" "T....,........T",  # 12
    "T..#####..T...T" "T......###....T",  # 13
    "T..##CC#......T" "T.....#####...T",  # 14  <- cave mouth
    "T..##CC#......." "......#####...T",  # 15  <- cave mouth, seam 2<->3
    "T..##..#......." ".......###....T",  # 16  <- seam 2<->3
    "T..,..........T" "T....,........T",  # 17
    "T....B....,...T" "T..B.......B..T",  # 18
    "T...BBB.......T" "T.BBB.....BBB.T",  # 19
    "T....B........T" "T..B..,....B..T",  # 20
    "TTTTTTTTTTTTTTT" "TTTTTTTTTTTTTTT",  # 21
]

# Dungeon: '.' floor  '#' wall  'S' stairs
#
# Every room is a 2-tile-thick wall around an 11x7 interior, which is the NES
# dungeon room proportioned to a 15-wide screen. Doorways are two tiles wide so
# the 16-pixel player walks through without having to be tile-aligned; the
# original gets away with one-tile doors only because it nudges Link onto the
# grid for you.
#
# The stairs sit inside the room rather than in the bottom wall. A walkable cell
# in a border would be an opening the room graph knows nothing about, and the
# map checker rejects those on the grounds that they are usually a forgotten
# connection rather than a deliberate exit.
DUNGEON_MAP_LEGEND = {
    '.': 'TILE_FLOOR',
    'S': 'TILE_STAIRS',
    '#': 'TILE_WALL',
}

DUNGEON_MAP = [
    "###############" "###############",  #  0
    "###############" "###############",  #  1
    "##...........##" "##...........##",  #  2
    "##...........##" "##...........##",  #  3
    "##...........##" "##...........##",  #  4
    "##............." ".............##",  #  5  <- doorway 0<->1
    "##............." ".............##",  #  6  <- doorway 0<->1
    "##...........##" "##...........##",  #  7
    "##...........##" "##...........##",  #  8
    "#######..######" "#######..######",  #  9  <- doorways 0<->2 and 1<->3
    "#######..######" "#######..######",  # 10  <- doorways 0<->2 and 1<->3
    "#######..######" "#######..######",  # 11  <- doorways 0<->2 and 1<->3
    "#######..######" "#######..######",  # 12  <- doorways 0<->2 and 1<->3
    "##...........##" "##...........##",  # 13
    "##...........##" "##...........##",  # 14
    "##...........##" "##...........##",  # 15
    "##............." ".............##",  # 16  <- doorway 2<->3
    "##............." ".............##",  # 17  <- doorway 2<->3
    "##...........##" "##...........##",  # 18  <- player enters here, col 7
    "##.....S.....##" "##...........##",  # 19  <- stairs out
    "###############" "###############",  # 20
    "###############" "###############",  # 21
]
