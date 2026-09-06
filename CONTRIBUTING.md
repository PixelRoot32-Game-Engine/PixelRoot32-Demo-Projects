# Contributing

Thanks for wanting to add a demo. This repository has one job: show what the
[PixelRoot32 Game Engine](https://github.com/PixelRoot32-Game-Engine/PixelRoot32-Game-Engine)
can do, in projects small enough to read in one sitting.

## What makes a good demo

- **One idea, clearly.** A demo that shows three systems at once shows none of
  them. If you need three, that is three demos.
- **Self-contained.** Copying the folder out of this repository must produce a
  project that still builds. No shared code, no relative paths that escape the
  demo folder.
- **Readable over clever.** Someone reading the demo is learning the engine,
  not admiring your optimization.

## Structure

Place the demo in the category that matches what it teaches. Outside
`getting_started/` and `games/`, a category is an engine module: the flags in
the right-hand column are what its demos are built on.

| Category | For | Typical flags |
| -------- | --- | ------------- |
| `getting_started/` | First-contact projects — minimal, heavily commented | none |
| `games/` | Complete playable games | whatever the game needs |
| `audio/` | One audio topic — sequencing, SFX banks, envelopes, sweeps | `PIXELROOT32_ENABLE_AUDIO` |
| `ui/` | One UI topic — layouts, widgets, HUDs, menu navigation | `PIXELROOT32_ENABLE_UI_SYSTEM` |
| `input/` | One input topic — touch, gestures, calibration | `PIXELROOT32_ENABLE_TOUCH` |
| `performance/` | One cost topic — memory budgets, pooling, redraw cost | `SCENE_ARENA`, `DIRTY_REGIONS`, `GAMEPLAY_OBJECT_POOL` |
| `gameplay/` | A single gameplay system (rooms, physics, state machines) | `PIXELROOT32_ENABLE_GAMEPLAY_*` |
| `graphics/` | One rendering topic — projections, tilemaps, cameras, blit paths | sprite bit depths, tilemap, palette, depth sort, particles |

The rule that follows from that table:

> A demo teaches **one** topic and enables the minimum set of flags that topic
> needs. If it takes a complete game to teach it, it belongs in `games/`.

The second half matters as much as the first. A demo that enables four
subsystems to show one of them tells the reader nothing about what that one
costs; a demo whose flag list is exactly its topic tells them what the feature
is worth in flash and RAM before they adopt it. Games are exempt — they are
sold as complete, not as one idea.

If nothing fits, propose a new category in your pull request and say why the
existing ones do not work. The catalogue's shape is tracked in
[`docs/category-rebalance.md`](docs/category-rebalance.md).

A demo folder looks like this:

```text
<category>/<demo_name>/
├── .gitignore
├── README.md
├── platformio.ini        # envs: native + esp32dev (+ others as needed)
├── lib/platformio.ini    # [base] / [base_native] / [base_esp32] templates
├── screenshots/          # screenshot.png — one frame from the native build
└── src/
    ├── main.cpp
    ├── platforms/        # native.h, esp32_dev.h — backend wiring per target
    └── assets/           # generated sprite/palette/audio headers
```

Use `snake_case` for demo folder names.

## Required environments

Every demo must build a `native` environment and at least one hardware one:

```bash
pio run -e native
pio run -e esp32dev
```

`esp32dev` is the default hardware target and the right choice unless the demo
needs something that board does not have. `games/chess` is the exception that
shows the rule: touch is its whole interaction model, the generic ST7789 board
has no panel, so it ships `esp32cyd` instead. A demo with no hardware
environment at all does not land.

CI reads the matrix from each demo's own `[env:*]` sections rather than
hardcoding a list, so a demo built for a specific board is covered without
every other demo growing an environment it has no use for. Environments whose
name contains `test` are skipped by the build job.

If a demo grows a host-side test suite, name its environment `*_test` and add a
row to the `test` job's matrix in
[`.github/workflows/build.yml`](.github/workflows/build.yml) — that matrix is
hand-maintained, so a suite nobody lists is a suite CI never runs.

Depend on the engine through the registry, pinned to a caret range:

```ini
lib_deps = gperez88/PixelRoot32-Game-Engine@^1.9.0
```

Do not commit a `symlink://` dependency — it points at a path only you have.

## README requirements

Every demo needs a `README.md` that opens with the same block, so the demos read
as one set rather than five unrelated documents:

```markdown
# Demo Name

> **Demonstration project** — provided as an example of what the PixelRoot32
> Game Engine can do. It is not a product: parts may be incomplete,
> experimental, or deliberately simplified to keep one idea in focus.

One paragraph on what the demo does and which single idea it is about.

Language: C++17
Engine: `gperez88/PixelRoot32-Game-Engine@^1.9.0`
Environments: `native`, `esp32dev`
Category: Getting started | Games | Audio | UI | Input | Performance | Gameplay | Graphics

![Demo Name](screenshots/screenshot.png)
```

Each metadata line ends with **two trailing spaces** so the four render as four
lines instead of collapsing into one paragraph. The title is the demo's name
alone — no `Example` suffix, no engine name appended.

Then, in this order:

1. **Requirements (build flags)** — every `PIXELROOT32_ENABLE_*` flag the demo
   sets, and *why*. Say what breaks without it. "Building without this flag is
   not an error — it is a black screen" is exactly the kind of note that saves
   someone an afternoon.
2. **Platforms** — display size, driver, and audio backend per environment.
3. **Controls.**
4. Any number of demo-specific sections.
5. **Build / Upload** commands.
6. **License** — asset-by-asset attribution (see below). A demo that ships no
   art still needs this section; it says so.

Use these exact heading names where they apply, rather than synonyms:
`## Requirements (build flags)`, `## Platforms`, `## Controls`,
`## What it demonstrates`, `## Project layout`, `## Build`, `## Upload (ESP32)`,
`## Engine documentation`, `## License`.

## Screenshots

Every demo ships one screenshot at `screenshots/screenshot.png`, captured from
`pio run -e native --target exec` with no window border, and links it from the
README header block shown above. Prefer a single image: one frame that shows
what the demo is about beats a gallery. If a demo genuinely needs a second one,
name it for what it shows (`screenshots/collision.png`) and add a
`## Screenshots` section at the end for the extras.

The folder sits at the demo root, beside `platformio.ini`. PlatformIO only
compiles `src/`, so nothing needs to be excluded from the build.

Link to engine documentation with **absolute URLs**. This repository is not
inside the engine tree, so `../../docs/...` resolves to nothing.

## Assets

Ship only assets that are freely redistributable and modifiable, **including
for commercial use**:

- CC0 1.0 / public domain dedication
- CC BY 3.0 / 4.0
- CC BY-SA 3.0 / 4.0
- Original work you created and are licensing under one of the above

**Not accepted:** "royalty-free", CC BY-NC, CC BY-ND, ripped sprite sheets, or
anything whose license you cannot name.

The demo README's License section must list, per asset: what it is, who made
it, under which license, and where it came from. If you generated the art
yourself, commit the generator script under the demo's `tools/` folder and say
so.

## Code style

Follow the engine's conventions: C++17, no exceptions, no RTTI, `snake_case`
files matching the primary type, and compile-time configuration through
`if constexpr` rather than runtime branches on ESP32 hot paths.

Keep an eye on the target. This engine runs on microcontrollers — a demo that
allocates per frame is teaching the wrong lesson.

`init()` and `update()` call their `Scene::` base first. **`draw()` does not.**
`Scene::draw()` paints the scene's entities, so a background fill placed after
it overpaints them. Paint the background, then call the base:

```cpp
void MyScene::draw(pr32::graphics::Renderer& renderer) {
    renderer.drawFilledRectangle(0, 0,
        renderer.getLogicalWidth(), renderer.getLogicalHeight(),
        pr32::graphics::Color::Black);
    Scene::draw(renderer);
}
```

## Pull requests

Use [Conventional Commits](https://www.conventionalcommits.org/) for commit
messages, and say in the PR description which engine version you built against
and which hardware, if any, you tested on.
