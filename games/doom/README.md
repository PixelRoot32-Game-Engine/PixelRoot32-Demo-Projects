# DOOM (PixelRoot32 port)

> **Work in progress** — this project is a scaffold. It boots, it owns a
> 320x200 logical surface and it draws a placeholder frame through the engine's
> present path. There is no game logic and no WAD reader yet: the phase plan in
> [`docs/plan-doom-esp32.md`](https://github.com/PixelRoot32-Game-Engine/PixelRoot32-Game-Engine/blob/feature/doom-esp32/docs/plan-doom-esp32.md)
> is the specification.

The target is DOOM (1993) on the CYD — the ESP32-2432S028 "Cheap Yellow
Display": an ILI9341 240x320 panel mounted in portrait and rotated a quarter
turn to a 320x240 frame, 240 MHz dual core, 4 MB flash, 327,680 B of DRAM, no
PSRAM. The engine supplies what a 1993 renderer does not need to own again: an
8bpp surface, the 8bpp-to-panel present path, input and the APU audio backend.
Everything above that line — the BSP renderer, the game loop, the fixed-point
math, the WAD reader — is DOOM's own code in [`lib/doom/`](lib/doom/), kept at
arm's length from the engine.

Language: C++17 (+ C99 for the DOOM core)  
Engine: `PixelRoot32-Game-Engine` branch `feature/doom-esp32`
(tracking `feature/apu-hybrid-synthesis`)  
Environments: `native`, `esp32dev_ILI9341`  
Category: Games

## The one constraint that shapes everything

DOOM renders **320x200**, and 320 is structural rather than a setting:
`SCREENWIDTH` sizes `distscale[]`, every `visplane_t` (`top[320]` /
`bottom[320]`), `floorclip` / `ceilingclip` and the status bar layout. So the
logical surface is fixed at 320x200 on every environment.

The rotated CYD frame is 320x240, so the surface fits **1:1** with a 20 px bar
above and below — no scaling, no crop. Phase 0 measured that window presenting
in 13.75 ms at 80 MHz (D0.1).

| Environment | Window | What it does |
| --- | --- | --- |
| `native` | 320x200 | SDL2 window, 1:1 with the surface |
| `esp32dev_ILI9341` | 320x200 + 2x20 px bars | the CYD, 1:1 |

### Why the build flags look transposed

`DisplayConfig::applyRotationNormalization()` swaps width and height — and the
offsets — whenever the rotation is odd. The flags therefore state the geometry
in the panel's own portrait axes:

| Flag | Value | After the swap |
| --- | --- | --- |
| `PHYSICAL_DISPLAY_WIDTH` / `_HEIGHT` | 200 / 320 | 320x200 window |
| `LOGICAL_WIDTH` / `_HEIGHT` | 200 / 320 | 320x200 surface |
| `X_OFF_SET` / `Y_OFF_SET` | 20 / 0 | 20 px bars above and below |

The platform header `static_assert`s that translation, so a flag set that does
not resolve to 320x200 fails the build instead of drawing off-panel.

## Build and upload

From **`games/doom`**:

```bash
pio run -e native
pio run -e esp32dev_ILI9341
```

The native build needs SDL2 (`libsdl2-dev` on Linux, `brew install sdl2` on
macOS, MSYS2/MinGW on Windows). The `[env:native]` flags ship with the
MSYS2 paths; swap them for the commented Homebrew lines on macOS.

```bash
pio run -e esp32dev_ILI9341 --target upload
pio device monitor
```

## Hardware

The CYD is an `esp32dev` module: 240 MHz, dual core, 4 MB flash, no PSRAM. The
pin map is copied from the pong demo, which is where it was validated; demos in
this repository do not import each other's code.

| Function | GPIO |
| --- | --- |
| TFT MOSI / SCLK / DC / RST / CS | 23 / 14 / 2 / 4 / 15 |
| Backlight | 16 (`TFT_BL`, active high) |
| SPI clock | 80 MHz (`SPI_FREQUENCY`), measured in Phase 0 |
| I2S BCLK / LRCK / DOUT | 5 / 21 / 19, 22050 Hz |
| D-pad up / down / left / right | 32 / 33 / 25 / 26 |
| A / B | 27 / 13 |
| Touch | XPT2046 present, bus not claimed yet |

`PIXELROOT32_TFT_ESPI_LINES_PER_BLOCK` is pinned to **20** in
[`lib/platformio.ini`](lib/platformio.ini): the drawer allocates two DMA line
buffers of `physicalWidth * 2 B * linesPerBlock` each, so 60 lines at 320 px
costs 76,800 B against the 25,600 B a 20-line block costs — 51,200 B that the
twin framebuffer needs. A 20-line block only renders cleanly with
`PIXELROOT32_DRAIN_PRESENT` defined, which closes each frame's transfer instead
of leaving its tail in flight into the next frame.

## What the scaffold proves

- The engine links from the tracker branch, not from a released version.
- A 320x200 logical surface reaches the panel 1:1, letterboxed by
  `Y_OFF_SET`-derived offsets.
- Orientation: red / green / blue / yellow markers sit at the four corners in
  the order the rasteriser writes them.
- The letterbox is real: the bars are the panel showing nothing, not the engine
  drawing black.

## Project layout

```text
games/doom/
├── platformio.ini          # native, esp32dev_ILI9341
├── lib/platformio.ini      # [base] / [base_native] / [base_esp32] templates
├── lib/doom/               # the DOOM core lands here (GPLv2, see License)
├── bench/                  # Phase 0 measurement firmware (throwaway)
└── src/
    ├── main.cpp            # platform selector only
    ├── DoomScene.h/.cpp    # composition root; a placeholder frame for now
    └── platforms/          # native.h, esp32_dev_ILI9341.h
```

## Phase 0 bench

[`bench/`](bench/) is a standalone PlatformIO project, not a demo: it sits at
depth 4, so the demo CI does not discover it, and it is deleted when Phase 0
closes. It measured the numbers the plan had only estimated — SPI clock, DMA
block size, fill throughput, DRAM census, the flash partition map and the
letterbox. Its own README carries the env table, the serial commands and the
results.

## License

Source code: [MIT](../../LICENSE), **until the DOOM core lands**. DOOM's source
is distributed under the Doom Source Code License, and the Chocolate Doom and
rp2040-doom work this port reads is GPLv2; once `lib/doom/` contains any of it,
this firmware becomes a GPLv2 combined work and this section has to say so
before the first public build.
