# Isometric Tilemap Export

> **Demonstration project** — provided as an example of what the PixelRoot32
> Game Engine can do. It is not a product: parts may be incomplete,
> experimental, or deliberately simplified to keep one idea in focus.

> **Requires engine 1.10.0 or newer.** This demo uses the projection API
> (`ProjectionSpec`, the projected `drawTileMap` overload,
> `expandProjectedMapBounds`, `cameraRangeFor`) and the per-row span fields of
> `Sprite4bpp` (`rowMinX`/`rowMaxX`), which first ship in `1.10.0`.

A minimal PixelRoot32 game whose entire scene comes out of the Tilemap Editor's
exporter: **isometric projection, 36x29 cells, a 32x16 cell stride, one ground
layer, one palette slot**. The scene lives as an editor project in
[`editor/`](editor/); `src/assets/tilemap/` is what the editor exports from it.
Nothing there is hand-written or hand-patched, and nothing in `src/` corrects
it. To change the map, change the project and export again (see
[*Re-exporting*](#re-exporting)).

The project exists to answer one question — *does the engine paint what the
editor showed?* — and it is also the check that keeps the answer true.

> **Two isometric demos, two questions.** This is the **pipeline** demo — an
> editor export reaching the screen unaltered. For what happens *after* the
> tiles are drawn — actors moving tile by tile, projection-aware depth
> sorting, rooms connected by a `RoomGraph` — see
> [`iso_dungeon`](../iso_dungeon).

Language: C++17  
Engine: `gperez88/PixelRoot32-Game-Engine@^1.10.0`  
Environments: `native`, `esp32dev`  
Category: Graphics

![Isometric Tilemap Export](screenshots/screenshot.png)

## Requirements (build flags)

Set in [`lib/platformio.ini`](lib/platformio.ini)'s `[base]` template, so both
environments inherit them:

| Flag | Value | Why |
|------|-------|-----|
| `PIXELROOT32_ENABLE_PROJECTION` | `1` | The cell-to-screen basis `ISO_PROJECTION` is expressed in. Without it `math/Projection.h` compiles to nothing. |
| `PIXELROOT32_ENABLE_TILEMAP_PROJECTION` | `1` | The projected `drawTileMap` overload — the whole point of the demo. Without it the only overload left is the orthogonal one, which steps by `tileWidth`/`tileHeight` and cannot place a diamond. |
| `PIXELROOT32_ENABLE_GAMEPLAY_GRID_SPACE` | `1` | `gameplay/GridMotion.h`, which the player uses to walk cell to cell. It shares this flag with `GridSpace`, so an isometric game that never declares a `GridSpec` still has to enable it. |
| `PIXELROOT32_ENABLE_4BPP_SPRITES` | `1` | The exported tileset and the player sheets are 4bpp. Building without it is not an error — it is a black screen. |
| `PIXELROOT32_ENABLE_DIRTY_REGIONS` | `1` | Per-tile dirty-skip on the ground layer (see *Performance optimisations*). ESP32 only; the native SDL2 driver ignores the dirty grid. |
| `PIXELROOT32_ENABLE_AUDIO` | `0` | Not used. |
| `PIXELROOT32_ENABLE_PHYSICS` | `0` | Not used — movement is grid motion, not a physics query. |
| `PIXELROOT32_ENABLE_PARTICLES` | `0` | Not used. |
| `PIXELROOT32_ENABLE_UI_SYSTEM` | `0` | Not used — the debug overlay draws through the renderer directly. |
| `PIXELROOT32_ENABLE_2BPP_SPRITES` | `0` | No 2bpp art in this scene. |

## Platforms

| Environment | Display | Audio |
|-------------|---------|-------|
| `native` | SDL2 window, 320x240 | none — `PIXELROOT32_ENABLE_AUDIO=0` |
| `esp32dev` | ST7789 240x240 over SPI — MOSI **23**, SCLK **18**, DC **2**, RST **4**, CS **-1** | none |

The two window sizes differ on purpose: the map is larger than either, so the
view scrolls (see *Scrolling*) rather than showing the whole scene at once.

## What it verifies

Painting the scene is one call:

```cpp
renderer.drawTileMap(ground, 0, 0, LayerType::Dynamic, ISO_PROJECTION);
```

Everything else — geometry, tile indices, tileset, tile count, per-tile foot
anchor, per-row opaque spans and the background palette — is set by the
export's own `init()`. The player's start cell is the scene's player spawn,
exported as `SPAWN_TILE_X`/`SPAWN_TILE_Y`. If any of that stops arriving,
this project stops building or starts drawing wrong, which is the point of
keeping it around.

## Scrolling: the engine's camera, not a local one

The map is larger than the window, so the view follows the player as it walks
the cells. None of that is hand-rolled any more, and none of it touches the
export:

```cpp
// once, in init()
expandProjectedMapBounds(world_, ground, ISO_PROJECTION);

// every frame, in this order
camera_.setViewportSize(viewW, viewH);
const auto range = cameraRangeFor(world_, viewW, viewH);
camera_.setBounds(...); camera_.setVerticalBounds(...);   // before the position
camera_.setPosition(...);                                  // setPosition clamps
camera_.apply(renderer);                                   // → display offset
```

Three things in that flow are easy to get wrong and worth naming:

- **`expandProjectedMapBounds` is what knows how big the drawn map is.** It is
  not `MAP_WIDTH * TILE_WIDTH`: under an isometric basis a step along either
  cell axis moves *both* screen axes, and 32×32 art anchored at foot row 24
  overhangs its 32×16 cell. For this export the answer is 1040×536 px, derived
  rather than written down.
- **Bounds before position.** `Camera2D::setPosition` clamps against the bounds
  the camera already holds, so setting the position first clamps it against the
  previous frame's range — or, on frame one, against `{0, 0}`.
- **`apply()` makes the origin zero.** It writes the negated camera position
  into the renderer's display offset, which every later primitive honours — the
  tilemap above and the player's sprite alike. That is why the `drawTileMap`
  call passes `0, 0`, and why the player takes no camera argument. Passing the
  camera at a call site as well would count it twice.

## What the export carries

```cpp
inline constexpr uint8_t TILE_WIDTH  = 32;   // the CELL stride
inline constexpr uint8_t TILE_HEIGHT = 16;   // not the bitmap size
inline constexpr uint8_t MAP_WIDTH   = 36;
inline constexpr uint8_t MAP_HEIGHT  = 29;

inline constexpr pixelroot32::math::ProjectionSpec ISO_PROJECTION{
    464, 24,
     16,  8,
    -16,  8};

inline constexpr uint8_t TILESET_MAX_TILE_HEIGHT = 32;
inline constexpr uint16_t SPAWN_TILE_X = 4;
inline constexpr uint16_t SPAWN_TILE_Y = 16;

#ifdef PIXELROOT32_ENABLE_4BPP_SPRITES
inline constexpr uint16_t TILESET_TILE_COUNT = 2;
extern const pixelroot32::graphics::Sprite4bpp TILESET_SPRITES[TILESET_TILE_COUNT];
extern const uint8_t TILESET_FOOT_Y[TILESET_TILE_COUNT];
extern pixelroot32::graphics::TileMap4bpp ground;
void init();
#endif
```

Both origin components are derived rather than guessed. `originX = 464` is the
map's own western inset, `rows * cellW / 2 = 29 * 32 / 2`. `originY = 24` is
`max(foot)`, which is what keeps the tallest sprite's top edge from crossing
`y = 0` — and 24 is `derivedFootY(32, 16, Isometric)`, i.e.
`tileHeight - cellHeight / 2`.

The foot table is parallel to the layer's `tiles[]` — `tileFootY` is indexed
by the same tile slot `tiles[]` uses, so the export emits it right alongside
the tileset. Two tiles here: index 0 is the exporter's empty sentinel (zero
extent, no pixel data, foot 0) and index 1 the 32×32 ground tile anchored at
foot row 24. Cells holding 0 are holes in the map; the player treats exactly
those as unwalkable.

The `.cpp` also defines the per-row span tables the descriptors in
`TILESET_SPRITES` point at, and `init()` installs `CUSTOM_PALETTE_DATA` into
the background palette bank (the *Install palette in init()* export option).
The editor additionally writes `isometric_scene_tilemap_palette.h`, a shared
palette header meant for projects with several scenes. This game does not
include it, since `init()` already installs the palette, but it is kept so the
folder stays exactly what the editor writes.

## The defect this project found

Built against the first export of this scene, it did not link:

```
undefined reference to `isometric_scene::main_scene::TILESET_FOOT_Y'
```

Three separate gaps in the Tool Suite's `cpp_code_generator.cpp`, since fixed:

1. **`init()` never assigned `tileFootY`** — on either palette path. That one
   was silent: it compiled, it linked, it rendered, and every tile anchored at
   its top-left corner instead of its foot, so the whole map drew a
   foot-height too low.
2. **Multi-palette exports never defined the table.** The definition was
   emitted from the single-palette tileset helper, while the header declared
   the `extern` unconditionally.
3. **The table was sized from the raw tilesheet, not the deduped pool.** Dedup
   collapses identical tiles, so the two arrays have different lengths and a
   different order, and `footYFor()` bounds-checks against the pool's
   `tileCount`.

The exporter has since also learned to size each tile from its own bitmap
(so 32x40 walls stay 32x40 next to 32x16 floors, as in
[`iso_dungeon`](https://docs.pixelroot32.org/examples/graphics/iso_dungeon/)),
to emit `TILESET_SPRITES` as an `extern` the game can name, and to install the
palette itself, which is why this project no longer carries a hand-patched
`extern` for the palette.

## Performance optimisations

Two opt-in engine optimisations for isometric ground layers are enabled here
(see `platformio.ini` and `IsoTilemapExportScene::init`):

- **Per-tile dirty-skip** (`PIXELROOT32_ENABLE_DIRTY_REGIONS=1`). When the
  camera is stationary and the layer is `Dynamic`, the projected tilemap loop
  blits only the tiles whose screen rect intersects a dirty cell — a few tiles
  instead of the ~200 the camera window covers. It takes effect only on an 8bpp
  framebuffer (ESP32/TFT_eSPI); the native SDL2 driver skips the dirty grid
  entirely, so the effect is invisible under `pio run -e native`.
- **Span-limited blit** (`rowMinX`/`rowMaxX`). The 32×32 ground tile is a
  diamond with transparent padding top and bottom, yet a plain blit decodes all
  1024 nibbles. The exporter precomputes each tile's per-row opaque span (the
  same values the engine's `computeSpanTable` would produce) into
  `TILESET_SPAN_MIN_X`/`TILESET_SPAN_MAX_X` and points every descriptor in
  `TILESET_SPRITES` at them. Descriptors, pixel data and spans all stay
  `const`, in flash on ESP32; the game makes no RAM copy and calls no
  `computeSpanTable` at startup.

## Build
```bash
pio run -e native
.pio/build/native/program.exe
```

The window is **320x240** on native and **240x240** on `esp32dev` (`platformio.ini`). The map is wider and taller than
that — see the camera section above — which is the reason the view scrolls
instead of showing the whole scene at once.

PlatformIO fetches the engine itself from the registry version pinned in
`platformio.ini` (`^1.10.0`); no local engine checkout is needed. Native builds also need SDL2 — from
MSYS2/MinGW on Windows, `libsdl2-dev` on Linux, `brew install sdl2` on macOS.
The `platformio.ini` here ships with the Windows/MSYS2 include and library
paths; remove the `-IC:/msys64/...`, `-LC:/msys64/...` and `-mconsole` flags on
the other two.

## Upload (ESP32)

```bash
pio run -e esp32dev --target upload
```

## Project layout
```
platformio.ini                 native and esp32dev environments
lib/platformio.ini             shared build templates
src/main.cpp
src/platforms/                 native (SDL2) and esp32dev entry points
src/IsoTilemapExportScene.*    the projected drawTileMap call and the camera
src/actors/PlayerActor.*       the player actor that walks the exported cells
editor/                        Tilemap Editor project the map is exported from
src/assets/tilemap/            exporter output, untouched
```

## Re-exporting

The map's source of truth is the editor project, not the exported C++:

```
editor/isometric_scene.pr32scene.bin    project: one isometric scene, "Main Scene"
editor/assets/tilesets/iso_ground.png   the 32x32 ground tile it is painted with
```

The scene is 36x29 cells, isometric, on a 32x16 cell; one layer, `Ground`, on
palette slot 0; the projection origin is left at its default (the map's own
inset); the player spawn is cell (4, 16).

To change the map and get it on screen:

1. Open the Tool Suite's **Tilemap Editor** and use **File → Open...** on
   `editor/isometric_scene.pr32scene.bin`. Edit the scene and save it
   (**File → Save**, Ctrl+S).
2. **File → Export...** (Ctrl+E) opens *Export Settings*. Use:
   - **C++ Namespace**: `isometric_scene`. The scene's own namespace
     (`main_scene`) and the file names are derived from it and the scene name.
   - **Color Depth**: leave the auto-detected value (4bpp for this art).
   - **Store data in Flash (ESP32)**: on.
   - **Legacy format (no Flash attribute)**: off.
   - **Install palette in init()**: **Background**. The game relies on it and
     installs only the player's sprite palette itself. *Dual* also works but
     additionally overwrites sprite slot 0; with *None* the map would be drawn
     with whatever background palette happens to be installed.
3. **Export Now**, and pick `src/assets/tilemap/` as the export folder. The
   editor overwrites `isometric_scene_main_scene.h`,
   `isometric_scene_main_scene.cpp` and `isometric_scene_tilemap_palette.h`.
4. Rebuild (`pio run -e native`). A different layout of the same layer needs
   no change in `src/`: the drawn map's size and the camera's range are derived
   from the export at runtime.

The export settings are not stored in the project file (the dialog starts from
its defaults every time), which is why they are listed here. Re-exporting
needs a Tilemap Editor with isometric export (zero-extent slot 0, span tables
and the palette install option); older editors produce a different file.

If it still links and the tiles still sit in their diamonds, the export
contract held.

## License

Source code: [MIT](../../LICENSE).

| Asset | Author | License | Source |
|-------|--------|---------|--------|
| Isometric ground tile: the editor project's sheet in [`editor/assets/tilesets/`](editor/assets/tilesets/), baked into [`src/assets/tilemap/`](src/assets/tilemap/) by the Tilemap Editor exporter | scrabling | CC BY 4.0 | [Pixel Isometric Tiles](https://scrabling.itch.io/pixel-isometric-tiles) |
| Player idle and run sheets, 64x64, in [`src/assets/sprites/`](src/assets/sprites/) | scrabling | CC BY 4.0 | same pack |

Art by [scrabling](https://scrabling.itch.io/), used under
[CC BY 4.0](https://creativecommons.org/licenses/by/4.0/) and re-encoded to the
engine's 4bpp nibble-packed format. The pack ships here as generated content
files of this demo, not as a redistributable copy of the pack: the author's own
terms permit exactly that use and prohibit redistributing the assets on their
own or as part of a collection. If you want the tiles themselves, get them from
the link above.
