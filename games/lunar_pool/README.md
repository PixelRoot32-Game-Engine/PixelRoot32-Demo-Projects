# Lunar Pool

> **Demonstration project** — provided as an example of what the PixelRoot32
> Game Engine can do. It is not a product: parts may be experimental or
> deliberately simplified to keep one idea in focus.

A fixed-point pool/billiards game inspired by the NES title *Lunar Pool*: aim
and strike a cue ball around the table, sink the 6 numbered target balls **in
ascending order**, and clear all **10 tables** without running out of shots.
Ball movement, collisions, cushions, pockets, and friction run entirely on a
deterministic, engine-free integer core (`src/pool/`), so the same recorded
input sequence always reproduces the same outcome on every platform.

Language: C++17  
Engine: `gperez88/PixelRoot32-Game-Engine@^1.10.0`  
Environments: `native`, `esp32dev`  
Category: Games

![Lunar Pool](screenshots/screenshot.png)

## How to play

| Input | Aiming | Elsewhere |
| ----- | ------ | --------- |
| Left/Right (hold) | Rotate aim | — |
| Up/Down (tap) | Power level 1–10 | — |
| A | Shoot | Confirm (menu, game over) |
| B | Pause | — |

Native keys: arrows + Space (A) + Return (B). ESP32: 6 GPIO buttons
(see `src/platforms/esp32_dev.h`).

## Rules (v1)

- Each stage grants `targets × 2` shots (12); every shot costs 1, foul or not.
- Correct pocket: +100. Foul (cue scratched or wrong order): −50, floored
  at 0; pocketed balls stay down and the cue respots.
- Clearing the table advances to the next stage with the score carried over;
  clearing stage 10 wins the run. Losing retries the same stage.
- Difficulty comes from the table, like the original: every stage plays the
  same 6 balls — Classic, Bites, Zigzag, Gate, Chevron, Fortress, Donut,
  Twins, 4-pocket Octagon, Corridor.

## Verifying

- `pio test -e host_test` — engine-free unit suites under `test/`
  (fixed-point, trig, geometry/tables, ball physics, collisions, rules).
- `pio run -e native` / `pio run -e esp32dev` — display / firmware builds.
