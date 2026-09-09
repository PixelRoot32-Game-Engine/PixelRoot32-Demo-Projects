"""Character art source for the midway_clone example.

Every image is an ASCII grid: one character per pixel, one string per row.
The characters are palette symbols, not colors - see PALETTES below for what
each symbol resolves to. '.' is the transparent hole (4bpp pixel value 0),
which the blitter skips entirely.

Edit the art here and re-run generate_assets.py. Nothing downstream is
hand-written, so there is no second copy to keep in sync.

Two palettes exist because the engine runs in dual palette mode: backgrounds
and sprites each get their own 16 slots, so the ocean is not competing with
the aircraft for table space.
"""

# ---------------------------------------------------------------------------
# Palettes
# ---------------------------------------------------------------------------
#
# Each entry: symbol -> (engine Color slot, RGB565, source hex, description)
#
# The RGB565 table is indexed by the engine's Color enum (Color.h:58-75), NOT
# by 4bpp pixel value. Colors stay in their conventional slots so Color::White
# is still white and Color::Red is still red for anything that names a color
# instead of indexing art - the debug overlay and error text do exactly that.
#
# Where a slot holds a value its name does not suggest, the description says
# so. That is deliberate: an 8-bit palette has 16 slots and a game has more
# than 16 needs.

TILEMAP_PALETTE = {
    "k": ("Black",     "#000000", "outlines, deck planking, rock mortar"),
    "n": ("Navy",      "#0000A8", "deep ocean - the darker water between swells"),
    "b": ("Blue",      "#0058F8", "open ocean - the sea's body color"),
    "c": ("Cyan",      "#3CBCFC", "foam and wave crests"),
    "d": ("DarkGreen", "#007800", "island foliage, shaded"),
    "g": ("Green",     "#00A800", "island foliage, lit"),
    # legend_of_clone spells sand '.', but here '.' is reserved for the
    # transparent hole in every grid, so sand is 'a'. A symbol that means
    # "nothing" in one file and "beach" in another is a trap.
    "a": ("Yellow",    "#E8D0A0", "beach sand"),
    "o": ("Orange",    "#A85400", "island rock, lit face"),
    "r": ("DarkRed",   "#703800", "island rock shaded face, and carrier decking"),
}
# There is deliberately no gray here. The player's airframe is #BCBCBC, and a
# gray carrier deck made the aircraft vanish the moment it crossed one. Real
# WWII flight decks were planked in wood, so the fix is also the accurate one.

SPRITE_PALETTE = {
    "k": ("Black",     "#000000", "outline on every aircraft"),
    "w": ("White",     "#FCFCFC", "specular highlight, explosion core"),
    "y": ("Gray",      "#BCBCBC", "player airframe - bare aluminium"),
    "s": ("Cyan",      "#7C7C7C", "player airframe, shaded. NOT cyan: this slot "
                                  "holds a mid gray because the sprite table has "
                                  "no second gray and the player needs two"),
    "c": ("Blue",      "#3CBCFC", "canopy glass"),
    "g": ("Green",     "#00A800", "enemy airframe - IJN green"),
    "d": ("DarkGreen", "#007800", "enemy airframe, shaded"),
    "h": ("LightRed",  "#F83800", "hinomaru roundels, enemy tracer"),
    "f": ("Yellow",    "#FCE4A0", "propeller blur, muzzle flash, player tracer"),
    "o": ("Orange",    "#F87800", "explosion body"),
    "e": ("DarkRed",   "#A81000", "explosion, dissipating"),
}

# Slots deliberately left at their conventional value even though no art uses
# them. Text rendering names these directly, so they must not resolve to black.
RESERVED_SLOTS = {
    "White": "#FCFCFC",
    "Red":   "#F80000",
    "Gray":  "#BCBCBC",
}


# ---------------------------------------------------------------------------
# Tileset - 16x16
# ---------------------------------------------------------------------------
#
# These were 8x8 first, to match the NES original, and 8x8 is the better
# looking choice - a finer grid draws a coastline that does not read as a
# staircase. It was given up for frame rate, and the measurement is worth
# recording because it is not about pixels at all:
#
#   A 240x240 screen is 30x30 tiles at 8px, and 15x15 at 16px. The renderer
#   pays a per-tile cost on every one of them - a call into drawSpriteInternal,
#   an index fetch, bounds arithmetic - and that cost does not shrink when the
#   tile does. Going to 16x16 draws the identical 57,600 pixels through 225
#   blits instead of 900.
#
# So the resolution of the art was traded for a quarter of the per-tile
# overhead. On a 240 MHz target pushing a full frame over 40 MHz SPI, that
# trade is worth making; on native it buys nothing visible.
#
# Slot 0 is reserved and never drawn: drawTileMap skips index 0.
#
# Every tile must tile seamlessly against a copy of itself in all four
# directions - the ocean is a field of thousands of them and any edge artifact
# becomes a visible grid.

TILE_SIZE = 16

TILES = [
    ("EMPTY", False, "Reserved slot 0. Never drawn.", [
        "................",
        "................",
        "................",
        "................",
        "................",
        "................",
        "................",
        "................",
        "................",
        "................",
        "................",
        "................",
        "................",
        "................",
        "................",
        "................",
    ]),

    ("OCEAN", False,
     "Open water. Flat blue with sparse deep-water flecks.\n"
     "     *\n"
     "     * The flecks are single pixels, not clusters. A cluster at this scale\n"
     "     * reads as an object floating on the sea; a lone pixel reads as depth.",
     [
        "bbbbbbbbbbbbbbbb",
        "bbbnbbbbbbbbbbbb",
        "bbbbbbbbbbbnbbbb",
        "bbbbbbbbbbbbbbbb",
        "bbbbbbbnbbbbbbbb",
        "bbbbbbbbbbbbbbnb",
        "bbbbbbbbbbbbbbbb",
        "bnbbbbbbbbbbbbbb",
        "bbbbbbbbbbbbbbbb",
        "bbbbbbbbbnbbbbbb",
        "bbbbbnbbbbbbbbbb",
        "bbbbbbbbbbbbbbbb",
        "bbbbbbbbbbbbbnbb",
        "bbbbbbbbbbbbbbbb",
        "bbbnbbbbbbbbbbbb",
        "bbbbbbbbbbbbbbbb",
     ]),

    ("OCEAN_WAVE", False,
     "Open water carrying a swell. Cyan crests in short horizontal dashes.\n"
     "     *\n"
     "     * Scattered through OCEAN at roughly one in six, this is what keeps a\n"
     "     * scrolling sea from looking like a solid blue sheet sliding past.\n"
     "     * The dashes are horizontal because the scroll is vertical: a crest\n"
     "     * perpendicular to the motion is the one the eye can track.",
     [
        "bbbbbbbbbbbbbbbb",
        "bbcccbbbbbbbbbbb",
        "bbbbbbbbbbcccbbb",
        "bbbbbbbbbbbbbbbb",
        "bbbbbbbbbbbbbbbb",
        "bbbbbcccbbbbbbbb",
        "bbbbbbbbbbbbbbbb",
        "bbbbbbbbbbbbbccc",
        "bbbbbbbbbbbbbbbb",
        "bcccbbbbbbbbbbbb",
        "bbbbbbbbbbbbbbbb",
        "bbbbbbbbcccbbbbb",
        "bbbbbbbbbbbbbbbb",
        "cccbbbbbbbbbbbbb",
        "bbbbbbbbbbbcccbb",
        "bbbbbbbbbbbbbbbb",
     ]),

    ("FOAM", False,
     "Churning white water. Laid as a one-tile ring around every island.\n"
     "     *\n"
     "     * This is the whole shoreline treatment. Directional shore tiles - one\n"
     "     * per edge and one per corner - would cost eight tiles instead of one\n"
     "     * and buy an outline the player never looks at, because in a shmup the\n"
     "     * eye is on the aircraft. The NES made this same trade.",
     [
        "ccbcccbccccbccbc",
        "cccbccccbcccccbc",
        "bccccbccccbccccc",
        "ccccbcccccccbccc",
        "cbcccccbcbccccbc",
        "ccccbccccccbcccc",
        "ccbccbcccccccbcc",
        "cccccbccbccccccb",
        "cbccccccccbccbcc",
        "cccbccbcccccbccc",
        "bcccccccbcccccbc",
        "ccbccbccccbccccc",
        "cccccbcccccccbcc",
        "cbcccccbccbccccc",
        "cccbcccccbcccccb",
        "ccbccccbcccbcccc",
     ]),

    ("SAND", True,
     "Beach. Flat, with brown grit so a wide beach does not read as a hole in\n"
     "     * the screen where the art should be.",
     [
        "aaaaaaaaaaaaaaaa",
        "aaaoaaaaaaaaaaaa",
        "aaaaaaaaaaaoaaaa",
        "aaaaaaaaaaaaaaaa",
        "aaaaaaoaaaaaaaaa",
        "aaaaaaaaaaaaaaoa",
        "aaaaaaaaaaaaaaaa",
        "aoaaaaaaaaaaaaaa",
        "aaaaaaaaaaaaaaaa",
        "aaaaaaaaaoaaaaaa",
        "aaaaoaaaaaaaaaaa",
        "aaaaaaaaaaaaaaaa",
        "aaaaaaaaaaaaaoaa",
        "aaaaaaaaaaaaaaaa",
        "aaaoaaaaaaaaaaaa",
        "aaaaaaaaaaaaaaaa",
     ]),

    ("GRASS", True,
     "Island vegetation. Body green, sparsely shaded.",
     [
        "gggggggggggggggg",
        "ggdgggggggggdggg",
        "gggggggggggggggg",
        "ggggggggdggggggg",
        "gggggggggggggggg",
        "gdgggggggggggggg",
        "gggggggggggdgggg",
        "gggggggggggggggg",
        "ggggggdggggggggg",
        "gggggggggggggggg",
        "ggggggggggggdggg",
        "ggdggggggggggggg",
        "gggggggggggggggg",
        "gggggggdgggggggg",
        "gggggggggggggggg",
        "gggggggggggggggg",
     ]),

    ("ROCK", True,
     "Island rock, laid in a running bond.\n"
     "     *\n"
     "     * The offset between courses is what makes a block of these read as one\n"
     "     * rock mass. A grid-aligned bond reads as a grid of stamps - the exact\n"
     "     * failure legend_of_clone's mountain tile was rebuilt to fix.",
     [
        "oooooookrrrrrrrr",
        "oooooookrrrrrrrr",
        "oooooookrrrrrrrr",
        "oooooookrrrrrrrr",
        "oooooookrrrrrrrr",
        "oooooookrrrrrrrr",
        "oooooookrrrrrrrr",
        "kkkkkkkkkkkkkkkk",
        "rrrrrrrroooooook",
        "rrrrrrrroooooook",
        "rrrrrrrroooooook",
        "rrrrrrrroooooook",
        "rrrrrrrroooooook",
        "rrrrrrrroooooook",
        "rrrrrrrroooooook",
        "kkkkkkkkkkkkkkkk",
     ]),

    ("DECK", True,
     "Carrier flight deck. Wooden planking, seams across the scroll axis.\n"
     "     *\n"
     "     * The seams run across the direction of travel, so a carrier sliding down\n"
     "     * the screen shows its length passing rather than sitting still.\n"
     "     *\n"
     "     * Wood, not the gray steel a carrier suggests: the player's airframe is\n"
     "     * bare aluminium at #BCBCBC, and a gray deck swallowed the aircraft whole\n"
     "     * the moment it crossed one. Wartime flight decks really were planked, so\n"
     "     * the color that reads is also the one that is correct.",
     [
        "rrrrrrrrrrrrrrrr",
        "rrrrrrrrrrrrrrrr",
        "rrrrrrrrrrrrrrrr",
        "kkkkkkkkkkkkkkkk",
        "rrrrrrrrrrrrrrrr",
        "rrrrrrrrrrrrrrrr",
        "rrrrrrrrrrrrrrrr",
        "kkkkkkkkkkkkkkkk",
        "rrrrrrrrrrrrrrrr",
        "rrrrrrrrrrrrrrrr",
        "rrrrrrrrrrrrrrrr",
        "kkkkkkkkkkkkkkkk",
        "rrrrrrrrrrrrrrrr",
        "rrrrrrrrrrrrrrrr",
        "rrrrrrrrrrrrrrrr",
        "kkkkkkkkkkkkkkkk",
     ]),
]


# ---------------------------------------------------------------------------
# Player aircraft - 16x16
# ---------------------------------------------------------------------------
#
# A P-38 Lightning, seen from above, nose north. Twin booms, central crew pod,
# one wing spanning the full sprite, tailplane bridging the booms.
#
# The twin-boom silhouette is the reason this airframe was picked over a
# single-engine fighter: at 16x16 an aircraft has roughly a 12x14 usable
# footprint, and a shape with two hard vertical lines through it stays legible
# against a busy sea. A single fuselage at this size is a blob.
#
# Banking frames narrow the wing rather than shifting the whole sprite. A shift
# reads as the aircraft teleporting sideways; a narrowed wing reads as roll.

PLAYER_SPRITE_SIZE = 16

PLAYER_FRAMES = [
    ("PLAYER_LEVEL_A",
     "Flying level, propeller blades caught at one angle.\n"
     "     *\n"
     "     * The wing shades along its trailing edge, not in patches. A top-down\n"
     "     * wing catches light on the leading edge and loses it toward the back,\n"
     "     * so one dark row at the bottom of the wing is the whole read - scattered\n"
     "     * shadow pixels just look like damage.\n"
     "     *\n"
     "     * The tailplane spans between the booms and no further. Drawing it as wide\n"
     "     * as the main wing is the single fastest way to make a P-38 stop looking\n"
     "     * like one.",
     [
        "..kyk..kk..kyk..",
        "..kfk.kyyk.kfk..",
        "..kyk.kyyk.kyk..",
        "..kyk.kcck.kyk..",
        "..kyk.kcck.kyk..",
        "..kyk.kyyk.kyk..",
        ".kyyyyyyyyyyyyk.",
        "kyyyyyyyyyyyyyyk",
        "kyssyyyyyyyyssyk",
        ".kssssssssssssk.",
        "..kyk.kyyk.kyk..",
        "..kyk..kk..kyk..",
        "..kyk......kyk..",
        "..kyk......kyk..",
        "..kyyyyyyyyyyk..",
        "..kkkkkkkkkkkk..",
     ]),

    ("PLAYER_LEVEL_B",
     "Flying level, propeller half a turn on.\n"
     "     *\n"
     "     * Only the two hub pixels change between A and B. That is the entire\n"
     "     * animation and it is enough - a propeller does not need to be drawn,\n"
     "     * it needs to flicker. Animating the airframe as well would make the\n"
     "     * aircraft look like it is flexing in flight.",
     [
        "..kfk..kk..kfk..",
        "..kyk.kyyk.kyk..",
        "..kyk.kyyk.kyk..",
        "..kyk.kcck.kyk..",
        "..kyk.kcck.kyk..",
        "..kyk.kyyk.kyk..",
        ".kyyyyyyyyyyyyk.",
        "kyyyyyyyyyyyyyyk",
        "kyssyyyyyyyyssyk",
        ".kssssssssssssk.",
        "..kyk.kyyk.kyk..",
        "..kyk..kk..kyk..",
        "..kyk......kyk..",
        "..kyk......kyk..",
        "..kyyyyyyyyyyk..",
        "..kkkkkkkkkkkk..",
     ]),

    ("PLAYER_BANK_LEFT",
     "Rolled left.\n"
     "     *\n"
     "     * The whole airframe shifts one pixel left AND the right wing loses two,\n"
     "     * because roll is foreshortening, not translation. A frame that only\n"
     "     * shifts reads as the aircraft teleporting sideways.",
     [
        ".kyk..kk..kyk...",
        ".kfk.kyyk.kfk...",
        ".kyk.kyyk.kyk...",
        ".kyk.kcck.kyk...",
        ".kyk.kcck.kyk...",
        ".kyk.kyyk.kyk...",
        "kyyyyyyyyyyyk...",
        "kyyyyyyyyyyyyk..",
        "kyssyyyyyyssyk..",
        ".kssssssssssk...",
        ".kyk.kyyk.kyk...",
        ".kyk..kk..kyk...",
        ".kyk......kyk...",
        ".kyk......kyk...",
        ".kyyyyyyyyyyk...",
        ".kkkkkkkkkkkk...",
     ]),

    ("PLAYER_BANK_RIGHT",
     "Rolled right. Mirror of the left bank.",
     [
        "...kyk..kk..kyk.",
        "...kfk.kyyk.kfk.",
        "...kyk.kyyk.kyk.",
        "...kyk.kcck.kyk.",
        "...kyk.kcck.kyk.",
        "...kyk.kyyk.kyk.",
        "...kyyyyyyyyyyyk",
        "..kyyyyyyyyyyyyk",
        "..kyssyyyyyyssyk",
        "...kssssssssssk.",
        "...kyk.kyyk.kyk.",
        "...kyk..kk..kyk.",
        "...kyk......kyk.",
        "...kyk......kyk.",
        "...kyyyyyyyyyyk.",
        "...kkkkkkkkkkkk.",
     ]),
]


# ---------------------------------------------------------------------------
# Enemy aircraft - 16x16
# ---------------------------------------------------------------------------
#
# A single-engine interceptor, nose south - drawn flying at the player rather
# than away, because that is the direction it spends its whole life travelling.
#
# Green against a blue sea, with the hinomaru on the wings in the one saturated
# red on the sprite table. Enemy and player share no color except black, so at
# a glance on a crowded screen the two never trade places.

ENEMY_SPRITE_SIZE = 16

ENEMY_FRAMES = [
    ("ENEMY_FIGHTER_A", "Diving at the player, propeller at one angle.", [
        "......kddk......",
        "....kdggggdk....",
        "....kdggggdk....",
        "......kggk......",
        "......kggk......",
        ".....kgccgk.....",
        ".....kgccgk.....",
        "kdggggggggggggdk",
        "kdgghgggggghggdk",
        ".kggggggggggggk.",
        "......kggk......",
        "......kggk......",
        ".....kggggk.....",
        "......kffk......",
        "....kffffffk....",
        "................",
    ]),

    ("ENEMY_FIGHTER_B", "Diving at the player, propeller half a turn on.", [
        "......kddk......",
        "....kdggggdk....",
        "....kdggggdk....",
        "......kggk......",
        "......kggk......",
        ".....kgccgk.....",
        ".....kgccgk.....",
        "kdggggggggggggdk",
        "kdgghgggggghggdk",
        ".kggggggggggggk.",
        "......kggk......",
        "......kggk......",
        ".....kggggk.....",
        "......kffk......",
        ".....kffffk.....",
        "................",
    ]),
]


# ---------------------------------------------------------------------------
# Projectiles - 8x8
# ---------------------------------------------------------------------------
#
# Both are 8x8 because a bullet is read by its color and its motion, never by
# its shape. Spending 16x16 on one would cost four times the blit for no gain.
#
# Player fire is yellow and vertical; enemy fire is red and round. Shape and
# color both differ, so the distinction survives on a monochrome capture and
# for a colorblind player.

PROJECTILE_SIZE = 8

PROJECTILES = [
    ("BULLET_PLAYER", "Player tracer. A vertical stroke - it reads as speed.", [
        "..kffk..",
        "..kffk..",
        "..kffk..",
        "..kffk..",
        "..kffk..",
        "..kffk..",
        "...kk...",
        "........",
    ]),

    ("BULLET_ENEMY", "Enemy round. Compact and round - it reads as a threat.", [
        "..kkkk..",
        ".khhhhk.",
        ".khhhhk.",
        ".khhhhk.",
        ".khhhhk.",
        "..kkkk..",
        "........",
        "........",
    ]),
]


# ---------------------------------------------------------------------------
# Explosion - 16x16, three frames
# ---------------------------------------------------------------------------
#
# White core, orange body, dark red dissipation. The frames expand and hollow
# out: a fireball's last readable state is a ring, not a fading disc, because
# the center cools and clears before the edge does.

EXPLOSION_SIZE = 16

EXPLOSION_FRAMES = [
    ("EXPLOSION_A", "Ignition. Small, and the only frame with a white core.", [
        "................",
        "................",
        "................",
        "................",
        "................",
        "................",
        ".....ffffff.....",
        "....ffwwwwff....",
        "....ffwwwwff....",
        ".....ffffff.....",
        "................",
        "................",
        "................",
        "................",
        "................",
        "................",
    ]),

    ("EXPLOSION_B", "Full bloom. Orange body, core still hot.", [
        "................",
        "................",
        "................",
        "................",
        "......oooo......",
        "....ooffffoo....",
        "...ooffwwffoo...",
        "..ooffwwwwffoo..",
        "..ooffwwwwffoo..",
        "...ooffwwffoo...",
        "....ooffffoo....",
        "......oooo......",
        "................",
        "................",
        "................",
        "................",
    ]),

    ("EXPLOSION_C", "Dissipating. A broken ring - the center has already cleared.", [
        "................",
        "................",
        "................",
        "....eeeeeeee....",
        "..ee..oooo..ee..",
        ".ee..oo..oo..ee.",
        "ee..oo....oo..ee",
        "e..o........o..e",
        "e..o........o..e",
        "ee..oo....oo..ee",
        ".ee..oo..oo..ee.",
        "..ee..oooo..ee..",
        "....eeeeeeee....",
        "................",
        "................",
        "................",
    ]),
]
