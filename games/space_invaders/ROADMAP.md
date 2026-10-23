# Space Invaders - fidelity plan

An audit of this demo against the 1978 Taito arcade game, and the plan for
closing the gap. Written on branch `feature/space-invaders-fidelity`; no code
change is part of the audit.

Every finding below cites the file and line it comes from, and every number in
the reference model comes from the ROM documentation listed at the end, not
from memory of playing the game. Where the reference is not reliably
documented - the march pitches, the exact hold on a player death - the spec
names the measurement to take instead of inventing a number.

The demo stays a teaching piece. The plan keeps the engine-facing ideas that
make it worth reading (fixed memory, swept collision, one animation clock) and
replaces the rules that are simply not Space Invaders.

## What the demo does today

| System | Today |
|---|---|
| Formation | 4 rows x 8 columns, 32 aliens, 30/20/10 points, animated by `move()` |
| March | every 417 ms, all aliens shift 2.5 px; 7 px drop on the edge; tempo factor scales the same timer |
| Cannon | 9 discrete lanes, 100 ms ease-out interpolation, 300 ms auto-repeat delay, 150 ms interval |
| Player shot | 1x4 at 80 px/s, one alive, release-to-fire |
| Enemy fire | one random column per formation step, no cap, no reload model |
| Shields | 4 rectangles, 24x16, 4 hit points, drawn as a shrinking height |
| Saucer | none |
| Waves | clearing the rack ends the run with `YOU WIN!` |
| Score | 4 digits, no high score, no extra ship |
| Lives | 3, drawn as a number |
| Audio | an 8-note C loop on three speeds plus win/lose jingles; three effects |
| Art | 1bpp masks upscaled 1.25x, drawn orange/yellow, 72-star background |

## The 1978 reference

### Screen and overlays

The arcade playfield is 224 x 256 pixels, one arcade pixel per tube pixel. The
panel here is 240 x 240, so the plan keeps 224 px of playfield width at x=8,
draws every sprite at 1:1 (no `SPRITE_SCALE`), and re-pitches the vertical
layout into 240 px: score row 0-15, saucer row y=24, rack from y=48, shields
at y=184, cannon baseline y=216, reserve ships y=224. The 16 px lost against
the original are the empty bottom margin, not game space.

The original is monochrome. Colour comes from cellophane strips over the tube:
red across the top, green across the bottom, clear in the middle. The plan
reproduces that by keeping every mask white and choosing the draw colour from
the sprite's screen band.

### The rack

55 aliens: 5 rows x 11 columns. Row 1 is the squid (30 points), rows 2-3 the
crab (20), rows 4-5 the octopus (10). Horizontal pitch 16 px, vertical pitch
16 px, so the formation is 172 px wide and starts 26 px into the field.

Movement is not a timer. The CPU moves exactly one live alien per 60 Hz
interrupt; when the sweep has visited every cell, the whole formation has
shifted 2 px and the animation frame flips. The observable consequences are
the ones that matter:

- the shift interval equals the number of live aliens, so the rack accelerates
  by itself as aliens die, from about 2 px per 0.9 s down to 2 px per tick;
- the last alien shifts 3 px per sweep, not 2;
- on the edge the direction reverses and the rack drops 8 px;
- the rack holds while an alien explosion is on screen;
- the animation frame toggles once per sweep, not once per moved alien.

The march sound is deliberately not tied to that clock. The original has its
own beat table, indexed by live count, and the tones audibly drift out of step
with the movement. Reproducing the drift is part of the fidelity, not a bug.

### The cannon and its shot

13x8, continuous, 1 px per tick, clamped to the playfield. No lanes, no
interpolation. One shot alive at a time, 1x4, 4 px per tick, fired on a fresh
press after the previous shot leaves the screen.

### Enemy fire

Three shots alive at most, 3x7 with two animation frames, 4 px per 3 ticks
(5 px per 3 ticks once fewer than 9 aliens remain). The shooter is the lowest
alien in a randomly chosen live column. The reload delay is a shot-step budget
selected by the score high byte; the ROM table is `30 10 0B 08 07` at `0x1AA1`
with the thresholds `02 10 20 30` at `0x1CB8`, which reads as a slow 48-step
reload at the start of a run tightening to 7 steps at a high score. P4 confirms
the index order against the disassembly before writing it.

### Shields

Four 22x16 destructible masks, 3 bytes per row, with the bottom-centre arch.
Both shot types erode the mask where they hit; a shield that loses every bit
disappears. Shields are rebuilt at the start of each wave.

### The saucer

16x7, white, crosses the top of the field 2 px per tick. It appears when at
least 8 aliens remain, on a 1536-tick timer, on a side chosen by the parity of
the shot counter rather than at random. Its score comes from a 15-entry table
that wraps: 100, 50, 50, 100, 150, 100, 100, 50, 300, 100, 100, 100, 50, 150,
100. A siren plays while it is on screen; hitting it plays a separate jingle
while the score is shown.

### Score, lives, waves

Four score digits, a high score, three ships, one extra ship at 1500. The HUD
is `SCORE<1>`, `HI-SCORE`, `SCORE<2>` on the top row with the reserve ships as
cannon icons at the bottom left. Clearing the rack does not end the run: the
next wave rebuilds the shields and restarts the formation 8 px lower, and the
drops accumulate until the aliens reach the cannon row, which ends the run
immediately. There is no win state, no win jingle and no game-over jingle.

### Sound

The arcade has a dedicated analog board, not a music sequencer: a four-tone
march circuit with its own beat clock, a shot circuit, an alien-killed circuit,
a player-killed circuit, and a saucer siren plus hit circuit. The plan maps
that onto the engine's 4 SFX voices, re-firing one short tone per beat and
short sweeps for the siren, because there is no clean per-channel stop.

### Sprite inventory

| Sprite | Size | Frames |
|---|---|---|
| Squid | 8x8 | 2 |
| Crab | 11x8 | 2 |
| Octopus | 12x8 | 2 |
| Saucer | 16x7 | 1 |
| Cannon | 13x8 | 1 |
| Player shot | 1x4 | 1 |
| Enemy shot | 3x7 | 2 |
| Explosion | 13x8 | 1 |
| Shield | 22x16 | mask |
| Font | 8x8 | A-Z, 0-9 |

The pixel masks are transcribed in P2 from the reference sheet supplied with
the request. P2 also settles the one open question there, the explosion: the
sheet's panels are compared against the arcade capture and the plan uses one
shared burst for an alien kill and the player death unless the capture shows
two distinct masks.

## Findings

**F1 (high) - enemy fire maps aliens to columns with a fixed grid.**
`enemyShoot()` matches aliens to columns with `std::abs(ax - colX) < 5.0f`
against the initial grid (`SpaceInvadersScene.cpp:665-668`,
`GameConstants.h:71,79`). The rack steps 2.5 px, so the first step still
matches, the second lands exactly on the 5.0 boundary and no column matches at
all, and as the offset approaches 17 px the match aliases to the neighbouring
column. Fire is therefore intermittent, and for most of the rack's travel the
"lowest alien in this column" is computed over the wrong grouping. The column
identity must come from the rack's grid, not from a float comparison against
where the formation started.

**F2 (high) - respawn leaves the lane state stale.**
`respawnPlayerUnderBunker()` writes only `player->position.x`
(`SpaceInvadersScene.cpp:882-889`). `currentPosition`, `targetPosition`,
`startX` and `isMoving` keep their old values, so the next input either slides
the ship back or snaps it to a stale target (`PlayerActor.cpp:56-67,77-91`).

**F3 (high) - bunker damage is cosmetic.**
`BunkerActor::draw()` shrinks the drawn rectangle with health
(`BunkerActor.cpp:15-34`) but the hitbox comes from the inherited
`StaticActor` full 24x16 box, and nothing overrides it. The README claim that
"the hitbox shrinks with it" (`README.md:126-128`) is false.

**F4 (high) - the arena can silently drop entities.**
The buffer is 4096 bytes (`SpaceInvadersScene.cpp:37-42`) and every failed
`arenaNew` is skipped with a bare `continue` (`:306-312`, `:350-355`,
`:377-383`). Today the scene holds about 50 entities (32 aliens, the ship, the
starfield, 4 bunkers, 12 pooled projectiles); the reference formation alone
needs 55 aliens, so the count rises to about 68, and the 4 KB is likely to run
out. Whatever the new budget is, it must be measured and asserted, not assumed.

**F5 (high) - clearing the rack ends the game.**
`getActiveAlienCount() == 0` sets `gameOver` and `gameWon`
(`SpaceInvadersScene.cpp:569-574`), and the scene draws `YOU WIN!`
(`:719-723`). The arcade has no win state.

**F6 (medium) - the formation is 4x8, not 5x11.**
`GameConstants.h:46-47` and `SpaceInvadersScene.cpp:338-363`. 32 aliens against
55, and row 3 is a single octopus row instead of two.

**F7 (medium) - march speed is a timer coupled to the music.**
`stepDelay` is a fixed 417 ms (`GameConstants.h:75-79`) and both the step timer
and the music are scaled by `currentMusicTempoFactor`
(`SpaceInvadersScene.cpp:461-465,736-761`). Kills do not speed the rack up, and
sound and movement cannot drift, which is the opposite of the original.

**F8 (medium) - enemy fire has no rate model.**
`enemyShoot()` is called once per formation step (`SpaceInvadersScene.cpp:501`)
and rolls a random column (`:683`). There is no three-shot cap (the pool holds
12 but nothing limits enemy shots), no reload table and no acceleration.

**F9 (medium) - the logic is frame-rate dependent.**
Everything runs off `deltaTime` (`SpaceInvadersScene.cpp:391-458,460-519`) and
the projectiles integrate in px/s (`ProjectileActor.cpp:32-41`). On a slow
frame the rack advances a whole step and the projectiles travel further; there
is no fixed tick to reproduce arcade positions or to test.

**F10 (medium) - the player shot is far too slow.**
`PROJECTILE_SPEED = 80.0f` px/s (`GameConstants.h:37`), against 4 px per 60 Hz
tick in the original. Crossing this field takes about 2.8 s instead of 1 s.

**F11 (medium) - the enemy shot is a flat bar.**
`ProjectileActor::draw()` fills a 1x4 orange rectangle
(`ProjectileActor.cpp:43-51`). The original is a 3x7 two-frame squiggle, which
is also its only animated sprite on the field.

**F12 (medium) - there is no saucer.**
No constant, no spawn, no state: `GameConstants.h` has no saucer entry and the
scene never references one. This is the single largest missing system.

**F13 (medium) - shields are health bars, not walls.**
`GameConstants.h:39-43` fixes 24x16 and `BunkerActor` treats damage as a single
health integer (`BunkerActor.cpp:40-49`). There is no per-pixel erosion, no
arch, and no rebuild on a new wave.

**F14 (medium) - no extra ship, no high score, ships drawn as a number.**
`lives` is only decremented (`SpaceInvadersScene.cpp:840-867`) and printed as
`LIVES %d` (`:716-717`). Nothing awards the 1500-point ship.

**F15 (medium) - the march is four identical notes.**
`BGM_SLOW_NOTES` and its two faster variants are four C notes each
(`SpaceInvadersScene.cpp:66-124`) played as a loop, and their only variation is
the tempo factor. The original cycles four different tones once per beat.

**F16 (medium) - the win and game-over jingles are inventions.**
`WIN_TRACK` and `GAME_OVER_TRACK` (`SpaceInvadersScene.cpp:126-157`) play at
`:512-513`, `:572-573` and `:853-857`. The arcade has neither.

**F17 (medium) - player death freezes the world and respawns under a bunker.**
`isPaused` short-circuits `Scene::update()` (`SpaceInvadersScene.cpp:402-411`)
and the respawn centres the ship under the first surviving bunker
(`:874-887`). The rack does hold in the original, but the ship returns at the
left edge and the hold is a fixed count, not the animation's length.

**F18 (medium) - sprites are upscaled and flat-coloured.**
`SPRITE_SCALE = 1.25f` (`GameConstants.h:21`) makes an 8x8 alien 10x10 with
nearest-neighbour and fractional origins; aliens draw orange
(`AlienActor.cpp:82`) and the ship yellow (`PlayerActor.cpp:157-162`). No
monochrome masks, no overlay bands.

**F19 (medium) - the crab never animates.**
`CRAB_ANIM_FRAMES` holds a single entry (`AlienActor.cpp:16-18`), so the middle
rows have no second frame while the other two types do.

**F20 (low) - the HUD uses the default proportional font.**
`SpaceInvadersScene.cpp:711-733` calls `drawText` with no `Font*`, and the
reference 8x8 font is not in the tree. The HUD reads as a modern overlay.

**F21 (low) - the starfield is an invention.**
72 stars (`Background.cpp:6-21`) drawn over black
(`SpaceInvadersScene.cpp:56-63`). The arcade field is black. It is also one
more entity in the arena budget.

**F22 (low) - dead constants and stale fields.**
`PLAYER_SPEED` (`GameConstants.h:30`), `INITIAL_STEP_DELAY` and
`MIN_STEP_DELAY` (`:76-77`) are never read. `GAME_OVER_Y`, `Y_RANGE` and
`INV_Y_RANGE` (`:82-84`) exist only to feed the tempo factor.

**F23 (low) - documentation drift.**
The shrinking hitbox in `README.md:126-128` and `BunkerActor.h:27-30`,
"bottom to top" in `BunkerActor.h:57-63` against a bottom-up shrink in the
code, and "120 px/s" in `ProjectileActor.h:31` against 80 px/s in
`GameConstants.h:37`.

**F24 (low) - hit resolution picks the first alien in vector order.**
The collision loop breaks on the first alien whose box intersects the sweep
(`SpaceInvadersScene.cpp:545-579`) instead of the nearest hit along the path,
so a fast shot can kill an alien behind another.

**F25 (low) - randomness is not injectable.**
`std::rand()` (`SpaceInvadersScene.cpp:683`) is unseeded and shared with the
rest of the process, so a test cannot reproduce a shot sequence.

**F26 (low) - none of the rules can be tested.**
There is no `host_test` environment (`platformio.ini:14-60`), and every rule
lives inside a scene that reaches for the engine singleton
(`SpaceInvadersScene.cpp:17`). The reference numbers above cannot be pinned by
a test today.

## Decisions

Status is `open` when the choice is yours to make and `proposed` when the plan
already commits to it. Only D1 and D2 need an answer before P2 and P3.

**D1 (open) - art provenance.**
The sheet supplied with the request contains the arcade masks. Reproducing them
pixel for pixel would make the README's CC0 claim (`README.md:20-27`) false and
turn the demo into a redistribution of Taito artwork. The plan recommends
redrawing each mask against the sheet: same silhouette, same frame count, same
proportions, hand-drawn `uint16_t` rows in this repository, and the README
rewritten to say exactly that. The alternative is to import the masks and
replace the CC0 claim with a descriptive-use notice. This is a licensing call,
not a technical one.

**D2 (open) - cannon movement.**
Continuous, 1 px per tick, clamped to the field, or keep the nine lanes. The
plan recommends continuous: it is the original, it removes the stale-state
respawn bug (F2) by construction, and aiming at a 2 px-wide column needs the
finer step. Keeping lanes is defensible if the lane feel is a deliberate part
of this demo.

**D3 (proposed) - screen model.**
224 x 240, origin x=8, every sprite 1:1, `SPRITE_SCALE` removed. Vertical
layout: score row at 0, saucer at 24, rack from 48, shields at 184, cannon
baseline 216, reserve icons 224.

**D4 (proposed) - rack model.**
Adopt the reference sweep: one live alien per tick, 2 px, 3 px when one alien
remains, 8 px drop on the edge, one animation flip per completed sweep, hold
while an explosion is on screen.

**D5 (proposed) - march clock.**
Decouple the sound from the movement. Movement interval is the live alien
count; the beat interval is its own table (S16). The two are allowed to drift.

**D6 (proposed) - shields.**
22x16 bitmasks, 3 bytes per row, erode on impact, rebuilt per wave, drawn with
`Renderer::drawBitmap`.

**D7 (proposed) - enemy fire.**
Three slots, the lowest alien of a live column, the score-indexed reload
ladder, 4/5 px per 3 ticks. Deterministic RNG behind a one-function interface.

**D8 (proposed) - saucer.**
1536-tick timer, at least 8 aliens alive, 2 px per tick, direction by shot
parity, the 15-value score table with its wrap, siren while on screen.

**D9 (proposed) - audio scope.**
March, shot, alien kill, player death, saucer siren, saucer hit. No win or
game-over music, no extra-ship jingle.

**D10 (proposed) - waves and score.**
No win state. Wave advance rebuilds the shields and restarts the formation
8 px lower. One extra ship at 1500. High score in RAM for the session.

## Specs

**S1 - Screen.** Playfield 224 px wide at x=8, 1:1 pixels, no scaling anywhere.
HUD band y=0-15, saucer row y=24, rack top y=48, shields y=184, cannon
baseline y=216, reserve icons y=224.

**S2 - Sprites.** Exactly the inventory in the table above; every mask 1bpp,
one bit per pixel, rows as `uint16_t` or `uint8_t`, no `MultiSprite`.

**S3 - Colour bands.** All masks draw white except by band: red for y < 32,
green for y >= 184, white between. The HUD stays white.

**S4 - Formation.** 5 rows x 11 columns, 16 px x 16 px pitch, row 1 squid,
rows 2-3 crab, rows 4-5 octopus, 30/20/10 points, top-left at (26, 48) in
field coordinates and 8 px lower per wave.

**S5 - Rack movement.** One live alien moves per tick. On sweep completion the
frame flips. 2 px per sweep, 3 px when a single alien remains. Reversing on the
edge drops the rack 8 px: the left limit is a left edge at x=2 and the right
limit a right edge at x=222, both checked against the outermost live sprite.
The rack holds for the duration of an alien explosion.

**S6 - March rate.** The sweep length is the live alien count, computed each
tick from the rack, not cached from a timer.

**S7 - Cannon.** 13x8, 1 px per tick, continuous, clamped to x=0..211.
No easing, no auto-repeat, no lanes.

**S8 - Player shot.** 1x4, spawns at the cannon centre, 4 px per tick, one
alive. A new shot requires a press after the previous one ended.

**S9 - Enemy shots.** At most 3. 3x7 with two frames alternating as it moves.
4 px per 3 ticks, 5 px per 3 ticks under 9 aliens. Shooter is the lowest alien
of a randomly chosen live column. Reload is a shot-step budget indexed by the
score high byte against the ROM ladder (48, 16, 11, 8, 7 at thresholds
0x02, 0x10, 0x20, 0x30), confirmed in P4 before implementation.

**S10 - Shields.** Four masks, 22x16, at x=32, 77, 122, 167 and y=184. A hit
erodes a small blast centred on the impact cell, clipped to the mask. A shield
with no bits left is gone. Rebuilt at the start of each wave.

**S11 - Saucer.** 16x7, white, y=24. Spawns when at least 8 aliens are alive
and the 1536-tick timer expires; enters from the left or right by shot-counter
parity; 2 px per tick; the score is the next entry of the 15-value table; the
siren plays while it is on screen; hitting it shows the score where it died.

**S12 - Score, lives, waves.** Four digits, high score in RAM, three ships, one
extra ship at 1500. `SCORE<1>`, `HI-SCORE`, `SCORE<2>` at the top, reserve
ships as cannon icons at the bottom left. Clearing the rack advances the wave.

**S13 - Invasion.** The run ends immediately when any alien's bottom reaches
the cannon baseline, and when the last ship is lost.

**S14 - Player death.** The cannon is replaced by the explosion mask, all shots
in flight are cleared, the rack holds for the explosion's fixed count, then the
cannon returns at the left edge of the field. The wave continues where it
stopped. The hold count is measured against the reference capture in P4.

**S15 - Fixed step.** Logic runs on a 60 Hz accumulator: one rules tick per
16.67 ms of real time, with a bounded catch-up (at most 5 ticks per frame, the
rest dropped) so a stall cannot teleport the rack. Rendering draws the state
after the last tick.

**S16 - March audio.** Four descending tones cycled 1-2-3-4, one short pulse
per beat, interval table by live count: 52, 46, 39, 34, 28, 24, 21, 19, 16,
14, 13, 12, 11, 9, 7, 5 ticks for 50, 43, 36, 28, 22, 17, 13, 10, 8, 7, 6, 5,
4, 3, 2, 1 aliens; 52 above 50. The four pitches are transcribed from a
reference recording in P5.

**S17 - Effects.** Player shot, alien kill, player death, saucer siren
(re-fired sweeps), saucer hit. No win, game-over or extra-ship sound.

**S18 - Font.** 8x8 uppercase A-Z, 0-9 plus `<`, `>`, `-`, `:` and space, as a
`pixelroot32::graphics::Font` descriptor; the HUD, `PRESS FIRE` and the
game-over text all use it.

**S19 - Memory.** The arena keeps its 4 KB budget or grows with a measured,
stated number; `arenaNew` failures are counted and asserted zero in a full
wave; the projectile pool keeps 12 slots. The starfield asset is removed.

**S20 - Determinism.** All rules run on the tick, not on `deltaTime`, and the
RNG is a seeded generator passed in as a function pointer or interface. The
same seed and input script reproduce the same shot columns and saucer sides.

**S21 - Testability.** `src/rules/` contains no engine header and no singleton.
A new `host_test` environment compiles and tests it with Unity, mirroring
`games/minesweeper`.

**S22 - Documentation.** `README.md` describes the rules as implemented, the
new art provenance, the audio model and the memory budget. This document keeps
the findings and the phase tables until the last phase ships.

## Phases

Each phase is a work unit with its own commit, on
`feature/space-invaders-fidelity`. Line estimates are the planning heuristic,
not a cap.

### P1 - Harness and engine-free rules (commit C1)

| # | Task | Spec | Route | Done when |
|---|---|---|---|---|
| T1.1 | Add `[env:host_test]` (Unity, `-Isrc`, `build_src_filter = +<rules/>`) mirroring `games/minesweeper/platformio.ini` | S21 | inline | `pio test -e host_test` runs a sanity suite |
| T1.2 | Write the RED suite for the reference numbers: 55 cells, sweep length, 3 px last alien, 8 px drop, reload ladder, 15-value saucer table, shield erosion, 1500-point ship, wave offset | S4-S6, S9-S12 | inline | the suite fails on assertions, not on compile errors |
| T1.3 | `src/rules/`: `Rack`, `RackTiming`, `FireTiming`, `Saucer`, `ShieldMask`, `Rng` | S20 | inline | RED turns GREEN under `host_test` |

Estimate: 450-600 lines, mostly tests.

### P2 - Screen, art, font (commit C2)

| # | Task | Spec | Route | Done when |
|---|---|---|---|---|
| T2.1 | Layout constants: field origin, rows, remove `SPRITE_SCALE` and the derived sizes | S1 | inline | the native build draws at 1:1 |
| T2.2 | Masks from the reference sheet: squid, crab (both frames), octopus, saucer, cannon, both shots, explosion, shields, font | S2 | inline | every sprite matches the sheet's silhouette at 1x |
| T2.3 | `Font` descriptor and HUD switch | S18 | inline | the HUD renders in 8x8 |
| T2.4 | Band colouring | S3 | inline | a still shows red top, white middle, green bottom |
| T2.5 | Remove the starfield and `Background` | S19 | inline | no star pixels in a still |
| T2.6 | README art and provenance rewrite | S22 | inline | the CC0 claim matches what actually shipped |

Estimate: 500-700 lines, dominated by mask tables. Deliberately art only: it
is reviewable by eye and has no rule risk.

### P3 - Rack, cannon, shots, clock (commit C3)

| # | Task | Spec | Route | Done when |
|---|---|---|---|---|
| T3.1 | 60 Hz accumulator, bounded catch-up | S15 | inline | a scripted 120-tick run yields identical state at 30 and 60 fps |
| T3.2 | Wire `Rack`: 5x11 spawn, sweep cursor, 2/3 px, 8 px drop, frame flip, explosion hold | S4-S6 | inline | host tests for the rack pass and the native build matches the tick count |
| T3.3 | Cannon: continuous 1 px per tick, clamp, remove lanes, remove the lane respawn | S7, D2 | inline | input steps the ship one pixel per tick |
| T3.4 | Player shot: 1x4 at 4 px per tick, one alive | S8 | inline | the shot crosses the field in about 60 ticks |
| T3.5 | Delete the dead constants and the fields only the old timer used | - | inline | no unused symbol |

### P4 - Enemy fire, shields, saucer, scoring (commit C4)

| # | Task | Spec | Route | Done when |
|---|---|---|---|---|
| T4.1 | Enemy shots: 3 slots, live-column choice, reload ladder, 3x7 two frames, speed change under 9 aliens | S9 | inline | the fire test drives 60 s and the shot gaps follow the ladder |
| T4.2 | Shields: masks, `drawBitmap`, erosion, rebuild | S10 | inline | a bullet carves a visible hole and the hole blocks the next bullet |
| T4.3 | Saucer: timer, parity, movement, score table, siren and hit hooks | S11 | inline | the saucer test reproduces the 15-entry sequence |
| T4.4 | Score, extra ship, high score, waves, invasion ending | S12, S13 | inline | clearing the rack starts wave 2, 8 px lower, shields rebuilt |
| T4.5 | Player death: explosion, clear shots, fixed hold, left-edge respawn | S14 | inline | no stale lane state on the next input |

### P5 - Audio (commit C5)

| # | Task | Spec | Route | Done when |
|---|---|---|---|---|
| T5.1 | March: one-shot tone per beat, 4-tone cycle, interval table | S16 | inline | the beat speeds up with kills and does not track the rack exactly |
| T5.2 | Effects: shot, alien kill, player death, siren, saucer hit | S17 | inline | each effect has its own voice and never cuts the march |
| T5.3 | Remove the tempo factor, the three BGM tracks and the two jingles | S16, S17 | inline | `MusicPlayer` is no longer used by this scene |

### P6 - Verification and hardware (commit C6)

| # | Task | Spec | Route | Done when |
|---|---|---|---|---|
| T6.1 | Full `host_test` suite plus the native differential run | S15, S20 | inline | green, and the same seed reproduces the same run |
| T6.2 | Arena measurement: entity count, bytes used, zero failed allocations across a full wave | S19 | inline | numbers recorded in this document |
| T6.3 | ESP32 build and a full run on the panel | S15 | inline | 60 Hz holds, no dropped frames, audio clean |
| T6.4 | Capture comparison: stills at ticks 0, 60, 600, 1200 against the reference | all | inline | each F1-F26 marked fixed or deferred |

### P7 - Documentation and close (commit C7)

| # | Task | Spec | Route | Done when |
|---|---|---|---|---|
| T7.1 | README: controls, rules, audio, memory, provenance, sources | S22 | inline | a reader can explain the game from the README |
| T7.2 | This document: phases marked shipped, findings closed with the commit that closed them | S22 | inline | no open finding without a decision |

## Verification

- **Rules.** `cd games/space_invaders && pio test -e host_test`. The suite
  covers every number in S4-S13 and S16-S17 that is a number: sweep length,
  shift, drop, reload ladder, saucer sequence, shield erosion, extra ship,
  wave offset, march intervals.
- **Determinism.** One test replays a fixed input script and compares the
  saucer sides and shot columns against a recorded expectation.
- **Builds.** `pio run -e native` and `pio run -e esp32dev`.
- **Visual.** A reference recording at 60 fps, compared as stills at fixed
  ticks. The checklist is F1-F26.
- **Memory.** The arena high-water mark printed on native, plus a counter of
  failed `arenaNew` calls that a full wave must leave at zero.
- **Regression.** Anything the request did not ask to change keeps its old
  behaviour; where a shared path is touched (the HUD text call, the projectile
  pool), the old path gets a test before the new one lands.

## Delivery

Forecast: 1,800-2,400 authored lines across the seven phases, over the
400-line review heuristic. Proposed chain, one PR per slice:

1. P1 - harness and rules, with the failing-then-passing tests.
2. P2 - art, font and screen, reviewed by eye.
3. P3 - rack, cannon and clock.
4. P4 - fire, shields, saucer, scoring.
5. P5 - audio.
6. P6 and P7 - verification, hardware, docs.

`ask-on-risk` stays the strategy: the chain is the recommendation, not an
automatic push. No push, PR or merge happens without an explicit go-ahead.

## Out of scope

- Two-player mode, the second score's real use, and the cabinet's credit
  handling.
- Persistent high score in NVS.
- Attract mode and the demo loop.
- The 256-px original field and any vertical scaling to fake it.
- The analog SN76477 sound board; the engine's synth is the medium.
- Extracting data from the ROM images. The masks are redrawn from the sheet
  under D1.
- Touch input and the ESP32 touch UI.

## Sources

- Computer Archaeology, *Space Invaders* - code walkthrough and disassembly
  (rack reference-alien loop, `rackDownDelta` 8, one-alien dx 3, shield bytes,
  `tillSaucer` 0x0600, extra ship 1500, `alienShotDelta` -4/-5, reload table
  `0x1AA1 = 30 10 0B 08 07`, thresholds `0x1CB8 = 02 10 20 30`, saucer scores
  at `0x1D54`, the 15-entry wrap):
  <http://computerarcheology.com/Arcade/SpaceInvaders/>
- Computer Archaeology, *Space Invaders* RAM map (one live alien per
  interrupt, formation state, shot slots).
- walkofmind, *Space Invaders sound hardware* (eight sound circuits, the
  march's port-5 bit rotation): <http://www.walkofmind.com/programming/space.htm>
- Righto / Ken Shirriff, notes on the SN76477 and the analog sound board.
- 4Gamer, interview with the original arcade team (Japanese), for the
  cellophane overlay and the sound-board behaviour.
- Ron Jeffries' march beat table, as reproduced in the sound sources above.
- The reference sheet supplied with the request (`ORIG.` and `COLOR` panels).
- PixelRoot32 engine 1.12.0 headers: `graphics/Renderer.h` (`drawBitmap` at
  :1017, `drawText` with `const Font*` at :929), `graphics/Font.h`,
  `audio/AudioTypes.h` (eight voices, sweep and envelope fields).
