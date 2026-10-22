# Pac-Man Clone

> **Demonstration project** — provided as an example of what the PixelRoot32
> Game Engine can do. It is not a product: parts may be incomplete,
> experimental, or deliberately simplified to keep one idea in focus.

A grid-locked arcade maze game in the classic style: run the corridors, clear
every dot, and stay away from the four hunters that patrol the maze. The
interesting part of the demo is not the game itself but how it is put
together — every rule lives in an engine-independent layer that can be
exercised by a host-side test with no graphics session at all.

Language: C++17
Engine: `gperez88/PixelRoot32-Game-Engine@^1.12.0`
Environments: `native`, `esp32dev`
Category: Games

![Pac-Man Clone](screenshots/screenshot.png)

## Requirements (build flags)

| Flag | Value | Why |
| --- | --- | --- |
| `PIXELROOT32_ENABLE_GAMEPLAY_GRID_SPACE` | `1` | The rules layer moves actors between cells with `pixelroot32::gameplay::GridMotion`. Building without this flag is not an error — the grid primitives are compiled out and the rules layer stops compiling, because `GridMotion` is guarded by the same flag. |
| `PIXELROOT32_ENABLE_AUDIO` | `1` | Sound effects for dots, pellets, defeated hunters, a lost life and a finished level. Building without it is not an error — the game plays silently. |
| `PIXELROOT32_ENABLE_PHYSICS` | `0` | The game resolves its own collisions on the grid; a physics engine would only add a second, conflicting source of truth. |
| `PIXELROOT32_ENABLE_PARTICLES` | `0` | Nothing in the game emits particles. |
| `PIXELROOT32_ENABLE_UI_SYSTEM` | `0` | The interface is a status band and a few text overlays, not a widget tree. |
| `PIXELROOT32_ENABLE_DEPTH_SORT` | `0` | Nothing overlaps, so there is nothing to sort. |

## Platforms

| Environment | Display | Driver | Audio backend | Input |
| --- | --- | --- | --- | --- |
| `native` | 240×240 logical, windowed | SDL2 | SDL2 | Arrow keys, Space, Enter |
| `esp32dev` | 240×240 | ST7789 (TFT_eSPI) | ESP32 I2S (MAX98357A-class DAC) | Six GPIO buttons: up, down, left, right, A, B |

Both environments run the same rules layer. The native build is the fast
iteration loop; the ESP32 build is the target the demo is written for.

## Controls

| Action | Native | ESP32 |
| --- | --- | --- |
| Move | Arrow keys | D-pad buttons |
| Pause / resume | Space | Button A |
| New game | Enter | Button B |

A direction pressed before an intersection is remembered and taken at that
intersection, exactly like the arcade original.

## The rules

The demo implements the rule set of the classic game: run the corridors,
collect every dot, and stay away from four hunters that patrol the maze.
The pieces are worth 10 points, a power pellet is worth 50 and turns the
hunters around for six seconds, and a frightened hunter is worth 200, 400,
800 and then 1600 points for consecutive hits inside one power effect. The
player starts with three lives and earns a fourth at every ten thousand
points.

Each hunter hunts in its own way: one goes straight for the player, one
aims four cells ahead of the player, one aims past the player measured from
the first hunter, and one hunts from far away and retreats when the player
gets close. All four find their way by asking a breadth-first route field
for the distance to every neighbouring cell, which makes their behaviour
deterministic and testable.

The level is a table of ASCII rows under `src/rules/PacmanMaze.cpp`. It is
28 columns by 26 rows with four power pellets, a tunnel on each side of the
middle row and a ghost house in the centre. A validator rejects a level with
a ragged row, an open border, a start inside a wall, nothing to collect, or
a corridor that cannot be reached from the player's start — before a game
ever begins.

## What it demonstrates

-   **A grid-locked actor.** The player occupies exactly one logical cell at
    a time and interpolates between cells for drawing. Gameplay rules never
    see the player straddling two cells, so a collision is always resolved at
    a cell, not at a frame boundary.
-   **A rules layer with no engine in it.** `src/rules/` holds the maze, the
    player, the enemies, the scoring and the game states. It includes the
    engine's *gameplay* primitives but never its drawing system, its input
    device or its audio backend — which is what makes the whole rule set
    testable headless.
-   **A level as data.** The maze is a table of ASCII rows. A validator
    rejects a level with a ragged row, an open border, a start inside a wall,
    nothing to collect, or a corridor that cannot be reached from the
    player's start — before a game ever begins.
-   **A fixed logic step.** The simulation advances in 20 ms steps, capped at
    four steps per frame, so the game plays the same whether the screen is
    redrawn at 30 or 60 frames per second.
-   **Rules that report, presentation that reacts.** The rules layer never
    plays a sound: it reports what happened as a set of flags, and the scene
    decides what that should look and sound like. The same split keeps the
    audio backend out of the rules.

## Project layout

```text
games/pacman/
├── platformio.ini          # envs: native, esp32dev, native_test
├── lib/platformio.ini      # [base] / [base_native] / [base_esp32] templates
├── screenshots/            # screenshot.png
├── src/
│   ├── main.cpp            # platform selector only
│   ├── platforms/          # native.h, esp32_dev.h — backend wiring per target
│   ├── assets/             # generated sprite tables (CC0, original art)
│   ├── audio/              # the sound effects, synthesised rather than sampled
│   ├── PacmanScene.*       # presentation: reads input, draws the rules
│   ├── PacmanPalette.h     # the demo's colours, in one place
│   └── rules/              # the rules layer: no drawing, no input, no audio
│       ├── PacmanConfig.h      # every tunable value, in one place
│       ├── PacmanMaze.*        # the level, its validator and its walkability
│       ├── PacmanRouteField.*  # shortest-route distances over the grid
│       ├── PacmanPlayer.*      # grid-locked movement and buffered turns
│       ├── PacmanEnemy.*       # the hunters, their states and their targets
│       ├── PacmanSession.*     # score, lives, collectibles, game states
│       └── ...
├── test/                   # host-side Unity suite (no graphics session)
│   └── unit/test_pacman_*/
└── tools/
    └── generate_sprites.py # regenerates src/assets/PacmanSprites.h
```

## Build

```bash
pio run -e native
pio run -e esp32dev
```

Host-side unit suite (no window, no SDL, no hardware):

```bash
pio test -e rules_test
```

The suite compiles only `src/rules`, which is the point: the rules have no
dependency on the drawing system, the input device or the audio backend. The
environment installs the engine for its gameplay headers but never builds it,
so the suite runs on a machine with no display stack at all.

## Upload (ESP32)

```bash
pio run -e esp32dev --target upload
pio device monitor
```

## Engine documentation

-   Engine repository: <https://github.com/PixelRoot32-Game-Engine/PixelRoot32-Game-Engine>
-   Gameplay framework (`GridSpec`, `GridMotion`): <https://pixelroot32.org/docs/gameplay-framework>
-   Getting started with the engine: <https://pixelroot32.org/docs/getting-started>

## License

-   **Sprite and level artwork** — original work created for this demo by
    `tools/generate_sprites.py` and by hand in `src/rules/PacmanMaze.cpp`.
    Dedicated to the public domain under **CC0 1.0**. The artwork is drawn
    from scratch and is not derived from any copyrighted game's assets.
-   **Code** — MIT, matching the engine's licence.

This demo is a clone of a game *genre*, not of a specific commercial title.
No trademarked names, characters, sounds or level layouts are used.
