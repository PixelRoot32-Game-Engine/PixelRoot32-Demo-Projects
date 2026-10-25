# Minesweeper

> **Demonstration project** — provided as an example of what the PixelRoot32
> Game Engine can do. It is not a product: parts may be incomplete,
> experimental, or deliberately simplified to keep one idea in focus.

Retro Minesweeper drawn in a fixed 240×240 composition: difficulty menu,
cursor-driven board, first-click safety, flood reveal, flagging, mine counter,
timer, and win/lose screens — with an original Game Boy-style 4bpp sprite line,
plus `Renderer` primitives where a primitive says it
better (panel fills, the wrong-flag cross, overlay text). The single idea it
is about is the **engine-free rules core**: `Minefield`, `GameSession`, input
handling and the rules-to-sprite mapping never include an engine, SDL2 or
Arduino header, so the same logic compiles for the host unit tests
(`pio test -e host_test`) and for all three targets unchanged.

Language: C++17  
Engine: `gperez88/PixelRoot32-Game-Engine@^1.12.0`  
Environments: `native`, `esp32dev_ST7789`, `esp32dev_ILI9341`, `host_test`  
Category: Games

![Minesweeper](screenshots/screenshot.png)

## Requirements (build flags)

Set in [`lib/platformio.ini`](lib/platformio.ini)'s `[base]` template, so
every environment inherits them:

- **`PIXELROOT32_ENABLE_AUDIO=1`** — required. Reveal/flag cues, the loss
  blast and the win jingle go through `AudioEngine` / `MusicPlayer`.
  Building without it is not a compile error — it is silence.
- **`PIXELROOT32_ENABLE_4BPP_SPRITES`** — required, and the easiest of these to
  lose. The engine gates every 4bpp path with `#ifdef`, so without the define
  `Renderer::drawSprite(Sprite4bpp)` is compiled out and the build still
  succeeds — with an empty board. 2bpp is deliberately omitted: that gate is
  `#ifdef` too, so any value (even `-D ...=0`) would enable it.
- **`PIXELROOT32_ENABLE_PHYSICS=0`** — the board is a fixed `Cell[480]`
  array and every rule is an array lookup. There is nothing to simulate.
- **`PIXELROOT32_ENABLE_PARTICLES=0`** — not used.
- **`PIXELROOT32_ENABLE_UI_SYSTEM=1`** — required. The main menu is the
  engine's own UI: a `UIVerticalLayout` of three `UIButton` rows, styled with
  `setButtonStyle()` so the selection is an ink reversal, plus two
  `UILabel` hint lines. The header band, board and result panel stay
  scene-drawn sprites and `Renderer` text; the gate is only for the menu.

Display size is **240×240** on the two square targets and **240×320** on the CYD
panel (see `PHYSICAL_DISPLAY_*`); either way the game itself is drawn in a fixed
240×240 composition, centred on the canvas it is given. A `Sprite4bpp` cannot be
scaled at draw time, so the cell size *is* the emitted tile size rather than the
largest size that fits: **EASY** 9×9 at 16 px (144×144), **MEDIUM** 16×16 at
12 px (192×192), **EXPERT** 30×16 at 8 px (240×128). Boards are centred under a
30 px sprite header — `MINES` with a 3-digit remaining counter, the 16 px state
smiley, `TIME` with a 3-digit clock — and above a footer status line carrying
difficulty, grid, mine count and cursor coordinates.

## Canvas and rotation

The composition is authored in `DESIGN_WIDTH` × `DESIGN_HEIGHT` (240×240, the
screen the sprite line was drawn for) while the canvas may be larger, so the
scene never bends its layout to the panel. `GameConstants.h` derives the real
canvas from the engine's rotation model and centres the design box in it:

| Target | Panel | `DISPLAY_ROTATION` | Canvas | `VIEW_X`/`VIEW_Y` |
|--------|-------|--------------------|--------|-------------------|
| `native` | 240×240 | 0 | 240×240 | 0, 0 |
| `esp32dev_ST7789` | ST7789 240×240 | 0 | 240×240 | 0, 0 |
| `esp32dev_ILI9341` | ILI9341 240×320 | 3 | **320×240** | **40, 0** |

- `QUARTER_TURNS` normalises `DISPLAY_ROTATION`, which arrives either as
  `0..3` or as degrees; `CANVAS_WIDTH`/`CANVAS_HEIGHT` swap the logical size for
  odd quarter turns. `static_assert`s refuse to build a composition that does
  not fit the canvas, so a new panel fails loudly instead of drawing off-panel.
- The CYD is mounted portrait (240×320) and used a quarter turn, so its
  framebuffer is 320×240. The 240×240 composition is centred with
  `Renderer::setDisplayOffset(VIEW_X, VIEW_Y)` — 40 px of margin on each side —
  and the whole canvas is painted mid ink first, so the margins read as paper
  rather than as stale framebuffer.
- **Do not set `LOGICAL_WIDTH`/`LOGICAL_HEIGHT` in the CYD environment.**
  `DisplayConfig` swaps logical width and height for a quarter turn, so those
  values must keep describing the panel *as mounted* (240×320). Hand-setting
  them to 320×240 is swapped back to 240×320: the scene then believes its canvas
  is the portrait shape (canvas 240×320, offset 0,40), draws the composition with
  no side margin and loses its bottom 40 px off the real framebuffer. The env
  leaves them unset so they follow `PHYSICAL_DISPLAY_*`.
- On the two 240×240 targets the canvas *is* the design box, so `VIEW_X` and
  `VIEW_Y` are both 0 and `setDisplayOffset(0, 0)` draws exactly where it drew
  before.

## Platforms

`default_envs = native`. Every environment extends the shared `[base]` flags in
[`lib/platformio.ini`](lib/platformio.ini); the three runnable targets draw the
same 240×240 composition and differ only in the panel and the input backend.

| Environment | Board / display | Canvas the scene draws into | Input | Audio | Platform header |
|-------------|-----------------|-----------------------------|-------|-------|-----------------|
| **`native`** | SDL2 window, 240×240 | 240×240 | Keyboard: arrows, Space (A), Return (B) | `SDL2_AudioBackend` | [`native.h`](src/platforms/native.h) |
| **`esp32dev_ST7789`** | ESP32 DevKit + **ST7789** 240×240, `DISPLAY_ROTATION=0` | 240×240 | 6 GPIO buttons | `ESP32_I2S_AudioBackend` → I2S DAC | [`esp32_dev_ST7789.h`](src/platforms/esp32_dev_ST7789.h) |
| **`esp32dev_ILI9341`** | ESP32-2432S028 "Cheap Yellow Display": **ILI9341** 240×320 panel, `DISPLAY_ROTATION=3` | **320×240** — the 240×240 composition is centred with 40 px side margins | 6 GPIO buttons (the XPT2046 touch panel is left unused) | `ESP32_I2S_AudioBackend` → I2S DAC | [`esp32_dev_ILI9341.h`](src/platforms/esp32_dev_ILI9341.h) |
| **`host_test`** | none | — | — | none | none — host-only Unity suite over the engine-free layers (`src/game/`, `src/input/`, `src/presentation/`) |

Both ESP32 environments use the `espressif32` platform and the Arduino
framework; the pin map (SPI panel bus, I2S audio, D-pad + two action buttons)
lives in each platform header, so edit it there if your wiring differs. The
CYD's XPT2046 touch controller is deliberately left unclaimed: the game is
played with the six buttons and `PIXELROOT32_ENABLE_TOUCH` stays at its default
of 0.

## Controls

| Action | `native` (keyboard) | `esp32dev_ST7789` / `esp32dev_ILI9341` (GPIO) |
|--------|----------------------|----------------------------------------|
| Move cursor | Arrow keys (hold to auto-repeat) | D-pad (hold to auto-repeat) |
| Open cell | Space | Button A |
| Flag / unflag | Return | Button B |
| Change difficulty (menu) | Up/Down or Left/Right | D-pad, then A to start |
| Back to menu (in game) | Space + Return together | A + B together |
| Retry (game over) | Space | Button A |
| Menu (game over) | Return (or Space + Return) | Button B (or A + B) |

The button-to-action mapping is identical on both ESP32 boards; only the GPIO
numbers differ (see the platform headers).

The first opened cell is always safe: mines are placed around it. After a
game ends, input is ignored for 600 ms so the press that ended the game
cannot immediately restart it.

## What it demonstrates

- **Engine-free rules core.** `src/game/Minefield.{h,cpp}` owns cells,
  mine sampling (partial Fisher–Yates over eligible cells, xorshift32 with
  an explicit seed) and the iterative flood over a preallocated 480-slot
  buffer — no recursion, no heap, no per-frame allocation. `src/game/`
  includes nothing but `<cstdint>`, so `host_test` compiles it with no
  display and no SDL2.
- **Deferred first-move safety.** `GameSession` clears the board on `start()`
  but mines it on the first `reveal()`, excluding that cell. Flagging first
  still starts the clock without mining, so the guarantee holds either way.
- **Monotonic clock injection.** The session never reads wall time: the
  scene passes `millis()` in, which freezes the timer on win/lose for free
  and makes every timing test deterministic.
- **Shaped input without an input queue.** `src/input/InputController`
  turns a raw held-button mask into commands: directions auto-repeat after
  350 ms every 120 ms, while reveal/flag/cancel fire on fresh edges only,
  so a held action button cannot open or flag twice. The A+B chord emits
  `Cancel` once per combined press and swallows the individual edges, so
  asking for the menu can never detonate a cell.
- **Sprite-per-cell rendering.** `MinesweeperScene::drawBoard()` loops the
  board and blits one `Sprite4bpp` per cell through `ui/TileCatalog`, the only
  table that maps a tile identity and pixel size to a generated sprite. Content
  precedence lives in the engine-free `presentation/tileFor()` — detonated,
  revealed mine, flag, open count, covered — so the scene only draws. The
  cursor is a dashed frame with a transparent interior, alternating
  its two dash phases every 300 ms; with only four inks in the palette,
  selection is expressed by ink reversal, never by hue. The frame is generated
  inset by one bevel (2 px at 16 and 12, 1 px at 8) so its light dashes land on
  the tile body instead of merging with a covered cell's light top/left edge;
  at 12 and 8 px the perimeter is too short for a readable dash rhythm, so
  those two sizes use a solid 1 px outline instead.
- **Engine UI menu.** The difficulty menu is built from the engine's UI
  widgets: one `UIVerticalLayout` places three `UIButton` rows, and
  `setButtonStyle()` supplies the selected black bar with light text while the
  rest stay dark on the mid panel. Two `UILabel`s carry the hints. Navigation
  still runs through `InputController` — the pad has one owner, because the
  same buttons drive the board and the engine's layout input path is edge-only
  and non-wrapping — so the scene mirrors `difficulty_` onto
  `setSelectedIndex()`. As of engine 1.12.0, `UIButton` stores a non-owning
  `string_view` and has no label setter, so each row's text is a scene buffer
  that is rewritten in place with a fixed-width `BEST` slot (`042` or `---`),
  which `presentation::menuRowWidth()` pins and the host suite tests. The
  dependency is pinned at `^1.12.0` — the release that behaviour was written
  against — so a future engine bump is a deliberate change, not a silent one.
- **A generated art pipeline.** `tools/art_source.py` holds the ASCII grids and `tools/generate_sprites.py` emits `src/assets/*.h`. `--check`
  re-renders every header in memory and fails on a stale one without writing
  anything; `--preview` renders a 4× contact sheet with transparent pixels
  shown magenta. Because 4bpp sprites cannot scale, board art is emitted at 16,
  12 and 8 px by endpoint-aligned nearest-neighbour resampling, and EXPERT
  open-number cells compose the 8 px HUD digits instead of
  downsampling the 16 px tiles. Two traps belong to the generator: engine
  `Color::Black` is RGB565 `0x0000`, which packs to 0 and disappears in the
  ESP32 8bpp framebuffer, so the black ink is bound to `Color::Navy` (the only
  `Color::Black` entry left is the transparent sentinel, which is never drawn);
  and headers are written with platform newlines because this clone checks out
  with `core.autocrlf`, while `--check` compares text rather than bytes.
- **Spec deviations, documented.** The spec's `RevealSolver` lives inside
  `Minefield::reveal()` rather than as its own file. The board, header band and
  overlays are private helpers of `MinesweeperScene` (the tic-tac-toe
  convention) instead of separate classes, while the menu is engine UI widgets,
  sprite lookup lives in `ui/TileCatalog` and the rules-to-sprite mapping in
  `presentation/`. Best times are RAM-only (persistent records are spec P2 and
  out of scope for this demo).

## Project layout

```
src/
├── GameConstants.h        canvas/design box, header/footer geometry, cell sizes, lockout
├── assets/                generated 4bpp headers: palette, tiles, HUD glyphs
├── game/
│   ├── MinefieldConfig.h  difficulties, Cell/BoardConfig, capacity limits
│   ├── Minefield.h/.cpp   board storage, mining, adjacency, flood reveal
│   └── GameSession.h/.cpp playthrough state, deferred placement, game clock
├── input/
│   ├── InputBindings.h    button indices, command bits, repeat timings
│   └── InputController.h/.cpp  edge detection, auto-repeat, A+B chord
├── presentation/
│   └── PresentationMap.h/.cpp  cell→tile, status→smiley, cell size, counters
├── ui/
│   ├── MinesweeperScene.h/.cpp  header band, board, overlays, sound cues
│   ├── ScenePalette.h/.cpp      applies the generated 16-slot palette
│   └── TileCatalog.h/.cpp       the one table from sprite id and size to sprite
├── main.cpp               platform selector
└── platforms/             native.h, esp32_dev_ST7789.h, esp32_dev_ILI9341.h — backend wiring per target
tools/
├── art_source.py          ASCII grids — the editable original
├── generate_sprites.py    the 4bpp generator (--check, --preview)
└── README.md              pipeline notes and the generator's two traps
test/
├── test_minefield/       generation counts (100 seeds × 3 levels),
│                         adjacency vs brute force, corners, flag rules
├── test_reveal/          flood expansion, detonation record, first-move safety
├── test_game_session/    clock freeze, post-game lockout, restart, seeds
├── test_input/           edge-only actions, repeat timing, menu chord
└── test_presentation/    tile/smiley/size/counter/cursor mapping, board fit
```

## Engine documentation

- [Audio API](https://docs.pixelroot32.org/api/audio)
- [Core API](https://docs.pixelroot32.org/api/core)
- [Graphics / Renderer](https://docs.pixelroot32.org/api/graphics)
- [Input API](https://docs.pixelroot32.org/api/input)

## Build

From **`games/minesweeper`** (use the PlatformIO environment, e.g.
`C:\Users\Gperez\.platformio\penv\Scripts\pio.exe` on this machine):

```bash
pio run -e native
pio run -e esp32dev_ST7789
pio run -e esp32dev_ILI9341
```

## Tests

```bash
pio test -e host_test
```

Runs the five host suites — 47 cases, no display and no SDL2: mine counts over
100 seeds per difficulty, adjacency cross-checked against brute force, flood
behaviour, first-move safety over every cell × 5 seeds, clock freeze, restart
invariants, input edge/repeat/chord timing, and the presentation mapping (tile
and smiley selection, the native cell size per difficulty, counter formatting,
the fixed-width menu rows, cursor phase, and the invariant that every board fits
240×240).

## Upload (ESP32)

```bash
pio run -e esp32dev_ST7789 --target upload
pio run -e esp32dev_ILI9341 --target upload
```

## License

Source code: [MIT](../../LICENSE).

| Asset | Author | License | Source |
|-------|--------|---------|--------|
| All audio cues | This repository | CC0 1.0 (public domain) | Original work, synthesised at runtime from `AudioEvent` / `MusicTrack` definitions in `src/` |
| Sprite line and generated headers | This repository | CC0 1.0 (public domain) | Original work, drawn in `tools/art_source.py` and emitted by `tools/generate_sprites.py` into `src/assets/` |

The sprite line is original: the four-tone bevel tiles, the flag, the mine, the
explosion, the four faces, the HUD font and the 9-slice pieces are all drawn in
`tools/art_source.py` for this demo and released with it under CC0 1.0. The
generated headers in `src/assets/` are the shipped artefact; editing the ASCII
source and re-running the generator is the only supported way to change them.
