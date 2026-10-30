# DOOM on ESP32 - Phase 0 measurement bench

Throwaway firmware for the feasibility audit's Phase 0 gate
(`docs/plan-doom-esp32.md`, §5 and §6.6). It carries **no DOOM code**, no WAD
and no renderer: it measures the reference board and PixelRoot32's real present
path, then prints one CSV line per measurement over serial.

Delete this directory when Phase 0 closes.

## Why it lives at depth 4

The demo CI discovers `<category>/<demo>/platformio.ini` at depth 3
(`.github/workflows/build.yml`). `games/doom/bench/platformio.ini` is one level
deeper, so CI does not build this scaffolding. That is intentional, not an
accident to fix.

## Environments

All environments use `board = esp32dev`, `framework = arduino`, and the engine
pinned to `#feature/doom-esp32`.

| Environment | Panel | Physical window | Logical | SPI | Lines/block | Measures |
| --- | --- | --- | --- | --- | --- | --- |
| `bench_st7789_40` | ST7789 240x240 | 240x180, `Y_OFF_SET=30` | 320x200 | 40 MHz | 60 | D0.1 baseline |
| `bench_st7789_55` | ST7789 240x240 | 240x180, `Y_OFF_SET=30` | 320x200 | 55 MHz | 60 | D0.1 |
| `bench_st7789_80` | ST7789 240x240 | 240x180, `Y_OFF_SET=30` | 320x200 | 80 MHz | 60 | D0.1 |
| `bench_lpb60` | ST7789 240x240 | 240x180, `Y_OFF_SET=30` | 320x200 | 40 MHz | 60 | D0.2 |
| `bench_lpb30` | ST7789 240x240 | 240x180, `Y_OFF_SET=30` | 320x200 | 40 MHz | 30 | D0.2 |
| `bench_lpb20` | ST7789 240x240 | 240x180, `Y_OFF_SET=30` | 320x200 | 40 MHz | 20 | D0.2 |
| `bench_lpb16` | ST7789 240x240 | 240x180, `Y_OFF_SET=30` | 320x200 | 40 MHz | 16 | D0.2 |
| `bench_ili9341_55` | ILI9341 320x240 (CYD) | 320x200, `Y_OFF_SET=20`, rotation 3 | 320x200 | 55 MHz | 60 | D0.6 second panel |
| `bench_flash` | ST7789 240x240 | 240x180, `Y_OFF_SET=30` | 320x200 | 40 MHz | 60 | D0.5, custom partition table |

The ST7789 pin map is the one in
`games/doom/src/platforms/esp32_dev_ST7789.h`; the CYD map is the one in
`esp32_dev_ILI9341.h`. Both are reproduced as build flags, not included as
headers, because those headers define an `Engine` and a scene.

### Why the bench drives `TFT_eSPI_Drawer` directly

`DisplayConfig::applyRotationNormalization()` swaps width and height for odd
rotations. That is correct when the caller passes the panel's native
orientation, but it cannot express a **320x200 sub-window of a rotated 320x240
panel**: passing 320x200 with `DISPLAY_ROTATION=3` yields a 200x320 window. The
bench therefore configures the same public drawer directly with the effective
geometry. The build flags above are exactly what the drawer receives.

This is also a finding about the port: `games/doom/src/platforms/esp32_dev_ILI9341.h`
passes 320x200 with rotation 3 through `DisplayConfig`, so on this engine it
would be normalised to 200x320. It is outside this bench's edit surface; the
Phase 1 hardware run will show it.

## Build and flash

The committed `lib_deps` is the git branch ref, which does not exist on the
remote until the branch is pushed. For a local build, temporarily replace it
with the worktree path:

```ini
lib_deps = symlink://<engine-worktree>
```

and restore it before committing. A committed `symlink://` fails the demo CI
structure job. Verify with:

```powershell
Select-String -Path platformio.ini -Pattern 'symlink'   # must return nothing
```

Build (PlatformIO may not be on `PATH`; use its full path if needed):

```powershell
pio run -e bench_st7789_40
pio run -e bench_st7789_40 -t upload
pio device monitor -b 115200
```

`BENCH_DEFAULT_MODE` can run one probe automatically at boot; add
`-D BENCH_DEFAULT_MODE=4`, for example, to the environment's `build_flags`.

## Serial output contract

Every measurement is one line beginning `BENCH,` and carrying `env=<name>`.
Capture with:

```powershell
pio device monitor -b 115200 | Tee-Object bench.log
Select-String -Path bench.log -Pattern '^BENCH,env='
```

Boot prints two lines before the menu: `probe=BOOT` (geometry, SPI clock, block
size, free heap) and `probe=STATIC` (surface/DMA/LUT bytes computed from the
flags). Neither is a measurement.

### Menu

| Key | Probe | What it reports |
| --- | --- | --- |
| `1` | D0.1 clock sweep | 100 frames of a fixed pattern through the real drawer: `present_us_min/mean/max`, `call_us_mean`, `drain_us_mean`, `wire_us` and a frame-integrity count |
| `2` | D0.2 block size | The same loop plus `lines_per_block` and `free_internal_at_init` |
| `3` | D0.3 fill throughput | `ns_per_px` for `memset`, `byte_store` and `lut_store` at 320x200 and 240x180, 1,000 iterations each |
| `4` | D0.4 DRAM census | Internal free/largest before init, after init and after a second 320x200 surface, plus the 300 KB gate |
| `5` | D0.5 flash | `wad` partition lookup, `esp_partition_mmap` pointer, sequential MB/s and strided 4-byte read latency |
| `6` | D0.6 letterbox | Border and four corner markers on the physical window, held 60 s for a photo |
| `h` | help | Reprints the menu |

### Timing fields

`present_us` is the full serialised frame: `sendBuffer()` **plus**
`flushPendingTransfers()`, which waits out the last DMA block the driver
deliberately leaves in flight. `call_us_mean` is `sendBuffer()` alone, the cost
the engine's loop actually pays when the deferred tail overlaps the next
frame's work. `drain_us_mean` is that tail. `wire_us` is the theoretical
`width * height * 2 * 8 / SPI_FREQUENCY` floor and is a sanity check on the
measured transfer, not a measurement.

`bad_frames` counts frames whose source-surface CRC differs from the pattern
written before the loop. It catches DMA writing into the framebuffer. It does
**not** see panel-side tearing; D0.6's photo is the check for that.

## Results

Fill this in from the serial capture. Targets are PLAN §6.6, B1-B6.

| Probe | Env | Metric | Target | Measured |
| --- | --- | --- | --- | --- |
| D0.1 | `bench_st7789_80` | `present_us_mean` (240x180) | ≤ 18,000 | |
| D0.1 | `bench_st7789_55` | `present_us_mean` (240x180) | ≤ 18,000 | |
| D0.1 | `bench_st7789_40` | `present_us_mean` (240x180) | ≤ 18,000 | |
| D0.1 | all three | `bad_frames` | 0 | |
| D0.2 | `bench_lpb60` | `present_us_mean`, `free_internal_at_init` | table | |
| D0.2 | `bench_lpb30` | `present_us_mean`, `free_internal_at_init` | table | |
| D0.2 | `bench_lpb20` | `present_us_mean`, `free_internal_at_init` | table | |
| D0.2 | `bench_lpb16` | `present_us_mean`, `free_internal_at_init` | table | |
| D0.3 | any ST7789 | `lut_store` at 320x200 | ≤ 6.0 ns/px (B3) | |
| D0.3 | any ST7789 | `byte_store` at 320x200 | ≤ 12.0 ns/px (falsifier) | |
| D0.4 | `bench_lpb16` | `free_internal` after init | ≥ 307,200 B (B4) | |
| D0.4 | `bench_lpb20` | `free_internal` after init | ≥ 307,200 B (B4) | |
| D0.5 | `bench_flash` | `mb_s` sequential | record | |
| D0.5 | `bench_flash` | `mean_us` strided 4 B | ≤ 1.5 us (B6) | |
| D0.6 | both panels | corners red/green/blue/yellow, bars clean | photo | |

Notes for the operator:

- Run D0.4 on `bench_lpb16` (and `bench_lpb20`) for the budget of record. The
  60-line default allocates 57,600 B of DMA buffers at 240x180; at 20 lines it
  is 19,200 B, which is the plan's assumed row.
- If the drawer cannot allocate the requested block it logs a warning and
  falls back to 30 lines. `PIXELROOT32_DEBUG_MODE` is on so that warning is
  visible; D0.2 still reports the compile-time flag, so check the boot log.
- D0.5 reports `partition_missing` and fails the probe when the `wad`
  partition is absent. That is the expected result on every environment except
  `bench_flash`.

## Static numbers this bench already fixes

At 240x180 the drawer allocates, from the flags alone:

| Lines/block | DMA per buffer | DMA total | LUTs | Surface | Present state |
| --- | --- | --- | --- | --- | --- |
| 60 | 28,800 | 57,600 | 1,352 | 64,000 | 122,952 |
| 30 | 14,400 | 28,800 | 1,352 | 64,000 | 94,152 |
| 20 | 9,600 | 19,200 | 1,352 | 64,000 | 84,552 |
| 16 | 7,680 | 15,360 | 1,352 | 64,000 | 80,712 |

At 320x200 (CYD), 60 lines: 38,400 per buffer, 76,800 total, LUTs 1,552,
surface 64,000, present state 142,352 B. At 20 lines: 12,800 / 25,600 /
91,152 B. The `probe=STATIC` boot line prints the live values.

## Limitations

- No hardware access from the authoring machine: the bench is compiled, never
  flashed, and no result in this README is measured.
- The integrity check covers the source surface only (see above).
- `SPI_FREQUENCY` is the requested clock; the ESP32 divider may land below it.
  `wire_us` is computed from the request, so compare it with the measured
  `drain_us_mean` rather than treating it as truth.
- D0.3's `byte_store` uses volatile stores to keep GCC from turning the loop
  into `memset`; that is the rasteriser's floor, not a cache-tuned benchmark.
- D0.6 blocks the menu for 60 s and has no early exit; reset the board to
  return sooner.
- The bench measures with audio and every other optional engine module off.
  The port's audio task, input and DOOM buffers are added on top of these
  numbers, not included in them.
