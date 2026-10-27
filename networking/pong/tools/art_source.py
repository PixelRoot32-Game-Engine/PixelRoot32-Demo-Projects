#!/usr/bin/env python3
"""Original arcade artwork for the networking Pong demo.

Every grid in this file is original work made for this repository and is
released with it under CC0 1.0 (see the demo README's License section). There
are no external image files: the art *is* these strings, one character per
pixel, and `generate_sprites.py` packs them into ``src/assets/``.

This module is the human-editable source of truth. The generator reads it,
validates every grid against the size each sprite declares, and emits the 4bpp
headers. Nothing here imports a third-party package.

Characters
==========  ======  =========================================================
Character   Nibble  Meaning
==========  ======  =========================================================
``B``       2       body - the lit face of the actor
``H``       3       highlight - the specular block on the ball
``R``       1       rim - the shadowed edge, away from the light
``C``       1       cap - the dark end of a paddle, and its back edge
``I``       2       inner band - the ferrule and the inner column of a paddle
``E``       4       leading edge - the paddle's court-facing side
``.``       0       transparent - the 4bpp sentinel, never drawn
==========  ======  =========================================================

The digit is the *nibble* the character packs to, and every sprite binds its
own nibbles to engine colours through the tables in ``PALETTE_INKS``. Two
paddles can therefore share one grid yet resolve to different inks: that is how
the players read apart without a second piece of art.

A nibble is not a palette slot. ``Sprite4bpp::palette`` maps a nibble to a
``Color``, and the active 16-slot table (``kPongPalette``) maps a ``Color`` to
RGB565. ``PALETTE`` below is that second table, so the preview the generator
renders shows the colours the panel will actually show.

Design
------
``BALL``
6x6, the exact rectangle ``game::kBallSize`` draws. The corners are cut so the
square reads as a disc at 6 px, a 2x2 specular block sits on the light side and
the rim ink closes the bottom-right so the ball looks lit rather than flat.

``PADDLE``
6x44, the exact rectangle ``game::kPaddleWidth`` x ``game::kPaddleHeight``
draws. It is built from a bevel rule instead of 44 hand-written rows, because a
rule is what keeps a 44-row bar consistent: one cap row at each end, two
inner-band rows inside each cap, a light leading-edge column on the court side,
a two-pixel inner band behind the body and a cap-ink column at the back. The
two player sprites share this grid and differ only in the colours the nibbles
resolve to; the renderer mirrors player two at draw time so its light edge faces
the centre of the court.
"""

# ---------------------------------------------------------------------------
# Engine colour names, indexed exactly as `pixelroot32::graphics::Color` is.
# Written out rather than imported: the generator must run without the engine
# headers, and this table is what turns a palette entry into the enumerator the
# generated `const Color ...` arrays spell.

COLOR_INDEX = {
    "Black": 0,
    "White": 1,
    "Navy": 2,
    "Blue": 3,
    "Cyan": 4,
    "DarkGreen": 5,
    "Green": 6,
    "LightGreen": 7,
    "Yellow": 8,
    "Orange": 9,
    "LightRed": 10,
    "Red": 11,
    "DarkRed": 12,
    "Purple": 13,
    "Magenta": 14,
    "Gray": 15,
}

# Pixel value that means "leave the framebuffer alone".
TRANSPARENT = "."

# ---------------------------------------------------------------------------
# The 16-slot RGB565 palette, in slot order.
#
# Slot order is the drawing order of the demo: the court ink first (slot 0
# *must* be assigned - `Color::Black` is index 0, and a 0x0000 entry would make
# every `Color::Black` primitive vanish into the 8bpp framebuffer's transparent
# pixel), then the inks the generated sprites blit with, then the HUD and
# effects inks. The spare shades at the end exist because a palette with holes
# resolves an unassigned `Color` to whatever the tail of the table held.

PALETTE = (
    ((0x0A, 0x0C, 0x18),
     "court ink - night playfield and band. Color::Black; never 0x0000"),
    ((0xF4, 0xF8, 0xFF),
     "arcade white - ball specular, paddle edge, scores, overlay text"),
    ((0x14, 0x1C, 0x3A),
     "court navy - the playfield fill; also the P1 paddle cap. Color::Navy"),
    ((0x1D, 0x4F, 0xA8),
     "P1 blue - player one's paddle inner band. Color::Blue"),
    ((0x35, 0xE0, 0xE8),
     "P1 cyan tube - player one's paddle body and HUD tag. Color::Cyan"),
    ((0x0B, 0x6E, 0x5A),
     "deep teal - spare cool shade for the left half of the court"),
    ((0x2F, 0xBF, 0x71),
     "arcade green - dim tick ink for the net diagnostics footer"),
    ((0x8C, 0xF0, 0xB0),
     "pale mint - bright flash for the same footer"),
    ((0xFF, 0xC9, 0x4D),
     "ball gold - the ball body. Color::Yellow"),
    ((0xF0, 0x7C, 0x1E),
     "ball ember - the ball's rim, the particle fade target. Color::Orange"),
    ((0xFF, 0x6B, 0x7A),
     "P2 rose - player two's paddle body and HUD tag. Color::LightRed"),
    ((0xD6, 0x26, 0x3F),
     "P2 red - player two's paddle inner band. Color::Red"),
    ((0x6A, 0x14, 0x24),
     "P2 maroon - player two's paddle cap. Color::DarkRed"),
    ((0x8C, 0x5B, 0xE8),
     "violet - spare cool ink, the net diagnostics accent"),
    ((0xF0, 0x5B, 0xD8),
     "magenta - spare hot ink, the loudest slot in the table"),
    ((0x7C, 0x89, 0xB8),
     "slate - court frame, dashed centre line, dim text. Color::Gray"),
)

# ---------------------------------------------------------------------------
# Nibble to Color tables. Index 0 is the transparent sentinel of a 4bpp sprite:
# the blitter skips it before it resolves a colour, so it is never drawn, but
# the array still has to be well formed.

BALL_INKS = {"R": 1, "B": 2, "H": 3}
PADDLE_INKS = {"C": 1, "I": 2, "B": 3, "E": 4}

BALL_PALETTE_NAME = "kBallInk"
BALL_PALETTE = (
    ("Black", "0  transparent sentinel, never drawn"),
    ("Orange", "1  rim       - ball ember  (palette slot 9)"),
    ("Yellow", "2  body      - ball gold   (palette slot 8)"),
    ("White", "3  specular  - arcade white (palette slot 1)"),
)

PADDLE_P1_PALETTE_NAME = "kPaddleP1Ink"
PADDLE_P1_PALETTE = (
    ("Black", "0  transparent sentinel, never drawn"),
    ("Navy", "1  cap        - court navy (palette slot 2)"),
    ("Blue", "2  inner band - P1 blue    (palette slot 3)"),
    ("Cyan", "3  body       - P1 cyan    (palette slot 4)"),
    ("White", "4  edge       - arcade white (palette slot 1)"),
)

PADDLE_P2_PALETTE_NAME = "kPaddleP2Ink"
PADDLE_P2_PALETTE = (
    ("Black", "0  transparent sentinel, never drawn"),
    ("DarkRed", "1  cap        - P2 maroon (palette slot 12)"),
    ("Red", "2  inner band - P2 red    (palette slot 11)"),
    ("LightRed", "3  body       - P2 rose   (palette slot 10)"),
    ("White", "4  edge       - arcade white (palette slot 1)"),
)

# ---------------------------------------------------------------------------
# Art

# 6x6 ball: cut corners, a 2x2 specular block on the light side and an ember
# rim closing the bottom right. Drawn at `game::kBallSize` - 4bpp has no
# draw-time scaling, so the sprite size *is* the collision square.
BALL = (
    ".BBBB.",
    "BHHBBB",
    "BHHBBB",
    "BBBBBB",
    "BBBBRR",
    ".BRRR.",
)

PADDLE_WIDTH = 6
PADDLE_HEIGHT = 44

# Pixels of cap-ink row at each end, and the band rows just inside them.
PADDLE_CAP = 1
PADDLE_BAND = 2

# Columns, counted from the back of the paddle (the edge away from the court):
# one cap-ink column, two band columns, two body columns, one light edge.
PADDLE_BACK = 1
PADDLE_BAND_COLUMNS = 2


def _paddle():
    """Builds the 6x44 bevelled bar every paddle sprite is packed from.

    The bar is described once, as a rule, instead of as 44 hand-typed rows:
    a rule cannot develop a ragged bevel when somebody edits row 23, and both
    players' sprites are guaranteed to be the same shape.

    The horizontal bands are the ferrules that give the bar a rounded end; the
    vertical columns are the tube's cross-section, lit from the court side. They
    share the band ink because they are the same bevel: a pixel on the inner
    ring of the cross-section catches the same amount of light as a pixel on the
    inner ring of the end.
    """
    rows = []
    for y in range(PADDLE_HEIGHT):
        row = []
        capped = y < PADDLE_CAP or y >= PADDLE_HEIGHT - PADDLE_CAP
        banded = (
            y < PADDLE_CAP + PADDLE_BAND
            or y >= PADDLE_HEIGHT - PADDLE_CAP - PADDLE_BAND
        )
        for x in range(PADDLE_WIDTH):
            if capped:
                row.append("C")
            elif banded:
                row.append("I")
            elif x == PADDLE_WIDTH - 1:
                row.append("E")
            elif x >= PADDLE_WIDTH - 1 - 2:
                row.append("B")
            elif x >= PADDLE_BACK:
                row.append("I")
            else:
                row.append("C")
        rows.append("".join(row))
    return tuple(rows)


PADDLE = _paddle()

# ---------------------------------------------------------------------------
# The sprite manifest. The generator walks this in order, so header order and
# the preview sheet both follow it.

SPRITES = (
    {
        "name": "kPongBall",
        "rows": BALL,
        "size": (6, 6),
        "inks": BALL_INKS,
        "palette": BALL_PALETTE_NAME,
        "colors": BALL_PALETTE,
        "doc": (
            "The ball: 6x6, the exact square `game::kBallSize` draws. Gold body,\n"
            " * an ember rim on the shadow side and a 2x2 specular block lit from\n"
            " * the top left, which is the same light direction both paddles use."
        ),
    },
    {
        "name": "kPongPaddle",
        "rows": PADDLE,
        "size": (PADDLE_WIDTH, PADDLE_HEIGHT),
        "inks": PADDLE_INKS,
        "palette": PADDLE_P1_PALETTE_NAME,
        "colors": PADDLE_P1_PALETTE,
        "doc": (
            "Player one's paddle: the shared bevelled bar in cyan. Draw it\n"
            " * unmirrored - its light leading edge is the right-hand column, the\n"
            " * side that faces the centre of the court."
        ),
    },
    {
        "name": "kPongPaddleP2",
        "rows": PADDLE,
        "size": (PADDLE_WIDTH, PADDLE_HEIGHT),
        "inks": PADDLE_INKS,
        "palette": PADDLE_P2_PALETTE_NAME,
        "colors": PADDLE_P2_PALETTE,
        "doc": (
            "Player two's paddle: the same bevelled bar in rose. Draw it with\n"
            " * `flipX`, which puts its light leading edge on the left, facing the\n"
            " * centre of the court. Same shape, different ink slots: that is what\n"
            " * lets the two players read apart at a glance."
        ),
    },
)
