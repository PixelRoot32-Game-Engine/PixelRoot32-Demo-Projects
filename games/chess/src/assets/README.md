# Chess assets

Both headers in this folder are **generated**. Edit
[`tools/generate_pieces.py`](../../tools/generate_pieces.py) and re-run it —
never edit `ChessPalette.h` or `ChessPieces.h` by hand.

```bash
python tools/generate_pieces.py      # run from the demo root, not from here
```

| File | Contents |
|------|----------|
| `ChessPalette.h` | The four `Color`s every piece sprite resolves against. |
| `ChessCustomPalette.h` | A full 16-slot RGB565 table, for `CHESS_CUSTOM_BACKGROUND=1`. |
| `ChessPieces.h` | `Sprite4bpp` tables at both sizes, plus `spriteFor()` / `iconFor()`. |
| `audio/ChessSfx.{h,cpp}` | Hand-written, not generated. See the demo README. |

## The art

Every piece is one **14×14 grid of characters** inside the generator, one
character per palette index:

```
'.'  index 0   transparent — never drawn, the 4bpp blitter skips nibble 0
'o'  index 1   outline
'b'  index 2   body
's'  index 3   accent
```

```python
"Pawn": [
    "..............",
    "..............",
    ".....oooo.....",
    "....obbbbo....",
    ...
```

This is original work for this demo, drawn from scratch against the 16-colour
PR32 palette. Nothing here derives from a third-party pack, which is what lets
the generated headers ship with the repository — see the licensing section of
the [demo README](../../README.md).

`validate()` runs first and refuses to generate anything if a grid is not
exactly 14×14 or uses a character outside the four above. A typo in the art is a
failed run, not a corrupt header.

To see the board without building the firmware:

```bash
python tools/preview_board.py board.png            # PR32, the default
python tools/preview_board.py wood.png Wood        # any entry in PALETTES
```

That script re-reads the same grids and the same palette slots, so the PNG is
what the panel will show. Rendering a palette before flashing it is the cheapest
way to find out that it is unreadable.

## Two sizes, no scaling

Each grid is emitted **twice**:

| Symbol | Size | Used for |
|--------|------|----------|
| `CHESS_<NAME>_4BPP` | 28×28 | the board — nearest-neighbour doubled |
| `CHESS_<NAME>_ICON_4BPP` | 14×14 | the captured-piece tray in the HUD |

The doubling happens in Python rather than at draw time because **the renderer
has no scaled draw for `Sprite4bpp`**. Only the 1bpp `Sprite` and `MultiSprite`
paths take a scale factor, so a 4bpp sprite has to be stored at its final size.
Nearest-neighbour keeps the pixel art exact — every source pixel becomes a clean
2×2 block, with no resampling.

28 px inside a 30 px cell leaves a 1 px margin (`kPieceInset`).

## Packing format

`Sprite4bpp` (`include/graphics/Renderer.h`) reads:

```
row stride       = (width * 4 + 7) / 8 bytes
byte low nibble  = LEFT pixel
byte high nibble = right pixel
nibble value 0   = transparent, never written to the framebuffer
```

Low nibble first is the part that catches people — it is little-endian *within
the byte*, so `0x21` is index 1 on the left and index 2 on the right, not the
other way round. A sprite packed the intuitive way comes out mirrored in pairs.

A 28×28 piece is therefore `28 * 28 / 2 = 392` bytes, and a 14×14 icon is 98.
All of it is `static const`, so it lives in flash, not RAM.

## Two palette traps

Both of these produce art that looks **correct on the SDL2 build and wrong on
the ESP32**, which is the worst way for a bug to behave. Neither is a compile
error.

### `Color::Black` is invisible on hardware

`Color::Black` resolves to RGB565 `0x0000`, and `packRgb565ToTftSprite8(0x0000)`
is `0` — the exact value the 8bpp logical framebuffer treats as **transparent**.
A black pixel draws fine under SDL2 and disappears on the panel.

So black is never used for something that has to be seen:

- Palette index 0 is the transparent sentinel. It is set to `Color::Black`
  precisely *because* it is never drawn — its value only has to be something.
- `Color::Navy` (`#1B1F3B`) is the darkest slot that survives the pack, so it
  plays the part of black: it is the outline on white pieces and the body of
  black ones.

The same trap decides the capture debris in
[`ChessEffects`](../effects/ChessEffects.h): particles interpolate toward their
end colour, so a fade to `Color::Black` fades to *nothing* on hardware. That is
what rules out `ParticlePresets::Explosion` and `::Smoke`, which both end there.

### `Color::Magenta` is not magenta

In the PR32 palette `Color::Magenta` is `#CECECE` — a light grey. It is the
**light board square**, and nothing else in this demo may use it.

It used to be the rim on the black pieces as well, chosen as "a light colour so
the piece reads against a dark square" — which it does. On a *light* square the
rim and the square were the same `#CECECE`, so the black pieces had no visible
edge over half the board. The header compiled, the sprite drew, and the outline
was simply not there.

Read the palette table, not the enum name. Several other names are aliases that
land somewhere unexpected too (`Gold` is `Yellow`, `Brown` is `DarkRed`,
`LightRed` is `#C77DFF`, a light purple).

### The generator refuses colours that collide

Because nothing downstream can catch either trap, `check_contrast()` runs before
a single byte is emitted. Every drawn colour is measured against **both board
squares** and against the other colours in **its own palette**, and anything
closer than an RGB distance of 60 fails the run:

```
colour collision (minimum distance is 60):
  black outline Color::Magenta vs the light square Color::Magenta: distance 0
```

60 sits below the weakest pair the art actually relies on — the White body
against the `#CECECE` light square, which measures about 85 and reads because
the Navy outline frames it — and far above the zero a straight collision scores.

The check runs once **per palette**, over every entry in `PALETTES`. That is not
belt-and-braces: a `Color` is a slot number, so what it looks like depends
entirely on the table loaded, and a rim that reads under PR32 can vanish under a
custom one. Registering a palette there is what makes it safe to ship.

The board squares are declared in the generator as `LIGHT_SQUARE` and
`DARK_SQUARE`. **They must match `kLightSquare` / `kDarkSquare` in
[`src/ChessConstants.h`](../ChessConstants.h)** — that pair of constants is the
one thing here still kept in two places, so change both.

### The custom palette is generated, not written

`ChessCustomPalette.h` holds a full 16-slot RGB565 table: PR32 with exactly the
two board slots re-pointed to wood tones. It is emitted from the `Wood` entry in
`PALETTES` so `check_contrast()` can validate it like any other — a hand-written
table would be a palette nothing verifies.

`rgb565()` **rounds**, and `check_packing()` holds it to the engine's own
`PALETTE_PR32` slot for slot. Truncating with `>> 3` and `>> 2` reproduces only
about half that table, so a custom palette built the obvious way would quietly
shift every slot it was meant to leave alone.

The engine stores the pointer and never copies, so the table must keep static
storage duration for as long as it is selected.

## Palettes

Index 0 is the transparent sentinel in both.

| Index | White pieces | Black pieces |
|-------|--------------|--------------|
| 0 | `Black` — never drawn | `Black` — never drawn |
| 1 | `Navy` — outline | `Orange` `#FF9F1C` — warm rim |
| 2 | `White` — body | `Navy` — body |
| 3 | `Gray` — accent | `Gray` — accent |

The two sides share one set of bitmaps and differ **only** by which palette is
passed at draw time. Adding a piece means adding one grid; it does not mean
drawing it twice.

The dark side is a near-black `Navy` body with a warm `Orange` rim, which is
what separates it from the light grey square *by hue* rather than by brightness
alone — a light rim on a light square is the collision described above, and a
dark rim would disappear into the piece's own body.

These tables are **generated** from `WHITE_PALETTE` / `BLACK_PALETTE` in the
generator, and `tools/preview_board.py` imports the same two dictionaries. There
is one definition, so the preview cannot disagree with the firmware.
