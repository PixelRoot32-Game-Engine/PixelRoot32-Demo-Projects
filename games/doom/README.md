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
Environments: `native`, `esp32dev_ST7789`, `esp32dev_ILI9341`  
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
| `esp32dev_ILI9341` | 320x200 | secondary target: the CYD turned a quarter turn, 20 px bars, 1:1 |

## Build and upload

From **`games/doom`**:

```bash
pio run -e native
pio run -e esp32dev_ST7789
pio run -e esp32dev_ILI9341
```

The native build needs SDL2 (`libsdl2-dev` on Linux, `brew install sdl2` on
macOS, MSYS2/MinGW on Windows). The `[env:native]` flags ship with the
MSYS2 paths; swap them for the commented Homebrew lines on macOS.

```bash
pio run -e esp32dev_ST7789 --target upload
pio device monitor
```

## Hardware

Both boards are `esp32dev` modules: 240 MHz, dual core, 4 MB flash, no PSRAM.
The pin maps are copied from the pong demo, which is where they were validated;
demos in this repository do not import each other's code.

| Function | ST7789 (primary) | ILI9341 (CYD) |
| --- | --- | --- |
| TFT MOSI / SCLK / DC / RST / CS | 23 / 18 / 2 / 4 / -1 | 23 / 14 / 2 / 4 / 15 |
| SPI clock | 40 MHz | 55 MHz |
| I2S BCLK / LRCK / DOUT | 26 / 25 / 22 | 5 / 21 / 19 |
| D-pad up / down / left / right | 32 / 27 / 33 / 14 | 32 / 33 / 25 / 26 |
| A / B | 13 / 12 | 27 / 13 |
| Touch | none | XPT2046 present, not claimed |

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
├── platformio.ini          # native, esp32dev_ST7789, esp32dev_ILI9341
├── lib/platformio.ini      # [base] / [base_native] / [base_esp32] templates
├── lib/doom/               # the DOOM core lands here (GPLv2, see License)
├── src/
│   ├── main.cpp            # platform selector only
│   ├── DoomScene.h/.cpp    # composition root; a placeholder frame for now
│   └── platforms/          # native.h, esp32_dev_ST7789.h, esp32_dev_ILI9341.h
└── odd/tasks/              # (repository root) the tracked task record
```

## License

Source code: [MIT](../../LICENSE), **until the DOOM core lands**. DOOM's source
is distributed under the Doom Source Code License, and the Chocolate Doom and
rp2040-doom work this port reads is GPLv2; once `lib/doom/` contains any of it,
this firmware becomes a GPLv2 combined work and this section has to say so
before the first public build.
