# Minesweeper sprite pipeline

`src/assets/{GraphicsPalette,TileSprites,HudSprites}.h` are **generated**. Edit
the character grids in [`art_source.py`](art_source.py) and re-run the
generator — never edit the headers by hand.

```bash
python tools/generate_sprites.py                       # regenerate the headers
python tools/generate_sprites.py --check               # fail if a header is stale
python tools/generate_sprites.py --preview tools/preview_contact.png
```

Run those from `games/minesweeper`.

## What the generator does

| Step | Detail |
|------|--------|
| Reads | the ASCII grids in `art_source.py` (`L`/`m`/`D`/`K` inks, `.` transparent) |
| Packs | 4bpp, row stride `ceil(width/2)`, **low nibble = left pixel**, value 0 = transparent |
| Sizes | board cells and the cursor at 16, 12 and 8 px; smileys at 16 px; digits, letters and 9-slice pieces at 8 px |
| Palette | a full 16-slot RGB565 table plus the 5-entry nibble-to-`Color` table every sprite carries |
| Writes | the three headers in `src/assets/` |

A `Sprite4bpp` cannot be scaled at draw time, so every difficulty has its art
emitted at its own cell size: EASY 16 px, MEDIUM 12 px, EXPERT 8 px. The small
sizes are endpoint-aligned nearest-neighbour resamplings of the 16 px cells.

The EXPERT opened-number cells are the one exception: they compose the 8x8 HUD
digit glyphs on an opened cell instead of downsampling the 16 px digit tiles,
because a 5x5 downsample of a 7x7 digit is unreadable. At 8 px that opened cell
is a plain mid-ink fill — the 16 px tile's 1 px dark seam would eat the glyph's
first row and column — and the glyph is recoloured to the dark ink.

The two cursor frames are not resampled either. `art_source.py` draws them
flush with the cell border, which is exactly where a covered tile's light
top/left bevel lives, so the light dashes vanished into that side of the tile.
The generator rebuilds them inset by one bevel (2 px at 16 and 12, 1 px at 8) so
they sit on the mid body. At 16 px the 2-on/2-off rhythm is reproduced exactly
from the frame's corner phases; below that the perimeter only fits two or three
dashes per side and reads as scattered pixels, so 12 and 8 px get a solid 1 px
outline and the two phases become identical (the blink is lost on those two
boards).

`--check` re-renders every header in memory and exits non-zero with a diff when
the file on disk differs. It writes nothing, so it is safe in CI. `--preview`
renders every emitted sprite into one PNG at 4x, with transparent pixels shown
magenta so a hole in the art cannot hide.

## Where the art comes from

`art_source.py` is the original source: every grid in it was drawn for this
demo under CC0 1.0 (see the demo README's License section). There is no
reference image to fetch and nothing to trace — the bevel tiles are built from a
short rule at the top of the file, and the flag, mine, faces and pixel fonts are
`#` masks beside it. `tools/__pycache__/` and `tools/preview*.png` are the only
ignored paths here.

## Two traps the generator owns

- `Color::Black` is RGB565 `0x0000`, which packs to 0 in the 8bpp framebuffer
  and disappears. No nibble is bound to it: the black ink rides `Color::Navy`,
  and `kMinesweeperPalette` assigns every one of the 16 slots so existing
  primitives stay readable.
- Palette pointers are stored, never copied, so the generated arrays have
  static storage duration.
