# DOOM (PixelRoot32 port)

> **Work in progress** — this project is a scaffold. It boots, it owns a
> 320x200 logical surface and it draws a placeholder frame through the engine's
> present path. There is no game logic and no WAD reader yet: the phase plan in
> [`docs/plan-doom-esp32.md`](https://github.com/PixelRoot32-Game-Engine/PixelRoot32-Game-Engine/blob/feature/doom-esp32/docs/plan-doom-esp32.md)
> is the specification.

The target is DOOM (1993) on the reference ESP32 board — a square 240x240
ST7789 panel, 240 MHz, dual core, 4 MB flash, 327,680 B of DRAM and **no
PSRAM** — with the engine supplying what a 1993 renderer does not need to own
again: an 8bpp surface, the scaled 8bpp-to-panel present path, input and the
APU audio backend. Everything above that line — the BSP renderer, the game
loop, the fixed-point math, the WAD reader — is DOOM's own code in
[`lib/doom/`](lib/doom/), kept at arm's length from the engine.

Language: C++17 (+ C99 for the DOOM core)  
Engine: `PixelRoot32-Game-Engine` branch `feature/doom-esp32`
(tracking `feature/apu-hybrid-synthesis`)  
Environments: `native`, `esp32dev_ST7789`  
Category: Games

## The one constraint that shapes everything

DOOM renders **320x200**, and 320 is structural rather than a setting:
`SCREENWIDTH` sizes `distscale[]`, every `visplane_t` (`top[320]` /
`bottom[320]`), `floorclip` / `ceilingclip` and the status bar layout. So the
engine's `LOGICAL_WIDTH` / `LOGICAL_HEIGHT` are fixed at 320x200 on every
environment and only the *physical* window changes. Both platform headers
`static_assert` this, so a build that tries to follow the panel with the
logical surface fails instead of shipping a cropped HUD.

| Environment | Physical window | What it does |
| --- | --- | --- |
| `native` | 240x180 | SDL2 window mirroring the primary board (0.75x squash) |
| `esp32dev_ST7789` | 240x180 | primary target: the square 240x240 board, 30 px bars |

The ILI9341 320x240 "Cheap Yellow Display" is the secondary, fidelity target,
and its environment is **deliberately absent until Phase 1**:
`DisplayConfig::applyRotationNormalization()` swaps width and height for odd
rotations, so a 320x200 window requested with `DISPLAY_ROTATION=3` becomes a
200x320 one on a panel whose rotated frame is 320x240. Phase 0 still measures
that panel — [`bench/`](bench/) drives `TFT_eSPI_Drawer` directly for D0.6 —
but the demo environment returns only once the rotation/sub-window question is
settled, which is engine-side work.

## Build and upload

From **`games/doom`**:

```bash
pio run -e native
pio run -e esp32dev_ST7789
```

The native build needs SDL2 (`libsdl2-dev` on Linux, `brew install sdl2` on
macOS, MSYS2/MinGW on Windows). The `[env:native]` flags ship with the
MSYS2 paths; swap them for the commented Homebrew lines on macOS.

```bash
pio run -e esp32dev_ST7789 --target upload
pio device monitor
```

## Hardware

The board is an `esp32dev` module: 240 MHz, dual core, 4 MB flash, no PSRAM.
The pin map is copied from the pong demo, which is where it was validated;
demos in this repository do not import each other's code.

| Function | GPIO |
| --- | --- |
| TFT MOSI / SCLK / DC / RST / CS | 23 / 18 / 2 / 4 / -1 (CS strapped low) |
| SPI clock | 40 MHz (`SPI_FREQUENCY`) |
| I2S BCLK / LRCK / DOUT | 26 / 25 / 22, 22050 Hz |
| D-pad up / down / left / right | 32 / 27 / 33 / 14 |
| A / B | 13 / 12 |
| Touch | none (`TOUCH_CS=-1`) |

`PIXELROOT32_TFT_ESPI_LINES_PER_BLOCK` is pinned to **20** in
[`lib/platformio.ini`](lib/platformio.ini), not left at the engine's default of
60: the drawer allocates two DMA line buffers of
`physicalWidth * 2 B * linesPerBlock` each, so 60 costs 57,600 B at a 240 px
window — 38,400 B more than the DRAM budget allows. Phase 0's D0.2 measures the
frame-time cost of the trade.

## What the scaffold proves

- The engine links from the tracker branch, not from a released version.
- A 320x200 logical surface reaches a 240x180 window through the drawer's
  `xLUT` / `yLUT` pair — the same path DOOM's pixels will use.
- Orientation: red / green / blue / yellow markers sit at the four corners in
  the order the rasteriser writes them.
- The letterbox is real: the bars are the panel showing nothing, not the engine
  drawing black.

## Project layout

```text
games/doom/
├── platformio.ini          # native, esp32dev_ST7789
├── lib/platformio.ini      # [base] / [base_native] / [base_esp32] templates
├── lib/doom/               # the DOOM core lands here (GPLv2, see License)
├── bench/                  # Phase 0 measurement firmware (throwaway)
└── src/
    ├── main.cpp            # platform selector only
    ├── DoomScene.h/.cpp    # composition root; a placeholder frame for now
    └── platforms/          # native.h, esp32_dev_ST7789.h
```

## Phase 0 bench

[`bench/`](bench/) is a standalone PlatformIO project, not a demo: it sits at
depth 4, so the demo CI does not discover it, and it is deleted when Phase 0
closes. It measures the numbers the plan currently estimates — SPI clock
stability, DMA block size, fill throughput, DRAM census, the flash partition
map and the letterbox — and its own README carries the env table, the serial
commands and the empty results table.

## License

Source code: [MIT](../../LICENSE), **until the DOOM core lands**. DOOM's source
is distributed under the Doom Source Code License, and the Chocolate Doom and
rp2040-doom work this port reads is GPLv2; once `lib/doom/` contains any of it,
this firmware becomes a GPLv2 combined work and this section has to say so
before the first public build.
