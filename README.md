# PixelRoot32 Demo Projects

[![build](https://github.com/PixelRoot32-Game-Engine/PixelRoot32-Demo-Projects/actions/workflows/build.yml/badge.svg)](https://github.com/PixelRoot32-Game-Engine/PixelRoot32-Demo-Projects/actions/workflows/build.yml)

Demonstration and template projects for the
[PixelRoot32 Game Engine](https://github.com/PixelRoot32-Game-Engine/PixelRoot32-Game-Engine)
— a lightweight, modular 2D game engine written in C++17 for ESP32
microcontrollers, with a native SDL2 simulation layer for PC.

Every demo is a **self-contained PlatformIO project**. Copy its folder
anywhere, run `pio run -e native`, and you have a working starting point.
No demo depends on another, and none of them needs this repository's
directory layout to build.

> **Demonstration code.** These projects exist to show what the engine can
> do and how its APIs fit together. They are not products: some are
> incomplete, experimental, or deliberately simplified to keep one idea in
> focus.

> **Looking for something smaller?** The engine repository ships
> [**five minimal capability examples**](https://github.com/PixelRoot32-Game-Engine/PixelRoot32-Game-Engine/tree/main/examples) — sprites, camera,
> animated tilemap, physics and a monochrome OLED. Each one isolates a single
> engine feature and stops there. Read those to learn an API; read these to see
> a project built out of several.

## Demos

| Demo | Category | What it shows |
| ---- | -------- | ------------- |
| [`hello_world`](getting_started/hello_world) | Getting started | Minimal scene, text rendering, input polling, and the palette. The smallest complete project — start here. |
| [`first_sprite`](getting_started/first_sprite) | Getting started | Your first sprite: load it, draw it, move it, flip it, step two walk frames. The second thing to read after `hello_world`. Its one idea is the 1bpp format — one bit per pixel, no build flag, no palette — and the bit order the engine's own header documents backwards. |
| [`bomberbot`](games/bomberbot) | Games | A full grid-based action game: interpolated grid movement, deterministic board generation, chain-reaction explosions, enemy AI, power-ups, Y-axis depth sorting, HUD, and audio. All art is original CC0. |
| [`top_down_city`](games/top_down_city) | Games | Stage 1 of a GTA-style open-world sandbox: walk anywhere in a 128×128-tile city with a downtown grid, a park, a plaza, a suburb and a beach. Its one idea is a procedural world that still speaks the Tilemap Editor's export format — three layers, eight per-cell palette slots and `TILE_SOLID` behaviour layers — which is what makes per-pixel collision against trees and lamp posts possible on a map nobody could draw by hand. |
| [`chess`](games/chess) | Games | Two-player touch chess with the full traditional rule set. Its rules core has no engine dependency, so it is verified on the host with perft — the only demo here with a test suite. Capture shake, particle debris, and dual palettes. |
| [`space_invaders`](games/space_invaders) | Games | An unofficial clone of the 1978 arcade fixed shooter, for demonstration only: marching formation, degradable bunkers, swept-circle projectile collision, and music that speeds up with the threat. Its one idea is the fixed memory budget — scene arena, projectile pool, explosion slots, all sized at compile time. |
| [`pong`](games/pong) | Games | Player-versus-CPU Pong. Its one idea is the physics solver's elastic response: a `RigidActor` ball at restitution 1.0 bounces off `StaticActor` walls with no manual reflection, and a built-in validator checks that energy really is conserved. |
| [`brick_breaker`](games/brick_breaker) | Games | Classic Breakout with a level ladder, multi-hit bricks and three lives. Its one idea is keeping physics, particles and audio from leaking into each other: the wall bounce belongs to the physics solver, the paddle english to the collision callback, and the scene tells `MusicPlayer` *what happened* rather than how to sound. |
| [`snake`](games/snake) | Games | Classic Snake on a 24×24 grid. Its one idea is the fixed segment pool: every segment the snake can ever have is built once, so growing and restarting never allocate. |
| [`tic_tac_toe`](games/tic_tac_toe) | Games | Three-in-a-row against a heuristic AI, drawn entirely with `Renderer` primitives. Its one idea is the custom 16-colour palette: one `setCustomPalette()` call repaints the whole game without touching the draw code. |
| [`2048`](games/2048) | Games | The sliding-tile puzzle on a resistive touch panel. Its one idea is that the rules and the AI are engine-free: `Game2048Logic` is a pure board-and-score core, and the expectimax auto-play that drives it never touches a `Scene`. Swipes arrive through `onUnconsumedTouchEvent`, so the game never sees a raw touch. |
| [`midway_clone`](games/midway_clone) | Games | A vertically scrolling shooter with the camera driven every frame. Its one idea is measurement over intuition: it is the counter-example to the tilemap cache, and it shows what a moving camera actually costs `StaticTilemapLayerCache` against the unconditional full-frame SPI push. Pooled bullets, enemies and explosions; physics off. |
| [`legend_of_clone`](games/legend_of_clone) | Games | An 8-bit-style overworld and the dungeon under it, screen by screen. Its one idea is the room transition: two scenes over a shared room-grid base, scrolling between rooms with input locked out, `triggerTransition` between scenes, and tile collision selectable at compile time — whole-tile, per-pixel, or per-pixel with erosion. |
| [`flappy_bird`](games/flappy_bird) | Games | The flap-and-pipes clone on a 1-bit OLED. Its one idea is how little screen a game needs: 72×40 logical pixels on an ESP32-C3, one button, no audio subsystem, and a `gameplay::StateMachine` carrying waiting / playing / game over. The only demo here that builds for `esp32c3`. |
| [`music_sequencer`](audio/music_sequencer) | Audio | The multi-track sequencer under live transport: two static four-track patterns, play/pause/stop, and tempo on the D-pad. Its one idea is what the sequencer needs from you — tracks and presets with static lifetime, durations in beats, and a percussion lane that only sounds when the sub-track declares `WaveType::NOISE`. |
| [`sfx_bank`](audio/sfx_bank) | Audio | Eight effects played through the engine's `playSfxBank` helper. Its one idea is the piece the helper deliberately does not own: the delay scheduler for timed sequence steps. Six fixed slots, no allocation, and the drop counter on screen when the ceiling is hit. |
| [`menu_navigation`](ui/menu_navigation) | UI | A settings menu built from `UIButton` and `UICheckBox` in a `UIVerticalLayout`, driven entirely by the D-pad. Its one idea is that the UI system is not touch-only: `UIVerticalLayout::handleInput()` is already a complete list picker, and `UIManager` — the touch router — never appears. |
| [`hud_widgets`](ui/hud_widgets) | UI | An anchored HUD that holds its pixels while the world scrolls under it. Its one idea is the bypass: fixed-position elements opt out of the camera offset, and because `Scene::draw()` culls entities against the *world* viewport, a HUD must be drawn outside the entity list or it vanishes one screen into the scroll. |
| [`touch_controls`](input/touch_controls) | Input | Every gesture the engine produces — click, double click, long press, the full drag sequence — logged as it fires, with a marker at the live touch point. Its one idea is that the game never sees raw touch: `Engine` owns the dispatcher, and the scene only overrides `onUnconsumedTouchEvent`. |
| [`memory_budget`](performance/memory_budget) | Performance | A `SceneArena` and an `ObjectPool` sized at compile time, with their occupancy drawn on screen. Acquire until the pool refuses — `acquire()` returning `nullptr` is shown, not hidden. Its one idea is that the ceiling should be visible rather than hoped for, which is also why it switches audio, physics, particles and the UI system off: the flag list is the lesson. |
| [`dirty_regions`](performance/dirty_regions) | Performance | Redraw cost made visible: a sparse scene, a toggle that forces a full clear every frame, and the engine's dirty-cell overlay. Its one idea is the precondition — the flag alone does nothing unless the driver hands the renderer an 8bpp framebuffer, so the demo reads that condition at runtime and prints it. Observable on `esp32dev`; on `native` it reports why it is not. |
| [`palette_swap`](graphics/palette_swap) | Graphics | Four palettes over one unchanging scene. Its one idea is that `setCustomPalette()` repaints everything without touching a line of draw code — and the two things that surprise you: the engine keeps your pointer rather than copying the 16 entries, and several `Color` names are aliases onto the same slot. |
| [`depth_sort`](graphics/depth_sort) | Graphics | Actors crossing in a top-down scene, with the Y-sort comparator on a toggle so the wrong answer sits next to the right one. Its one idea is the comparator's contract: precedes means painted first, which means behind. |
| [`particles`](graphics/particles) | Graphics | The five built-in presets walked one at a time, each printing the config behind it. Its one idea is the cheapest subsystem in the engine — ~2 KB of RAM for a 50-particle pool — and the fact that particle life is counted in frames, so an effect tuned on the simulator looks different on hardware. |
| [`room_screen`](gameplay/room_screen) | Gameplay | `RoomGraph<N>` room-to-room navigation built from a Tilemap Editor export, with per-pixel tile collision. |
| [`metroidvania`](gameplay/metroidvania) | Gameplay | A platformer whose subject is not the platforming. Its one idea is the path from a sensor overlap to game logic: `InteractionTracker` turns the per-frame contact set into `onEnter`/`onExit` edges and the engine-owned event bus delivers the result. Larger than the one-idea rule wants — the README says so, and names the split that would fix it. |
| [`state_machine`](gameplay/state_machine) | Gameplay | Idle / Walk / Attack / Hurt over a `const` state table, with every transition on screen. Its one idea is what the machine will not tell you: chained transitions cap at eight, the ninth is discarded, `requestState()` still returns `true`, and a saturating counter is the only witness — so this demo puts that counter on screen. |
| [`object_pool`](gameplay/object_pool) | Gameplay | Projectiles acquired and recycled with no allocation. Its one idea is that live slots are a set, not a prefix: the demo draws the liveness bitmask itself, so a release opens a hole and the next acquire refills it. Releasing during a `nextLive()` walk is safe, and the README explains why from the source. |
| [`iso_tilemap_export`](graphics/iso_tilemap_export) | Graphics | An isometric scene consumed straight out of the Tilemap Editor exporter: projected `drawTileMap`, camera bounds derived from the projected map, per-tile dirty-skip and span-limited blit. |
| [`iso_dungeon`](graphics/iso_dungeon) | Graphics | Three isometric rooms and a hero who walks them tile by tile. Where `iso_tilemap_export` asks whether the engine paints what the editor showed, this one asks what happens next: exact cell-to-cell motion through `GridMotion`, projection-aware depth ordering via `compareByDepthKey`, and rooms wired by a self-validating `RoomGraph`. Its one idea is that the engine has no isometric mode — the view is six integers in a `ProjectionSpec` and every other system stays projection-blind. Carries the same temporary branch pin. |

## Requirements

- [PlatformIO Core](https://docs.platformio.org/en/latest/core/installation/) (`pip install platformio`)
- **Native builds:** SDL2 development libraries
  - Linux: `sudo apt install libsdl2-dev`
  - macOS: `brew install sdl2`
  - Windows: [MSYS2/MinGW](https://www.msys2.org/) with `mingw-w64-x86_64-SDL2`
- **ESP32 builds:** nothing extra — PlatformIO downloads the toolchain

## Running a demo

```bash
cd getting_started/hello_world

pio run -e native                    # build and link the PC simulator
pio run -e native --target exec      # run it

pio run -e esp32dev                  # build for ESP32
pio run -e esp32dev --target upload  # flash it
```

### Platform notes

The `platformio.ini` files ship configured for **Windows/MSYS2**, which is the
primary development environment for this project. On Linux and macOS, edit the
`[env:native]` `build_flags` of the demo you are building:

- Remove the `-IC:/msys64/...`, `-LC:/msys64/...` and `-mconsole` flags.
- On macOS, uncomment the Homebrew `-I/opt/homebrew/include` and
  `-L/opt/homebrew/lib` lines.

CI applies the Linux variant of this edit automatically, so the demos are
verified to build on both Windows and Linux.

## Engine dependency

Each demo pulls the engine from the PlatformIO Registry:

```ini
lib_deps = gperez88/PixelRoot32-Game-Engine@^1.9.0
```

One demo, [`iso_tilemap_export`](graphics/iso_tilemap_export), is pinned to the
engine's `feature/isometric-support` branch instead: it needs
`graphics/ProjectedMapBounds.h` and `math/Projection.h`, which `1.9.0` does not
ship. That pin is temporary and its README says so.

To develop a demo against a local engine checkout instead, swap that line for
a symlink dependency:

```ini
lib_deps = symlink:///absolute/path/to/PixelRoot32-Game-Engine
```

## Repository layout

```text
getting_started/   first-contact projects
games/             complete playable games
audio/             one audio topic: sequencing, SFX banks, envelopes, sweeps
ui/                one UI topic: layouts, widgets, HUDs, menu navigation
input/             one input topic: touch, gestures, calibration
performance/       what a feature costs: memory budgets, pooling, redraw cost
gameplay/          one gameplay system per demo (rooms, physics, state)
graphics/          rendering: projections, tilemaps, cameras, blit paths
```

New categories are added as demos need them — the rule is one clear reason
per folder, not a slot for everything.

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md). In short: a demo must build in both
`native` and `esp32dev`, ship a README explaining which engine build flags it
needs and why, and use only assets that are freely redistributable and
modifiable, including commercially.

## License

The demo source code is distributed under the [MIT license](LICENSE).

Assets are licensed individually. Each demo's README documents the license
and source of every asset it ships; where a demo's art is original work
created for this repository, its README says so explicitly.
