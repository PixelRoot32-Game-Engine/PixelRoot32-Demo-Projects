# DOOM (PixelRoot32 port)

Shareware DOOM (1993), episode 1, running on the CYD — the ESP32-2432S028
"Cheap Yellow Display": an ILI9341 240x320 panel mounted in portrait and rotated
a quarter turn to a 320x240 frame, 240 MHz dual core, 4 MB flash, 327,680 B of
DRAM, no PSRAM. All nine levels of E1 play with the status bar, the weapon, the
menu, the automap, sound effects, music, level transitions and six save slots.

The engine supplies what a 1993 renderer does not need to own again: an 8bpp
surface, the 8bpp-to-panel present path, input, persistence and the APU audio
backend. Everything above that line — the BSP renderer, the game loop, the
fixed-point math, the WAD reader — is DOOM's own code in [`lib/doom/`](lib/doom/),
kept at arm's length from the engine.

Language: C++17 (+ C99 for the DOOM core)  
Engine: `PixelRoot32-Game-Engine` branch `feature/doom-esp32` (referenced by
branch in `platformio.ini`, not by a released version)  
Platform: `espressif32@7.1.2` (Arduino-ESP32 2.0.17), pinned in
[`lib/platformio.ini`](lib/platformio.ini)  
Shipped environment: `esp32dev_ILI9341_hud`  
Category: Games

## Quick path

1. Build the firmware: `pio run -e esp32dev_ILI9341_hud` (from `games/doom`).
2. Get the shareware `DOOM1.WAD` v1.9 and build the container:
   `.\tools\make_container.ps1 -Wad C:\path\to\DOOM1.WAD -Flash -Port COM4`.
3. Flash the app: `pio run -e esp32dev_ILI9341_hud -t upload --upload-port COM4`.
4. Open the serial monitor at 115200 and look for
   `SHOW,boot,screen=game,level=E1M1,open_ms=...`.

Each step is explained below.

## The one constraint that shapes everything

DOOM renders **320x200**, and 320 is structural rather than a setting:
`SCREENWIDTH` sizes `distscale[]`, every `visplane_t` (`top[320]` /
`bottom[320]`), `floorclip` / `ceilingclip` and the status bar layout. So the
logical surface is fixed at 320x200 on every environment.

The rotated CYD frame is 320x240, so the surface fits **1:1** with a 20 px bar
above and below — no scaling, no crop.

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
pio run -e esp32dev_ILI9341_hud
```

If `pio` is not on `PATH`, use the PlatformIO venv's copy
(`%USERPROFILE%\.platformio\penv\Scripts\pio.exe` on Windows).

The native build needs SDL2 (`libsdl2-dev` on Linux, `brew install sdl2` on
macOS, MSYS2/MinGW on Windows). The `[env:native]` flags ship with the
MSYS2 paths; swap them for the commented Homebrew lines on macOS.

```bash
pio run -e esp32dev_ILI9341_hud --target upload
pio device monitor
```

`esp32dev_ILI9341_hud` extends `esp32dev_ILI9341_show`, which extends
`esp32dev_ILI9341`; the `_show` and `_hud` families select
[`partitions_wad.csv`](partitions_wad.csv), the plain `esp32dev_ILI9341` does
not. Every other `esp32dev_ILI9341_*` environment is a diagnostic or a
measurement build and is never the shipped image.

**Do not run `pio pkg update` on this project.** The platform pin is
load-bearing: an unpinned update moves `espressif32@7.1.2` (Arduino-ESP32
2.0.17) to `8.0.0` (Arduino 3.x), and the same sources then measure 686,446 B of
flash and overflow `dram0_0_seg` by 680 B. Bumping the platform is a
repartition decision, not a routine update. The host test environments
`link_test` and `sound_test` name `gperez88/PixelRoot32-APU@^2.0.0` explicitly
(it resolves to 2.2.0); the firmware gets the APU through the engine's own
dependency.

### What the engine branch carries

The port needs three things the released engine does not have: a present path
whose frame transfer can be closed before the next frame (the 20-line DMA
block, `PIXELROOT32_DRAIN_PRESENT`), a caller-supplied 8bpp expansion table (the
PLAYPAL path), and the persistence module (`PIXELROOT32_ENABLE_PERSISTENCE`: the
key-value and slot stores, the `RecordCodec` envelope and the ESP32 NVS
adapters). The `feature/doom-esp32` branch also contains the APU
hybrid-synthesis work. The engine contains no DOOM source; the GPL boundary is
this demo's binary, not the engine tree (see [License](#license)).

## Get the WAD and build the container

The firmware does not ship DOOM's data. `DOOM1.WAD` is id Software's shareware
artwork under its own terms, not the GPL, and it is 4,196,020 B -- larger than
the board's entire flash. Download the shareware WAD from the Doom Archive's
Shareware page:

<https://www.doomarchive.com/ListFiles.php?FolderId=216&ContentsFolderId=216>

Take **v1.9** -- `doom19s.zip`, or `doom1.wad.zip` for the WAD on its own. The
v1.0 release listed on the same page is a different WAD: the port's container,
partition sizes and graphics bake are all measured against v1.9, so a v1.0 file
will not match them.

Then turn it into the container the firmware reads, from **`games/doom`**:

```powershell
.\tools\make_container.ps1 -Wad C:\path\to\DOOM1.WAD
```

It writes `tools/DOOM1.whd` -- 3,078,396 B of the `wad` partition's 3,375,104 --
and proves the result four ways: the WAD rebuilds byte for byte, the container
parses, all nine levels open through `lib/doom/doom_map.c`, and every baked
texture recomposes from the source WAD's patches. A fifth gate, `tools/hud_check.py`,
proves the 86 status-bar patches listed in `tools/hud_lumps.txt` (generated by
`tools/gen_hud_lumps.py`) are stored raw and equal their source patches; they are
kept raw by name, not registered in the bake manifest, so they cost no RAM. The
same holds for the weapon (psprite) art: `tools/weapon_check.py` proves the 28
patches of `tools/weapon_lumps.txt` (generated by `tools/gen_weapon_lumps.py` from
`weaponinfo[]` and the state table), and for the menu: `tools/menu_check.py
--container` proves the 17 menu patches and the HU font of `tools/menu_lumps.txt`
(generated by `tools/gen_menu_lumps.py`). The encode also reads
`kBakedCapacity` out of `src/doomc/BakedGraphics.h` and refuses a bake whose
manifest holds more records than the firmware can: that budget is separate from
the flash one, and a container past it is rejected whole at boot. The baker
stores a mask plane only for textures `TEXTURE1` marks as masked; an opaque
texture's patch gaps are palette 0, which vanilla draws as black.

`-Flash` writes the container to the board at `0x0C0000` after building it:

```powershell
.\tools\make_container.ps1 -Wad C:\path\to\DOOM1.WAD -Flash -Port COM4
```

The encoder underneath is committed and usable on its own
(`python tools/whd_encode.py --help`; `--check` re-decodes a container and
compares it with the WAD it came from).

Neither `DOOM1.WAD` nor any `.whd` is committed: `tools/.gitignore` refuses both,
because a container is a repacked copy of shareware data.

### Flashing the board

The board needs both halves: the container in the `wad` partition, and the app
image built by an environment that selects `partitions_wad.csv`. The table:

```text
nvs       0x009000 .. 0x00F000        24 KB    settings record
factory   0x010000 .. 0x0C0000       720,896 B   the app
wad       0x0C0000 .. 0x3F8000     3,375,104 B   the container
saves     0x3F8000 .. 0x400000        32 KB    six save slots
```

Flashing this table over a different one writes NVS, so the menu's settings
record can be lost; the save slots at `0x3F8000` are untouched. Reflashing the
*same* table keeps whatever is there, with one caveat: an upload always writes
`boot_app0.bin` at `0xE000`, the last page of `nvs` (see "Saves" below).

A freshly flashed board with `nvs` and `saves` erased boots on the defaults,
with sound on and six `EMPTY` slots:

```text
SHOW,persist,status=ok,settings=1,saves=1,slots=6,record=3968
SHOW,menu,settings=default,skill=2,volume=8,gamma=0,mute=0,level=67
SHOW,boot,screen=game,level=E1M1,open_ms=199
SHOW,frame00 ... hash=1d8b3849ab3f76750056937f2827bf2bcbb2b98e82976fb3a7ce263d5273c9d3
```

The `frame00` hash is the first world frame of the shipped image; a change that
moves it changed what E1M1's spawn view looks like.

## Hardware

The CYD is an `esp32dev` module: 240 MHz, dual core, 4 MB flash, no PSRAM. The
pin map is copied from the pong demo, which is where it was validated; demos in
this repository do not import each other's code.

| Function | GPIO |
| --- | --- |
| TFT MOSI / SCLK / DC / RST / CS | 23 / 14 / 2 / 4 / 15 |
| Backlight | 16 (`TFT_BL`, active high) |
| SPI clock | 40 MHz (`SPI_FREQUENCY`) |
| I2S BCLK / LRCK / DOUT | 5 / 21 / 19, 22050 Hz |
| D-pad up / down / left / right | 32 / 33 / 25 / 26 |
| A / B | 27 / 13 |
| Start / Select | 34 / 35 (external 10 kOhm pull-ups) |
| Touch | XPT2046 present, bus not claimed |

`PIXELROOT32_TFT_ESPI_LINES_PER_BLOCK` is pinned to **20** in
[`lib/platformio.ini`](lib/platformio.ini): the drawer allocates two DMA line
buffers of `physicalWidth * 2 B * linesPerBlock` each, so 60 lines at 320 px
costs 76,800 B against the 25,600 B a 20-line block costs — 51,200 B that the
port needs elsewhere. A 20-line block only renders cleanly with
`PIXELROOT32_DRAIN_PRESENT` defined, which closes each frame's transfer instead
of leaving its tail in flight into the next frame.

**Wiring note for GPIO 34 and GPIO 35.** The engine's input layer
(`InputManager::init()`) calls `pinMode(pin, INPUT_PULLUP)` on every configured
pin and reads a button as pressed when `digitalRead() == LOW` (active low, with a
100 ms debounce). GPIO 34 and GPIO 35 are both input-only on the ESP32 and have
**no internal pull-up or pull-down**, so `INPUT_PULLUP` does nothing there: each
button needs an **external pull-up** (10 kOhm from the pin to 3V3, switch to GND).
`analogRead()` must not be used on those pins. A cold joint on GPIO 35's
resistor shows up as short low pulses and spurious automap toggles; reflow the
joint, and a 100 nF from the pin to GND filters the pulses if they persist.

## What the shipped image runs

### The status bar

`esp32dev_ILI9341_hud` is `_show` plus `-D DOOM_HUD=1`: the 3D view shrinks to
DOOM's screen size 9 (320x168, `centery` 84) and `lib/doom/st_stuff.c` repaints
the bottom 32 rows every frame from the player the world already owns (ammo,
health, armor, the arms digits, the three key boxes and the face). `_show` keeps
the 200-row view whose frame-00 hash is `50fb3d9c...`; the HUD frame has its own,
`1d8b3849...`. `esp32dev_ILI9341_hud_nofaces` is the CYD fall-back (the bar
without the face widget), kept for the case the full bar misses its frame rate.
`lib/doom/r_psprite.c` draws the weapon in hand over the view and under the bar
(`R_DrawPlayerSprites`: the weapon and its muzzle flash, lit by the player's
sector, full bright for the flash frames); its patches are the raw lumps of
`tools/weapon_lumps.txt`. `esp32dev_ILI9341_hud_noweapon` is the same build
without that call, kept only to measure what the weapon costs. Not drawn: the
invisibility fuzz and the deathmatch frags (the palette flashes are below).
`python tools/hud_draw_check.py --wad DOOM1.WAD` and
`python tools/weapon_draw_check.py --wad DOOM1.WAD` compare the bar and the
weapon with independent oracles pixel by pixel.

### The palette flashes

`-D DOOM_PALFLASH=1` (on in `esp32dev_ILI9341_hud`, needs `DOOM_MENU` for the gamma)
ports `ST_doPaletteStuff`: once per rendered frame the scene asks
`lib/doom/palflash.c` which of PLAYPAL's 14 pages the player's counters call for
(red 1-8 for damage and the berserk fade, gold 9-12 for a pickup, green 13 for the
radiation suit and its blink) and re-applies the palette only when the page or the
gamma changes. Berserk is the strength POWER, not the wielded weapon; red page 1 is
never chosen (upstream's `(cnt + 7) >> 3` is 0 only when there is no flash).

A flash page is a mapped raw view of the container's PLAYPAL: page *p* is
`view + p * 768`, copied into the scene's 768-byte buffer, so a page change costs
one copy and no heap, and page 0 stays resident (768 B). Decoding the whole
10,752 B lump instead would need a transient internal-RAM block larger than the
largest one left after the save buffer (`heap_largest` 7,924). A container that
stores PLAYPAL deflated makes the view answer `ERR_NOT_RAW`: the scene keeps the
page it already has, and there is deliberately no allocation fallback. Gamma is
applied to each page.

`python tools/palflash_check.py --wad DOOM1.WAD --container DOOM1.whd` compares
the page selection, the 14 pages at five gamma levels and the decay on the real
world with an independent oracle (`--perturb` must fail).
`esp32dev_ILI9341_hud_palflashtest` plays a scripted damage and pickups into the
real player state and prints every switch (`SHOW,palette,...`); a diagnostic, never
the shipped image. `esp32dev_ILI9341_hud_nopalflash` compiles the feature out for A/B.

### The menu and the settings

`esp32dev_ILI9341_hud` also carries `-D DOOM_MENU=1`: DOOM's menu, ported from
linuxdoom's `m_menu.c` (`lib/doom/m_menu.c`), drawn over the finished frame with
the container's own `M_*` patches and the HU font.

**Buttons.** The six game buttons keep their mapping. **Start** (GPIO 34) opens
the menu on its press edge (a held Start does not flicker it) and, while the menu
is open, closes it from any screen, as Esc toggles DOOM's menu. Start is never a
game button: the world never sees it. LEFT + RIGHT held together is a game input
(both cancel to no turn) and opens nothing. **Select** (GPIO 35) toggles the
automap (see "The automap" below) and is not a game button either. While the menu
is open:

| Button | Does |
| --- | --- |
| Start | close the menu (from any screen) |
| Up / Down | move the cursor (wraps) |
| Left / Right | on a slider row, one step down / up |
| A | enter the screen; on a skill row, start the new game at that skill; on a slider row, one step up; on a slot row, load or save it (see "Saves") |
| B | back one screen; from the main screen, close the menu |

Opening the menu **pauses the 35 Hz world** (the tic clock keeps running, nothing
is replayed when it closes) and the world is handed no buttons until every button
has been released, so the Start press does not reach the game and the B that
closed the menu does not become a use.

**Screens.** NEW GAME, OPTIONS, LOAD GAME and SAVE GAME. NEW GAME is linuxdoom's
skill screen (the cursor starts on the saved skill); choosing a row reloads E1M1
at that skill over the world's own arena (rules, random position and every THING
run again). OPTIONS is the setup-lite screen: **skill** (the one New Game starts
on), **sound volume** (0-15, a thermometer), **gamma** (0-4, a thermometer) and
**mute** (OFF/ON, the same slider row with a range of one). LOAD GAME and SAVE
GAME are six-row slot screens: the rows are the save records' labels
(`E1M1 HURT 03:12`) or `EMPTY`, the titles and rows are drawn with the HU font
instead of vanilla's two title patches (`M_LOADG`/`M_SAVEG`, 2.3 KB each, not in
the container), and an activation reports an action to the scene rather than
touching a store -- the menu is C and engine-free. There is no READ THIS or QUIT
(no help screens, nothing to quit to), no episode screen (shareware has one
episode) and no nightmare "are you sure?" (the board has no Y/N keys). A SAVE over
a used row writes immediately: vanilla's confirm dialog would need a third screen
and two keys.

**What each setting does.** Gamma changes the palette for real: the PLAYPAL
tables are rebuilt through `gammatable` (`lib/doom/m_gamma.c`) and handed to the
panel with `setPalette8()` again, live while you move the thermometer. Skill takes
effect when a New Game starts. **Volume and mute drive the APU, live**: the 0..15
slider and the mute row are mapped onto DOOM's own 0..127 level
(`0 -> silence`, `15 -> 127`, mute `-> 0`) in `src/doomc/AudioLevels.h`, and that
level is applied once, as the engine's master gain, so the four SFX voices and
the music tracks are scaled by the same number (see "Volume and mute" under
Sound). Mute is a separate stored field, never `volume = 0`, so unmuting restores
the slider's position.

**Persistence.** The four values live in NVS (namespace `doom`, key `cfg`, 8
bytes: magic, version, the values, a checksum). They are written through the
engine's persistence layer -- an `Esp32NvsKeyValueStore` in
`src/doomc/DoomPersistence.cpp`, behind the `src/doomc/SettingsStore.h` seam --
whose ESP32 adapter is byte-compatible with an Arduino `Preferences` record in the
same namespace and key. They are written when you leave the options screen or
close the menu, not on every step. A missing, short, damaged or out-of-range
record falls back to the defaults (skill 2, volume 8, gamma 0, mute off). Boot
always starts E1M1 at medium skill; the stored skill is where the New Game cursor
starts. On a board whose NVS has never been written, the read fails and the boot
line says `settings=default`. `SHOW,menu,settings=nvs|default,...` is that boot
line; it ends `...,mute=0|1,level=0..127`, which is the value the engine was
actually given.

The mute flag rides in byte 6, the record's former *reserved, must be zero*
byte. A record holding 0 there reads as "not muted", and the version is still 1,
so records written without the flag stay valid. Skill, volume and gamma keep their
bytes (a board that stored `444D01030C020048` reads skill 3, volume 12, gamma 2,
mute off). Byte 6 is 0 or 1 and nothing else; any other value rejects the whole
record, and the checksum covers it too.

**Gates.** `python tools/menu_check.py` (the state machine against an independent
model, the stored form, the gamma table), `python tools/menu_check.py --arena
tools/DOOM1.whd` (New Game fits the board's arena at all five skills; under
`DOOM_MENU` the arena is sized at nightmare), `python tools/menu_check.py
--container tools/DOOM1.whd --wad DOOM1.WAD` (the menu art is raw and equal to
the WAD's), `python tools/menu_draw_check.py --wad DOOM1.WAD` (the menu screens
against an independent drawer, pixel by pixel, the mute row included) and
`pio test -e menu_test` (the mute codec and the volume mapping among its cases).
`esp32dev_ILI9341_hud_menutest` plays a fixed input script into the real button
path so the board can be driven without pressing anything; it is a diagnostic,
never the shipped image.

### The automap

`esp32dev_ILI9341_hud` also carries the automap: `lib/doom/am_map.c`, a
translation of linuxdoom's `am_map.c` (the window and its scale, the outcode
clipper, the Bresenham rasteriser, the block-aligned grid, the wall colour ladder,
the player arrow and the crosshair), drawn into the 168 rows above the status bar.
While the map is up the world frame and the weapon are not drawn at all -- the
bar and, if it is open, the menu are painted on top, exactly upstream's
`D_Display` order.

**Buttons.** While the map is up, none of them reach the world's tic command (the
player stands still behind the map, as they do behind the menu):

| Button | Does |
| --- | --- |
| Select (GPIO 35) | opens the map, or closes it; one press is one toggle |
| Up / Down / Left / Right | pan the window; the first press cancels "follow the player" |
| A | zoom in (1.02x a tic, twice a second) |
| B | zoom out (to the whole level, then it stops) |
| Start | opens the menu over the map, as upstream draws the menu on top of it |

An arming latch keeps a stuck-low Select pin from toggling the map forever (see
the wiring note under "Hardware").

There is no button for the grid: upstream toggles it with `g`, this board has no
keyboard, and no control was invented for it. The grid is proven by the host gate
and not reachable on the panel.

**What is drawn.** A line appears only when the wall pass has stored a range for
it, which is upstream's rule (`ML_MAPPED`): the port's linedefs are a frozen
flash record, so the session owns a shadow bitmap of one bit per linedef (60 bytes
for E1M1's 475 lines, 130 for E1M2's 1,033) and `r_segs.c` sets a bit for every
wall range it stores -- the same third statement of upstream's
`R_StoreWallRange`. The colours are upstream's: red for a solid wall, brown for a
floor-level change, yellow for a ceiling-level change, yellow-ish for a line
special 39 teleporter, grey for the computer-map power-up's unseen lines.
`AM_updateLightLev()`'s strobe is ported but **not running**, because upstream
1.10's own call to it is commented out: every wall is its base colour, exactly as
the reference is. Marks, `AM_drawThings`, the `IDDT` cheat and the deathmatch
followers are not ported (no keyboard, no marks art, no cheats, one player).

**Rate.** `AM_Ticker` runs once per 35 Hz world tic (not per frame), so a pan is
140 px a second and a zoom 2x a second whatever the panel's frame rate is.

**What it costs.** On the CYD a map frame costs **0.81-1.02 ms** (`us=` on every
`SHOW,map` line; mean 0.84-0.87 ms across two runs) against ~30 ms for the world
frame; the shadow bitmap is 60 bytes of heap taken at level load and freed with
the session, and the whole static cost is 240 B, the automap's state and two
counters.

**Log.** While the map is up, each reported frame also prints
`SHOW,map,us=<map microseconds>,active=1,follow=<0|1>,scale=<scale_mtof>,
grid=<0|1>,hash=<SHA-256 of the 320x168 window>`. That hash is the number
`tools/am_check.py` prints for the same state: the board's first map frame
(`b1677b75...`, the spawn view with the lines the wall pass stored there) is the
gate's `spawn_mapped` state, byte for byte. With the map down the log is
unchanged: the automap adds no field to `SHOW,frame`.

**Gates.** `python tools/am_check.py --wad DOOM1.WAD` renders ten scripted states
through the real `am_map.c` (and the real BSP walk and wall pass, for the mapped
bits) and compares the whole 320x200 surface with an independent oracle in
`tools/doom_ref.py`: E1M1 at the entry scale, the same view after a walk, the
light strobe, the grid, zoom-in, a pan with the follow cancelled, zoom-out to the
clamp, the computer-map power-up, E1M2 with its own bounds, and the stopped map.
Rows 168-199 must stay untouched. `--perturb colour|scale` moves one oracle
constant and must fail. `esp32dev_ILI9341_hud_mapcheck` plays a fixed input script
(Select, A, B, the d-pad, a walk, Select again) through the real button path and
prints `SHOW,map` for every reported frame; it reads only the script's Select bit,
so the pin's electrical noise cannot scramble the states. A diagnostic, never the
shipped image.

### Sound

`esp32dev_ILI9341_hud` carries `-D DOOM_AUDIO=1`: DOOM's own sound API --
`S_StartSound`, `S_UpdateSounds`, `S_Start`, `S_ChangeMusic`, `S_PauseSound` --
ported from linuxdoom's `s_sound.c` and `sounds.c` in `lib/doom/`, with the
DMX half replaced by a narrow C seam into the APU (`src/doomc/DoomAudio.cpp`).
Every cue is **pure APU synthesis** on the Legacy profile: four SFX voices
(slots 4..7), no PCM sample, no DMX decode, and never a `SET_SOUND_PROFILE`.

**What plays.** Weapons (pistol, shotgun, chaingun through its `chgun` link,
chainsaw start/idle, punch, rocket/plasma/BFG where a level can hand them
out), doors (open, close, blazing, the crush bounce), lifts and floor movers
(`pstart`/`pstop`/`stnmov`), switches (pressed lines, exits), monsters (sight,
active, attack, pain, death), the player (pain, death, the hard-landing `oof`),
pickups (`itemup`/`wpnup`/`getpow`) and the teleporter. The "one sound per
origin", `S_sfx[]` priority/`link`, distance culling and `M_Random` pitch
jitter are upstream's rules, statement for statement.

**The timbre is fitted, not asserted.** Each cue's values are derived from the
WAD's 55 `DS*` lumps: `tools/sfx_design.py` parses `S_sfx[]`, resolves `DS` +
uppercase(name), reads the DMX sample, and fits each cue's frequency, 4-point
pitch/duty contour and ADSR to the original's 16-segment RMS envelope, ZCR/2
centroid contour and autocorrelation pitch track. The 53 ids the shareware WAD has
no lump for (Doom 2's monsters and weapons, the super shotgun, the boss family)
are fitted from the closest relative in the same family; `tools/sfx_fit.txt` names
the donor and the reason for every one. The generator is deterministic: the same
WAD produces a byte-identical `SfxBankTable.inc`.

`AudioEvent` has no envelope field, so the bank carries its ADSR as 109
`InstrumentPreset` rows generated from the same table and looks one up per
cue; `src/doomc/SfxCueEvent.h` is the single cue -> event translation shared
by `DoomAudio.cpp` and the host gate. One logical cue per APU voice, eight
logical channels onto four voices, no allocation. The 109 presets cost
**9,252 B** of flash.

**The gate.** `python tools/sfx_check.py --wad tools/DOOM1.WAD` compiles
`tools/sfx_render.cpp` with the system `g++` against the real ApuCore, renders
every cue through it at 22050 Hz, and compares duration (5 % / 25 ms), the
16-segment envelope (Pearson >= 0.80), class, centroid (factor 2) and pitch
(1.5 semitones) with the original. All 108 cues are measured: the 55 ids with a
shareware lump compare against it, and the 53 donor cues compare against the
donor's original (the cue they copied). Of the 55 originals, 30 pass every
check and 25 are recorded as `accepted-deviation` with the measured number and
a reason (a multi-lobe RMS contour one ADSR cannot follow, or the 8-bit
original's broadband grit putting its ZCR centroid 3-4x its fundamental); the
donor copies inherit their source's result, so the whole run is 49 pass and 59
recorded, 0 failed. `--perturb` (frequency x1.5) and `--stub` (a fixed 440 Hz
square) must both fail. The gate proves the cue **is** the sound it claims to
be; whether it is pleasant takes a listening pass.

**Music.** `esp32dev_ILI9341_hud` plays E1M1's track on every level.
`lib/doom/s_sound.c` keeps upstream's
`S_ChangeMusic`/`S_StartMusic`/`S_StopMusic`/`S_PauseSound`; the three
`doom_music_*` calls they make are implemented in `src/doomc/DoomAudio.cpp`
over the engine's `MusicPlayer`. `S_Start` asks for the map's own track
(`mus_e1m1 + (episode-1)*9 + map-1`); `MusicBank` answers E1M1's track for every
level id (`e1m1`..`e1m9`), so a level change restarts the same music instead of
going silent. That is a 4 MB limitation: the other eight themes are ~270 KB and
the app has 57,843 B free. The intermission and title answer nullptr. The menu's
open/close calls `S_PauseSound`/`S_ResumeSound` (`DoomScene::runMenu`), so the
pause path is upstream's, not a second control.

**The converter.** `tools/mus2tracks.py` reads a `d_*` MUS lump, parses it by
`mus2mid.c`'s rules, and writes a deterministic C++ include. MUS has no tempo
field: its clock is fixed at 140 ticks/s and a quarter note is 70 ticks.
`ApuCore` clamps `tempoBPM` to 300, so the converter runs `MusicPlayer` at
**300 BPM**, where a beat is 0.2 s and one sequencer tick is 1/4 beat = 0.05 s
— exactly 7 MUS ticks, the finest grid the API allows. Every press's start is
placed on the nearest grid tick (`round(cumulativeMusTicks / 7)`) and its
`duration` is the difference of consecutive grid starts / 4 beats, an exact
multiple of 0.25 that the sequencer's truncation returns unchanged. The track
closes at `round(scoreEnd / 7) / 4` beats, so all four tracks end on the same
grid tick (E1M1: 1,920 ticks = 480.000 stored beats = 96.0 s) and the loops
align exactly. Pitch is `octave = midi / 12 - 1`, `note = midi % 12`, clamped
to octave 0..8 (D_INTRO's bass is MIDI 8 and its 24 notes become G#0);
pitch-wheel events are counted and ignored (1,146 in E1M1, a few cents each).

**Roles are measured, not fixed.** The three busiest melodic channels are kept
and register decides: lowest mean pitch is the triangle bass, highest the pulse
lead (duty 0.5), the last the pulse harmony (duty 0.125). A fixed channel
number would not hold -- E1M1's bass is ch2, E1M3's ch0, E1M5's ch1, D_INTER's
ch2 with a different melody/third order. Channels that do not make the three
are dropped and printed (`D_INTER` drops ch3's 400 notes and ch4's 194).

**Percussion.** One noise track. Simultaneous hits are folded by a fixed
priority (kick > snare > tom > open hi-hat > closed hi-hat > crash > ride >
other) because `ApuCore` plays one percussion voice per hit; the losers are
counted (190 of E1M1's 702 drum presses, mostly the kick+snare+tom stack DOOM
layers on one tick). The APU has three presets, so the mapping is kick
(35/36), snare (38-40), tom (41/43/45/47/48/50, folded to the snare's mid-band
body), and hi-hat for open/closed hats, crash, ride and the odd 75/80/81. A
drum's audible length is the preset's `defaultDuration`; the note's duration
only advances the sequencer. This is a deliberate fidelity cost of one
percussion voice.

**What ships.** One theme: E1M1's. `MusicBank.cpp` includes
`src/assets/music/MusE1M1.inc` once and answers it for every level id
(`mus_e1m1`..`mus_e1m9`). E1M1: 2,332 press events -> 2,145 `MusicNote`s ->
**34,320 B** (melody ch0 657 array entries, third ch1 712, bass ch2 263,
percussion 513). `D_INTRO` is 80 notes = 1,280 B and `D_INTER` is 3,975 =
63,600 B; both are converted and gated in memory, but not committed. With
57,843 B free in the app, one more theme would fit and the other eight do not; a
16 MB ESP32-S3 profile is where each id would get its own track, and only
`MusicBank`'s lookup changes.

**The gate.** `tools/music_check.py` converts `D_INTRO`, `D_INTER` and
`D_E1M1` in memory, re-derives every press's pitch and grid start from the
MUS, checks every percussion tick against the fold priority, and proves each
track's stored durations sum to `round(scoreEnd / 7) / 4` and project to the
same `round(scoreEnd / 7)` APU ticks. For E1M1 it also requires the committed
include to be byte-identical to a fresh conversion and parses the arrays back
to compare against the MUS. `--perturb` (every melodic pitch +1 semitone) and
`--stub` (empty tracks) both fail all three lumps. `tools/mus2tracks.py
--check` reproduces the committed asset byte for byte. The record is
`tools/music_check.txt`, and it carries the worst start error per track.

**The grid's price.** The 300 BPM ceiling fixes the grid at 50 ms, so a note
can start at most 25 ms from its original tick; the measured worst is
**21.43 ms** on all three lumps. At 22,050 Hz the 0.05 s tick truncates to 1,102
samples (49.977 ms), a 0.045 % shortening shared by all four tracks, so it
cannot desynchronise them. A finer grid would need a higher `TICKS_PER_BEAT` in
the APU; the BPM clamp is the API's ceiling.

**Telemetry.** The `SHOW,audio` line ends in two APU counters on the CYD.
`underruns=` and `worst_us=` are read once a second through
`doomAudioSetTelemetrySource()`, which `src/platforms/esp32_dev_ILI9341.h`
installs after `engine.init()` with the I2S backend it owns:

- `underruns` -- `ESP32_I2S_AudioBackend::getUnderrunCount()`: blocks whose
  `i2s_write` barely waited (less than a tenth of the block, meaning the DMA ring
  was already empty), failed, or came back short, after the ten warm-up blocks
  that prime the ring. It is the number that says the render task missed its
  deadline; one is a defect, not a jitter figure.
- `worst_us` -- `getWorstRenderUs()`: the longest `generateSamples()` of one
  block over every block since boot. It says what the voices actually cost.

The seam is engine-free (`src/doomc/AudioTelemetry.{h,cpp}`): one function
pointer plus one user pointer in `.bss`, asked for once a second by the scene's
report and never from the frame path, so an audio subsystem nobody listens to
costs the loop nothing. `pio test -e sound_test` proves both arms -- no source (or
a source that answers false) keeps the line at `underruns=-,worst_us=-`, and an
installed one puts its numbers on it -- with no engine and no APU. A build with
no source keeps the dashes; the native simulator takes that state (it compiles
the sound layer out entirely, and the SDL2 backend exposes no counters anyway),
because a zero would claim a measurement nobody took. The wiring costs 44 B of
flash.

**The soak.** `esp32dev_ILI9341_hud_soak` is the shipped HUD build plus
`-D DOOM_AUDIO_SOAK=1`. E1M1 plays from level load, and every four tics the scene
submits all four longest one-shots -- `barexp`, `telept`, `rlaunc`, `pldeth` --
from four distinct `S_SectorOrigin()` tokens. Each token re-fires its own origin,
so re-arming a cue never has to steal a voice, and four sequencer tracks plus
four SFX voices synthesise continuously (about 30 submits a second at the
shipped frame rate, because a round runs at most once per frame). Once a minute
it prints

```
SHOW,audio,soak,t=60,plays=..,steals=..,refused=..,active=..,underruns=..,worst_us=..,fps_x10=..
```

and after ten minutes the closing pair

```
SHOW,audio,soak,end,minutes=10,underruns=..,worst_us=..,plays=..,steals=..,refused=..,resets=0
SHOW,audio,soak,budget,block_us=5804,worst_us=..,pct_x10=..
```

`resets` is the soak's own restart count: 0 by construction, because the window
runs once and a board reset ends the capture at a boot line instead of
incrementing a field. `pct_x10` is `worst_us` in tenths of a percent of the
**5.80 ms 128-sample block at 22 050 Hz** (the DMA granularity). The engine's
render block is **512 samples, 23.22 ms**, so divide that percentage by four to
read it against the deadline an underrun is measured against.

Recorded baseline, CYD, one 10-minute run
(`pio run -e esp32dev_ILI9341_hud_soak -t upload`, whole log captured):

- `underruns` **0** at every minute and at the end -- the acceptance criterion.
- `worst_us` 5,938 -> **5,971**, i.e. 102.8 % of the 5.80 ms reference and
  **25.7 %** of the real 23.22 ms block.
- `plays` 18,092 at the ten-minute mark (18,956 by the end of the 10.5-minute
  capture), `steals` 0, `refused` 0, `active` 4 every minute.
- `fps_x10` 145-167 across the ten one-minute windows, mean 151.5. The first
  window reads 145 because it includes boot and the level load; a frame line
  inside the same run reads 150-155, the shipped band.
- `resets` 0, and the theme is still `playing=1` at the end.

The shipped `_hud` build prints the same two counters with music alone
(`underruns=0,worst_us=5314`), so the 5.97 ms is what four music voices plus four
SFX voices cost, not what the soak's serial writes cost. The script, its
counters and its lines live entirely inside `DOOM_AUDIO_SOAK`; the shipped HUD
build carries none of it.

**The profile is `Legacy`, and nothing switches it.** `Legacy` is the engine's
default and the only profile this port runs. There is no `setSoundProfile` call
and no `SET_SOUND_PROFILE` anywhere in `games/doom/`; the counters above are
read instead of a profile being tried. `Nes` and `Hybrid` are out of scope.

**Volume and mute.** The menu's slider and its mute row reach the APU.
`src/doomc/AudioLevels.h` owns the one conversion in the port -- the menu's 0..15
and its mute flag onto DOOM's 0..127 level, `volume * 127 / M_VOLUME_MAX`, muted
-> 0 -- and the scene hands that level to `AudioEngine::setMasterVolume()` as
`level / 127` from its settings load at boot and from `runMenu()` every frame
(the first statement is the comparison, so only a moved value costs a call).

The application point is deliberate, and it is why `S_SetSfxVolume` is not used
for gain: `MusicPlayer::setMasterVolume()` only forwards to
`AudioEngine::setMasterVolume()`, and the engine applies the master *to the
accumulated mix* (`ApuCore` sums all eight voices and multiplies once, before the
soft clip). It is therefore not a music level: it is the only level that can
scale SFX and music together, and applying the menu value there **and** through
`S_SetSfxVolume` would attenuate SFX by the square of the setting (volume 8 would
be -11 dB, volume 1 about -43 dB) while the music saw one factor. So
`snd_SfxVolume` stays at `S_MAX_VOLUME` (127), each event keeps upstream's own
`volume / 127` from `S_StartSoundAtVolume()`, and the one gain the player moves is
the master. Mute is `master = 0`, which silences both, and it does not touch the
stored volume.

`S_StartMusic` (looping = 0) is never called by the port; the shipped track
loops, and a non-looping view would need a second `MusicTrack` struct per asset.

**Gates and board proof.** `pio test -e menu_test` proves the encoded flag -- an
old 8-byte record with the reserved byte zero decodes as "not muted", the flag
round-trips through the store, a flag byte of 2 is refused even with a valid
checksum -- and the mapping (`0 -> 0`, `M_VOLUME_MAX -> 127`, `8 -> 67`, mute
`-> 0` while `settings.volume` is unchanged). `tools/menu_check.py` carries the
fourth row and the flag in its model and its golden bytes (`444D01030C020149` is
`(3, 12, 2)` muted), and `tools/menu_draw_check.py` a fifth options state (mute
ON); both still fail under `--perturb`.

Recorded on the board (COM4, CYD), NVS erased first:

- shipped `_hud`, nothing stored:
  `SHOW,menu,settings=default,skill=2,volume=8,gamma=0,mute=0,level=67` -- the
  default 8 of 15 is level 67 of 127, so the master gain is 0.527.
- `esp32dev_ILI9341_hud_menutest`, one full script: the OPTIONS save reads
  `SHOW,menu,saved=ok,skill=2,volume=5,gamma=3,mute=1`, the five New Games keep
  `volume=5,gamma=3,mute=1`, and the last one leaves `skill=3`.
- RTS reset of that image:
  `SHOW,menu,settings=nvs,skill=3,volume=5,gamma=3,mute=1,level=0` -- the
  volume, gamma and mute came back from NVS, the slider's stored value is 5 and
  the level the engine was given is 0, i.e. silence.
- the shipped `_hud` flashed over it reads the same record
  (`nvs,...,volume=5,gamma=3,mute=1,level=0`; `skill` in that line is whatever
  the diagnostic's script had reached last), and `frame00` is unchanged.

Erase NVS after such captures: a shipped image left muted is silent, and that
reads as a broken audio path. Two limits apply: the `Legacy` profile's release is
capped at 100 ms, so a cue whose original tail is longer decays inside its own
duration; and sector-originated cues (doors, lifts, floors) are non-local at full
volume because `p_sector_t` has no `soundorg`, their sector-index token keeping
one-sound-per-sector and `S_StopSound` working.

**Eight channels, four voices.** DOOM keeps its eight logical channels, so
vanilla's priority and origin rules still apply. The APU has four SFX voices:
an idle voice is taken first, otherwise the least important sounding cue is
stolen, and when every voice holds a Reserved cue (the player's pain, the
player's weapon, the boss family) the newcomer is refused rather than stealing
one. `steals` and `refused` are counted, and `pio test -e sound_test` (including
the preset/cue consistency, the shared event translation, the telemetry seam's
two arms and the music bank's one shipped track) proves the selector never names
a Reserved cue as a victim, for every class combination.

**Log.** At boot: `SHOW,audio,init,status=ok,bank=108,mode=legacy`, then
`SHOW,music,event=init,track=none,...`; when `S_Start` loads E1M1,
`SHOW,music,event=start,track=e1m1,notes=657/712/263/513,
beats=480.00/480.00/480.00/480.00,playing=1,paused=0,underruns=0,worst_us=1013`
(480 stored beats at 300 BPM is the original 96 s; at 120 BPM the same score
would read 192). Once a second beside `SHOW,frame`:
`SHOW,audio,plays=..,steals=..,refused=..,unknown=..,voices=4,active=..,
underruns=0,worst_us=..` and the same `SHOW,music,event=periodic,...` line. Both
trailing fields are the APU's own counters (see Telemetry above); a build with no
source -- the native simulator -- prints `-` there.

**How to hear it.** `esp32dev_ILI9341_hud_audiotest` is the shipped build plus
`-D DOOM_AUDIO_SCRIPT=1`: at boot it plays twelve cues -- pistol, shotgun,
door open, door close, switch, lift, imp sight, player pain, item pickup,
teleport, barrel explosion, player death -- one every 700 ms through the real
`S_StartSound` path, and prints a `SHOW,audio,play,id=..,name=..,voice=..,
vol=..,pitch=..,dur_ms=..` line for each. The `dur_ms` is the fitted duration, so
the board log can be checked against `tools/sfx_fit.txt` without ears. Flash it
with `pio run -e esp32dev_ILI9341_hud_audiotest -t upload`. It is a diagnostic,
never the shipped image.

### The level transition's fade

Vanilla's screen melt (`f_wipe.c`) copies `screens[2]` and `screens[3]` -- two
more 64,000 B surfaces, plus column-major transposes of both. This board does not
have them: the largest free block after setup is 47,092 B, so the melt is left
for a 16 MB ESP32-S3 profile. The 4 MB build substitutes a **palette fade**,
which needs no buffer at all: after every level load -- a map change, a death's
reload and a New Game -- the first frame is painted black and the next eight
steps ramp back through the PLAYPAL table the palette path already owns
(`palette::applyGammaScaled()`: one integer scale per channel before the gamma
table, then the two packings).

**What it costs.** Eight table rebuilds per transition, measured on the CYD at
**1.96-2.01 ms in total** (`SHOW,fade,steps=8,us=<total>`; ~245 us a step, well
under one 28.57 ms tic) and nothing at all on the frames between transitions. A
fade owns the palette while it runs, so the damage/pickup flashes are skipped for
those frames and the gamma path defers to it; the last step hands the table back
to the normal path. The reload is noticed without new plumbing: `leveltime`
restarts at 0 on every load, so a tic count that goes backwards is a transition.

**Gate.** `python tools/fade_check.py --wad DOOM1.WAD` builds the firmware's own
`DoomPalette.cpp` and compares its tables for gamma 0..4 at every fade scale
(0..256 plus out-of-range values that must clamp) against an independent oracle,
then proves scale 0 is black, scale 256 is exactly `applyGamma()`, and every
channel is monotone across the scale. `--stub` builds a copy of
`DoomPalette.cpp` with the scale term removed (no fade) and must fail;
`--perturb` moves the oracle by one scale step and must fail.

### Level transitions

Exit lines (specials 11/51 on use, 52/124 on cross) consume the line and write
`p_level_t::exitRequest`; E1M8's ending, sector special 11, writes the same
request. The driver reads it after every tic and loads the next map.

**What a transition is.** The driver carries the level the world is on
(`gRuntimeLevel`, one byte of `.bss`): it starts at the build's map
(`PIXELROOT32_DOOM_FRAME_LEVEL`), moves to the map an exit names, returns to the
build's map on a New Game and after a victory, and is what a save record stamps.
After each 35 Hz tic, `frameLevelRunTics()` reads `exitRequest` and runs the
upstream chain, in upstream's order and before the HUD, automap and sound work of
that tic:

```text
exitRequest != P_EXIT_NONE
  -> levelExitNext()            g_exit_from_level + g_completed_next (next map, or the victory)
  -> openSessionMap(next)       the container's nine raw levels; a map view is pointers, no allocation
  -> p_level_unbind_render()    p_level_change copies SECTORS through the live reader
  -> levelChangeLoad(..., true) g_do_world_done + the player carried across the load
  -> p_level_bind_render()      the new map's live sectors, scroll offsets and animation frame
```

That composition lives in
[`src/doomc/LevelTransition.{h,cpp}`](src/doomc/LevelTransition.h), which is
engine-free (lib/doom and `<string.h>`), so `pio test -e level_test` drives the
same two calls the board runs. There is no intermission screen: the next map
loads directly and the palette fade plays over it.

**What carries, and what resets.** Upstream finishes a level with
`G_PlayerFinishLevel()` and does **not** recreate the inventory: `P_SpawnPlayer`
rebirths a player only when `playerstate == PST_REBORN`, which a death sets, not
a completed level. This port's loader cannot express that -- `p_spawn_map_thing()`
answers a type-1 record with `p_player_wire()`, i.e. `G_PlayerReborn` -- so the
transition takes the carried fields before the load and writes them back after
it. The header comment gives the rule each field serves; the table is:

| Carried (a completed level does not reborn the player) | Reset (`G_PlayerFinishLevel`, plus what a load recomputes) |
| --- | --- |
| `health`, `armorpoints`, `armortype` | `powers[]` -- an invulnerability does not survive a level |
| `ammo[]`, `maxammo[]` -- the pools and the ceilings a backpack raised | `damagecount`, `bonuscount` -- "no palette changes" into a level |
| `weaponowned[]`, `backpack` | `extralight`, `fixedcolormap` -- gun flashes and ir goggles |
| `cards[]` (see the deviation below) | `attacker` -- a pointer into the level being left |
| `readyweapon`, `pendingweapon` | `killcount`, `itemcount`, `secretcount`, `didsecret` -- per-level |
| | `viewz`, `viewheight`, `deltaviewheight`, `bob` -- P_CalcHeight's |
| | `psprites[]`, `pspritesStarted` -- rebuilt from the carried weapon |
| | `message`, `usedown`, `attackdown`, `refire`, `onground`, `playerstate` |

**Deviation from vanilla:** the keys (`cards[]`) carry across a level, although
upstream's `G_PlayerFinishLevel` clears them. Everything else in the table is
upstream's own rule. The carried `health` is also written to the new avatar's
`mobj->health`, because `P_DamageMobj` decrements both.

**E1M8, as a named placeholder.** The boss map has no exit line: its ending is
sector special 11, which sets the same request, and `g_completed_next()` answers
the episode's victory for map 8 before the secret flag is read. There is no
finale screen, so the run ends honestly: the driver prints
`SHOW,level,victory=1,episode=1,map=8`, reloads the run's first map with
`carry=false` (G_InitNew is a reborn) and raises a one-shot; the scene opens the
main menu over it on the next frame, through the same `m_menu_open()` a Start
press uses, so the menu's own `SHOW,menu,state=open` line reports it.

**The map view.** Opening a level at runtime rebuilds the session's one
`doom_map_t` from the container's raw lumps (`--raw-level E1M1..E1M9`). In the
flash build every lump is a pointer into the mapping that stays live for the
session, so a transition allocates nothing; the lump order is one function
(`readMapLumps()`) so the startup map and a transition bind the same ten lumps in
the same order the RAM probe measures. The automap is re-initialised for a new
map (`am_level_init()` reads its linedef count and bounds), not merely stopped;
the HUD restarts; `S_SetGameLevel`/`S_Start` run as G_DoLoadLevel's do. One
track ships, so every map plays E1M1's theme rather than the wrong one.

**The eye, before the first tic.** A load has no camera height until a tic
computes it (`p_player_wire()` is G_PlayerReborn's `memset`; `P_CalcHeight` is
what turns `viewheight` into `viewz`). Every load therefore states the eye its
own tics would reach, in `restartLevelPresentation()` -- see
`settleLevelView()`'s comment in
[`src/doomc/FrameDriver.cpp`](src/doomc/FrameDriver.cpp). Without it, the frame
after a load is drawn from the sector floor, where the wall and plane passes no
longer meet and the surface's `0xFF` prefill fills the gap as a magenta bar at
each edge of the view. The fade hides that on an ordinary transition; the
victory does not, because the main menu opens on the next frame and freezes the
world.

**The serial lines**, one per event:

```text
SHOW,level,from=E1M1,to=E1M2,skill=2,tics=68,health=100,ammo=50
SHOW,level,victory=1,episode=1,map=8
SHOW,level,error=load,from=E1M1,to=E1M2,code=5,recovered=1
```

`from`/`to` are the level markers, `tics` is the level's clock at the request,
`health`/`ammo` are the carried values read after the load (clip ammo), and a
refusal names the step that refused (`levelChangeLoad`'s code in
`g_level_error_t`'s vocabulary). A failed load is re-laid from the map the world
was on, and a world that cannot be re-laid is closed rather than drawn zeroed.

**Gate and board proof.** `pio test -e level_test` (nine cases: the switch through
`p_use`, E1M1 -> E1M2 with every carried and reset field, the no-carry control,
E1M3's secret exit to E1M9, E1M8's ending, a save in E1M2 round-tripping into
E1M2, the slot budget, the weapon repair, no request) and `tools/level_check.py`
(the nine-change chain). On the board, `env:esp32dev_ILI9341_hud_leveltest` (the
shipped image plus `DOOM_LEVEL_TEST`) stands the avatar in front of E1M1's own
exit switch at frame 20 and holds USE through the real button byte from frame
24. Captured on COM4:

```text
SHOW,level,script=stand,status=ok,level=E1M1,line=330,special=11,standoff=2097152,...
SHOW,level,from=E1M1,to=E1M2,skill=2,tics=68,health=100,ammo=50
SHOW,map,init,status=ok,lines=1033,bytes=130        (E1M2's automap)
SHOW,fade,steps=8,us=2195,frame=32
SHOW,save,slot=3,status=ok,bytes=2030               (in E1M2)
SHOW,fade,steps=8,us=2184,frame=88                  (New Game, back to E1M1)
SHOW,load,slot=3,status=ok,level=2,skill=2,tics=96  (back in E1M2, from E1M1)
SHOW,map,init,status=ok,lines=333,bytes=42          (E1M8, loaded by the script)
SHOW,level,victory=1,episode=1,map=8
SHOW,menu,state=open,frame=121,arena_used=16736,arena_free=6224,...
SHOW,fade,steps=8,us=2117,frame=128
```

The same image proves the other two halves end to end. It saves the live E1M2
world into slot 3, starts a New Game (back to E1M1), then loads the record -- and
`SHOW,load` names level 2 while the world was on level 1, because a restore opens
the map the record names. And it loads E1M8 at the running skill, raises the exit
request the sector special raises, and the next tic takes the shipped victory
branch: the run reloads on E1M1 (mobjs=92 at medium, so the boss map fit the
arena the session sized from its startup map), the main menu opens and the fade
runs again. `frame00` stays
`1d8b3849ab3f76750056937f2827bf2bcbb2b98e82976fb3a7ce263d5273c9d3`.

**Watching the whole episode.** `esp32dev_ILI9341_hud_levelwalk` is a viewing
aid, never shipped: it walks E1M1 -> E1M2 -> E1M3 -> **E1M9** (E1M3's secret
line 51) -> E1M4 -> E1M5 -> E1M6 -> E1M7 -> E1M8 -> the ending, dwelling about
three seconds per level. Each press is the real one (`p_use` through the held B
byte), and the driver's `SHOW,level,script=stand`, `from=...` and `victory=` lines
are the record of what the panel showed. Flash it, reset, watch; reflash `_hud`
to return to the game. On COM4 the sequence was:

```text
E1M1 -> E1M2 -> E1M3 -> E1M9 -> E1M4 -> E1M5 -> E1M6 -> E1M7 -> E1M8
SHOW,level,victory=1,episode=1,map=8
SHOW,menu,state=open,...
```

## Boot and failure screens

A boot that cannot open the container shows the reason on the panel as well as
on the serial port, so a failed boot is distinguishable from a hung board before
any level exists.

**The loading screen** is painted and presented from `DoomScene::init()`, before
the level session opens: title `DOOM`, `PIXELROOT32 ESP32`, `OPENING THE WAD`,
`E1M1`, and the footer `SHAREWARE DOOM1.WAD`. You see the display work before the
game does, and a boot that dies in the loader is not silent.

**The error screen** replaces it when the open fails, and `draw()` keeps painting
it while the scene runs. The title, one-sentence body and advice come from a
taxonomy in [`src/doomc/LevelFailure.cpp`](src/doomc/LevelFailure.cpp); the footer
carries the machine pair (`CODE 3 E1M1`) so a photograph of the panel is enough
to find the branch in the serial log.

| `reason=` | Title | What it means | What to do |
| --- | --- | --- | --- |
| `partition_missing` | NO WAD SLOT | the image has no `wad` partition | flash a table that has one |
| `partition_map_failed` | WAD UNREADABLE | the partition exists but did not map | reflash and retry |
| `container_invalid` | BAD CONTAINER | the header or its length is not a container | reflash `DOOM1.WHD` |
| `unavailable` | NO FLASH ACCESS | this build has no flash mapping | use the board build |
| `baked` | NO BAKE | the baked-graphics manifest is missing or malformed | rebuild the container |
| `palette` | NO PALETTE | `PLAYPAL` is absent or too short | check the WAD, rebuild the container |
| `level` | NO LEVEL | the level marker lump is missing | check the WAD |
| `map_lump` | MAP LUMP | a required map lump or directory failed | rebuild the map lumps |
| `colormap` | NO COLORMAP | `COLORMAP` is missing or rejected | check the container |
| `sprites` | NO SPRITES | the thing list is empty/oversized, or its heap block failed | free memory; rebuild |
| `storage` | NO RENDERER | the renderer's pools failed to initialise | reboot; rebuild the container |
| `world` | NO WORLD | the level's arena did not fit | free memory; retry |
| `surface` | NO DISPLAY | the build has no usable framebuffer | check the display build |

**Why the font is embedded.** The failure it must report includes "the WAD is
missing", so the message cannot come from a WAD font. `src/doomc/BootScreen.cpp`
carries a 5x7 column font for ASCII 32..95 -- 64 glyphs, 320 bytes -- which also
keeps the screen legible at the panel's 0.75 downscale and makes the painter
host-testable. Uppercase-only is a deliberate limit of that font.

**Why the painter writes RGB332.** The engine's 8bpp surface stores RGB332 bytes
until `setPalette8()` installs the PLAYPAL expansion table. A boot screen is only
ever painted before a level session exists, so its bytes are RGB332; once a
session is open the same byte is a DOOM palette index, and painting a boot screen
then would lie about every colour. That is why `drawSceneFrame()` paints the boot
screen only while `isOpen()` is false, and keeps the last good picture (and its
`SHOW,renderfail` line) for an open session that refuses a viewpoint.

**The serial lines.** One line per outcome, beside the setup line:

```text
SHOW,boot,screen=loading,level=E1M1
SHOW,boot,screen=game,level=E1M1,open_ms=142
SHOW,boot,screen=error,reason=container_invalid,code=5,detail=
```

The `open_ms` field is the measured `openLevelSession()` plus PLAYPAL cost. On
failure the `SHOW,setup,status=fail,...` line still prints with its exact text
and fields; the boot line only adds the screen's own classification.

**Reading a photograph of the panel.** The title names the failure in words, and
`CODE <n> <level>` in the footer joins the picture to the log: search the serial
capture for `SHOW,boot,screen=error,reason=<name>,code=<n>` where `<name>` is the
title's meaning from the table above. The third line, when present, names the
lump or baked error that failed.

**Proving the failure screens.** The host gate proves the painter's bytes; the
board proves the two WAD-missing branches, because those are the failures a
person can actually create.

*A table with no `wad` row.* Build and flash the diagnostic environment, which
is the shipped image over `partitions_nowad.csv`:

```text
pio run -e esp32dev_ILI9341_hud_nowad -t upload --upload-port COM4
```

It proves `ERR_PARTITION_MISSING`: `esp_partition_find_first("wad")` returns
null, the serial keeps `SHOW,setup,status=fail,reason=open,code=3` and adds
`SHOW,boot,screen=error,reason=partition_missing,code=3,detail=`, and the panel
shows `NO WAD SLOT`, with no `SHOW,frame` and no panic. The environment is a
diagnostic, never the shipped image.

*An erased container over the shipped table.* This one destroys the container on
the board, so the restore is part of the proof, not a cleanup step:

```text
esptool.py --chip esp32 --port COM4 --baud 921600 erase_region 0xC0000 0x338000
esptool.py --chip esp32 --port COM4 --baud 921600 write_flash 0xC0000 tools/DOOM1.whd
esptool.py --chip esp32 --port COM4 --baud 921600 verify_flash 0xC0000 tools/DOOM1.whd
```

The erase covers the `wad` row only. Its offset and size are `0x0C0000` and
`0x338000`: erasing from `0x0B0000` would touch the last 64 KB of the app row,
and a `0x348000` length would reach into the `saves` partition -- a diagnostic
that costs the app or the save slots is not a diagnostic.

(`esptool.py` ships with PlatformIO at
`%USERPROFILE%\.platformio\packages\tool-esptoolpy\esptool.py`; run it with the
PlatformIO venv's `python.exe`.) The erase proves `ERR_CONTAINER_INVALID`, not
`ERR_PARTITION_MISSING`: the table still has the partition, so the open gets past
`esp_partition_find_first()` and fails on the header instead -- the erased bytes
are `0xFF`, which read as a container size of `0xFFFFFFFF`, larger than the
partition. The serial prints `reason=open,code=5` and
`SHOW,boot,screen=error,reason=container_invalid,code=5,detail=`, and the panel
shows `BAD CONTAINER`.

**The board cannot play until the container is restored.** Until the
`write_flash` (and a `verify_flash` that reports `verify OK`) succeed, every boot
lands on `BAD CONTAINER`; the app image itself is untouched by the erase, so the
restore is a container write and nothing else.

**Gate.** `python tools/boot_check.py` compiles `BootScreen.cpp` and
`LevelFailure.cpp` with the system `g++` and hashes every screen state's
320x200 RGB332 bytes. It asserts `bootScreenFits()`, the ASCII 32..95 alphabet,
the 25-character line limit, distinct titles, and the `CODE`/level pair in every
error footer, then compares the title, lines, footer and hash with
[`tools/boot_screens.txt`](tools/boot_screens.txt). `--perturb` (one string
shifted) and `--stub` (a blank surface) must both fail.

## Saves

A save is one level's mutable world, written by the port itself. Vanilla's
`p_saveg.c` archives a memory image: a flat copy of `player_t`, every `mobj_t`
and every sector thinker, with pointers stored as offsets into a 32-bit DOS build
of the same structs. That cannot load here -- this port's thinkers live in a
caller-owned arena whose addresses differ on every load, and `doom_map` is a
zero-copy view over (on the board) a mapped flash page -- so the record is
self-describing and index-based instead. `src/doomc/SaveGame.{h,cpp}` is that
record: engine-free, allocation-free, no floating point, no stdio.

**What one record holds.** A fixed 48-byte header -- magic, version, schema, map
number, skill, `leveltime`, kill count, map fingerprint, a 15-character label
(`E1M1 HURT 03:12`) and the body's length and checksum -- which is all the slot
menu reads. Then the player record (health, armour, ammo, weapons, powers, the
weapon/psprite state, the view height and the screen-flash counters, with the
avatar and the attacker as mobj indices), the level counters and the `M_Random`
position, the sector and linedef deltas, every thinker in list order, the
`P_NoiseAlert` targets by mobj index, and the scroll offsets.

**Why deltas and indices, not a full dump.** One record is capped at 3,968 B --
the largest blob that stays inside one NVS page -- and there are six slots. A
full sector dump is 2,210 B for E1M1's 85 sectors and a full mobj dump is ~90 B
for each of the 116 things at nightmare; together they do not fit. So sectors are
stored only where floor/ceiling height, flat, light, special or tag moved -- plus
every sector whose WAD special is nonzero, because `P_SpawnSpecials()` rewrites
`special` and the record must pin the result -- and each mobj carries a mask of
the fields that still equal what a fresh `p_spawn_mobj()` of the same type would
produce. A monster asleep in its spawn state costs 16 B; one mid-chase costs
about 64 B. The state is a `statenum_t` index, `target`/`tracer` are indices into
the mobj list (`0xFFFF` for none), and the thinker list is written in order so a
restore reproduces the order RNG draws and collision walks happen in.

**Measured.** `pio test -e save_test` prints its numbers. The budget line is the
state the record must cover, and the long-run line is where it actually peaks:

```text
SAVE_BUDGET mobjs=116 bytes=2607 max=3968
SAVE_LONGRUN t=16272 mobjs=120 alive=115 bytes=3393 ...
SAVE_MARGIN max=3393 bound=15662 budget=3968 headroom=14.4%
```

116 is the most things E1M1 can spawn (nightmare and hard share option bit 4;
medium spawns 92). The first line is that state at tic 0: 2,607 B. The long run
is ten minutes of deterministic combat (21,000 tics, fire held, a USE tap every
40 tics, the avatar topped up); the peak is **3,393 B with 120 live and 115 awake
mobjs**, 575 B of headroom over the measured record (14.4 % of the 3,968 B
slot). The analytic bound -- 116 mobjs at their widest, 85 tag movers, every
sector and linedef -- is 15,662 B, so the record is bounded by measurement and a
save taken in a state the run never reached answers `TooLarge` and says so rather
than dropping a field. It does not corrupt or silently truncate.

**What the differential proves.** A round trip compares the writer with its own
reader; a field the writer forgot is absent from both sides and the comparison is
still equal. So the gate is a differential: save world A at tic 20, run A 35 more
tics with a fixed script (turn, fire, walk), snapshot every observable; build
world B the same way, restore the payload at tic 20, run B the same 35 tics, and
require player position/angle/health/armour/ammo/weapon, the mobj count and each
mobj's position/health/state/reference, the sector heights and the RNG position to
match. A failure names the observable and both values. The same differential runs
with a door mid-motion, and once more at the end of the ten-minute run. One more
case deliberately restores *without* the RNG and must diverge -- that case is what
makes the gate's silence about the RNG mean something.

**What is not archived, and why.** `r_mobj_t::spawnpoint` is written by
`P_SpawnMapThing()` and read by nothing in this port (there is no
item/nightmare respawn path); the moment a respawn path reads it, the record
must grow. `sprite`/`frame` are exactly `states[state]`, `subsector`/`sector` are
recomputed from `x`/`y` at spawn, and `radius`/`height` come from `mobjinfo[type]`.
`p_level_t::animFrames`, the per-sector animation frames and the sound flood's
epoch are recomputed from `leveltime`/geometry on the next tic, and
`P_UpdateSpecials()` runs before any draw. The full list, with the reason for
each, is the header comment in [`src/doomc/SaveGame.h`](src/doomc/SaveGame.h).

### How it is wired

**The partition.** The 4 MB part is fully allocated (see "Flashing the board"
for the table). The `saves` row takes the top 32 KB of flash, and the app row is
720,896 B. To fit both, the container omits the 239 sprite frames no list can
name -- 264,819 B stored, 271,532 B of container with their directory entries
(`tools/whd_encode.py --drop-unused-sprites`). That is not a fidelity loss: the
renderer's sprite table is the union bake (216 frames from the nine levels'
THINGS and their state chains) plus the 28 weapon patches that are raw by name,
and nothing else is addressable.

32 KB is 1,008 NVS entries of 32 B. One NVS page is 126 entries, so the largest
blob that stays inside one page is `(126 - 2) x 32 = 3,968 B`; six of those use
756 entries (24.2 KB) and leave two pages for NVS metadata and garbage
collection. Eight at that size would use all 1,008 with no slack, which is why
there are six. `tools/make_container.ps1`'s `$Budget` and `$ContainerOffset`
follow the `wad` row, so a rebuild cannot write over the app or the slots.

**The stores.** `src/doomc/DoomPersistence.{h,cpp}` owns one
`PersistenceService` and its two media. On the board they are the engine's
`Esp32NvsKeyValueStore("doom", 64)` on `nvs` and
`Esp32NvsSlotStore("doom", "save", 6, 3968, "saves")` on the `saves` partition; on
a host the same code uses the engine's `HostFile*` adapters under `save_data/`.
`Engine` does not own any of it -- the scene constructs it, calls `init()` once in
`DoomScene::init()` before the menu can be opened, and binds the two roles. The
save payload and the envelope scratch are one 7,936 B heap block taken there:
`dram0_0_seg` has under 1 KB free, so a static pair could not link, and nothing is
allocated on the frame path. A store that cannot initialise answers `Unavailable`
and the game runs on defaults with saves disabled -- which is the honest state on
a board flashed with a table that has no `saves` row.

**The settings record.** The eight-byte record (`D` `M` v1 skill volume gamma
mute checksum) lives in namespace `doom`, key `cfg` on `nvs`.
`doomc::KeyValueSettingsStore`
([`src/doomc/SettingsStore.h`](src/doomc/SettingsStore.h)) sits on the engine's
`IKeyValueStore`, whose ESP32 adapter is byte-compatible with Arduino
`Preferences` in the same namespace and key. The engine's `RecordCodec` envelope
is deliberately *not* used for this record -- its bytes are a contract pinned by
`menu_check` -- while a save slot does carry the envelope, whose CRC is a second
guard beside the record's own checksum.

**The six slots, from the menu.** MAIN has **LOAD GAME** and **SAVE GAME** in
vanilla's order after OPTIONS; each opens a six-row screen drawn from labels the
scene fills by reading the six slot headers (one `loadRecord()` per slot, into the
same scratch). A row with no usable record reads `EMPTY`. Activating a LOAD row
reloads the record's level at its skill through the same
`g_demo_reborn()` path a New Game uses and then applies the record; an EMPTY row
does nothing and keeps the screen open. A SAVE writes the live world into the
row immediately -- vanilla asks "are you sure?", and this port has no confirm
dialog, which is stated in `m_menu.h`. The menu itself stays C and engine-free: it
receives labels and returns an action, never a store.

**The map number is real.** A record's header names the level the world was on:
`frameLevelSave()` stamps `frameLevelNumber()` and `frameLevelLoad()` opens the
map the record names before applying it. That is what makes a save written in
E1M2 load back into E1M2 -- and into E1M2 from E1M1, which is the case the board
proof exercises. The body's map fingerprint still refuses a record applied to the
wrong world, so a wrong header cannot put a level's state onto another level's
geometry.

**The serial record.** One line at boot and one per operation, all through the
same console as every other line:

```text
SHOW,persist,status=ok,settings=1,saves=1,slots=6,record=3968
SHOW,save,slot=0,status=ok,bytes=3393
SHOW,load,slot=0,status=ok,level=1,skill=3,tics=16272
```

`status` is `ok` when both roles are bound, `partial` when only one is (settings
work, the `saves` partition is missing) and `unavailable` when neither is. A
record the envelope rejects prints `status=corrupt`; a payload the save layer
rejects prints its own word (`truncated`, `bad_magic`, `bad_version`,
`wrong_level`, `wrong_skill`, `apply_failed`). A load that fails leaves the world
freshly loaded, never half-restored.

**Operational caveat: a reflash can cost you the settings, not the saves.**
`pio run -t upload` always writes `boot_app0.bin` (the 8 KB `otadata` image) at
`0xE000`, which is the last page of the `nvs` partition (`0x9000..0xF000`), so a
reflash can lose the settings record. The `saves` partition is at `0x3F8000`, far
above it and not in the upload list, so a reflash does not touch it. That is why
the settings are one small record (defaults are a solved state) and the slots are
their own partition. Whether a given upload hits the record depends on where NVS
placed its live entries; in the save proof below the uploader printed
`Wrote 8192 bytes ... at 0x0000e000` and the record survived. The engine's
persistence smoke tool has a fix this project does not wire in:
`tools/persist_nvs_smoke/keep_nvs.py` in the engine repository is an
`extra_scripts = post:` hook that drops the `0xE000` image from
`FLASH_EXTRA_IMAGES`/`UPLOADERFLAGS`; add it to the environments you flash if a
reflash ever loses a settings record you need.

**Flash and RAM budget (measured).** The shipped `_hud` image is **663,049 B of
the 720,896 B app row (92.0 %)**, leaving 57,847 B; the container is
**3,078,396 B of 3,375,104 B**, leaving 296,708 B; `dram0_0_seg` keeps
`seg_free=1124` (the image is 123,456 B of the region, 37.7 % of the chip's RAM).
The save record (`src/doomc/SaveGame.cpp`) is 25,737 B and PlatformIO links every
project source, so it is in the image even in environments that never call it;
the persistence wiring (the engine's persistence module, the NVS partition
adapter, the two menu screens) is a further 35,700 B. On the DRAM side the scene
is a heap object and the engine is not (a `Engine*` here would alias
`DoomScene.cpp`'s `extern Engine engine;` and crash).

**Running the gate.** `pio test -e save_test` needs `tools/DOOM1.WAD` (or
`DOOM_WAD` pointing at one): a differential against a synthetic map would not
exercise 116 mobjs. That WAD is shareware data and is not committed. The slot
budget, the six labels and the activation rules are gated by
`pio test -e menu_test`, `tools/menu_check.py` and `tools/menu_draw_check.py`; the
engine's own persistence contract suite lives in the engine repository.

### Proving a save survives a reset

The record has to outlive the run that wrote it. The board has no buttons the
build host can press, so the proof is driven by `esp32dev_ILI9341_hud_menutest`
(the shipped image plus `DOOM_MENU_SCRIPT`); the renderer, the world and the
frame path are the shipped ones. The diagnostic chooses its script at boot from
slot 0's label: empty means the full script (settings, six New Games, SAVE slot
0, LOAD slot 0, SAVE slot 1), used means the load-only script. One image, two
boots, and **no reflash between the save and the load** -- the reset is the only
event between them.

The capture sequence, on COM4 at 115200:

1. Erase `saves` (`esptool erase_region 0x3F8000 0x8000`) so all six rows start
   `EMPTY`, flash the diagnostic
   (`pio run -e esp32dev_ILI9341_hud_menutest -t upload --upload-port COM4`),
   then reset and capture 100 s of the full script.
2. Reset (open COM4, `RtsEnable=$true` for 150 ms, `RtsEnable=$false`) and
   capture 30 s; the same image now runs the load-only script.
3. Compare `x`, `y`, `angle`, `health` and the player `tics`, plus the
   `SHOW,load` level/skill/tics. The `SHOW,frame` line immediately after a load
   is forced by the load itself (`showReportAfterLoad_`), so it describes the
   restored world before another tic can run; the ordinary cadence is one line a
   second and would otherwise compare a world that had already advanced.

**Before the reset (full script; slot 0 was EMPTY).**

```text
SHOW,save,slot=0,status=ok,bytes=2613
SHOW,slot,row=0,label=E1M1 HURT 00:10
SHOW,slot,row=1,label=EMPTY
SHOW,slot,row=2,label=EMPTY
SHOW,slot,row=3,label=EMPTY
SHOW,slot,row=4,label=EMPTY
SHOW,slot,row=5,label=EMPTY
SHOW,load,slot=0,status=ok,level=1,skill=3,tics=358
SHOW,frame=1141,x=69206016,y=-236978176,angle=1073741824,viewz=2686976,tics=358,health=100,...,hash=5e89591346c4222f86628b84235224df8c2b6254e7b06e77b0df3b13bc11a436
SHOW,save,slot=1,status=ok,bytes=2615
SHOW,slot,row=0,label=E1M1 HURT 00:10
SHOW,slot,row=1,label=E1M1 HURT 00:17
SHOW,slot,row=2,label=EMPTY
SHOW,slot,row=3,label=EMPTY
SHOW,slot,row=4,label=EMPTY
SHOW,slot,row=5,label=EMPTY
```

**After the reset (load-only script, same image and same two records).**

```text
SHOW,persist,status=ok,settings=1,saves=1,slots=6,record=3968
SHOW,boot,screen=game,level=E1M1,open_ms=198
SHOW,menu,settings=nvs,skill=3,volume=5,gamma=3,mute=1,level=0
SHOW,frame00,x=69206016,y=-236978176,z=2686976,angle=0,hash=1d8b3849ab3f76750056937f2827bf2bcbb2b98e82976fb3a7ce263d5273c9d3
SHOW,slot,row=0,label=E1M1 HURT 00:10
SHOW,slot,row=1,label=E1M1 HURT 00:17
SHOW,slot,row=2,label=EMPTY
SHOW,slot,row=3,label=EMPTY
SHOW,slot,row=4,label=EMPTY
SHOW,slot,row=5,label=EMPTY
SHOW,load,slot=0,status=ok,level=1,skill=3,tics=358
SHOW,frame=93,x=69206016,y=-236978176,angle=1073741824,viewz=2686976,tics=358,health=100,...,hash=5e89591346c4222f86628b84235224df8c2b6254e7b06e77b0df3b13bc11a436
```

The two `SHOW,frame` lines are character-for-character equal in `x`, `y`,
`angle`, `viewz`, `tics`, `health` and `hash`; the two `SHOW,load` lines name the
same level, skill and tic; and `SHOW,menu,settings=nvs,skill=3,volume=5,
gamma=3,mute=1,level=0` is identical in both boots, so the settings record
crossed the same reset.

**A reset is not an unplug.** The proof uses the ESP32's RTS reset, not a
physical power cut. For this feature the distinction does not weaken the claim,
because the claim is *flash retention across a run*, not behaviour under a
brownout: the record is bytes in the `saves` NVS partition, no boot path erases
that partition, and the only writer is an explicit menu SAVE. A reset and an
unplug both leave those bytes in flash. What the reset does **not** prove is that
a record written while the supply is collapsing is atomic; the engine's
`RecordCodec` CRC is the guard for a torn write, and `savetest` below exercises
the rejection path.

**Six slots, and a corrupted record is refused.** The labels above are the six
rows the slot screen draws: two used, four `EMPTY`. A corrupt row is not loaded
and not labelled: `DoomPersistence::readSlotLabels()` asks the engine's
`loadRecord()` for each row, and a `Corrupt` answer leaves the row `EMPTY`;
`loadSlot()` would print `SHOW,load,slot=N,status=corrupt`. The board-level proof
is `esp32dev_ILI9341_hud_savetest`, which writes the live world to slot 5 through
the real `saveRecord()` envelope, then writes the same encoded record to slot 4
with one payload byte flipped in its RAM buffer and the slot store's raw
`write()` (so the stored CRC no longer covers the payload; calling `saveRecord()`
again would recompute it and store a valid record). Its boot line is:

```text
SHOW,savetest,valid=ok,corrupt=corrupt
```

`valid` is the engine's `StoreResult` for the untouched record and `corrupt` is
its answer for the flipped one. Slots 4 and 5 are used so the proof never touches
the record in slot 0. This is the one property no host gate can prove: the CRC
the engine installed on the board rejecting bytes on the board.

## Performance and settings

The frame time is documented per panel, and every setting behind the numbers is
written down once. All the measurements below are board captures on COM4 at
115200, reduced by `tools/perf_report.py` -- the command is the reproducibility,
not a table typed from memory.

Two stages are measured separately, and that separation is the point:

- **the world-pass stage**, `env:esp32dev_ILI9341_frame`, which renders the world
  pass alone (20 sampled views, two runs each, `-O2`), and
- **the shipped image**, `env:esp32dev_ILI9341_hud`, which draws the same world
  plus the status bar, the weapon and the sound seam.

Comparing the two directly would call three shipped features a regression. The
comparison is made on the world-pass stage; the shipped image is reconciled
against it member by member, below.

### The panels

| Panel | Role | Geometry | SPI | DMA block | Pixel format | Source |
| --- | --- | --- | --- | --- | --- | --- |
| ESP32-2432S028 CYD, ILI9341 | **shipped** | 240x320 panel rotated to a 320x240 frame (`DISPLAY_ROTATION=3`); 320x200 window at 1:1 with two 20 px bars | 40 MHz | 20 lines | RGB565 | `src/platforms/esp32_dev_ILI9341.h`, `platformio.ini` `[env:esp32dev_ILI9341]` |
| ST7789 240x240 | **bench, not shipped** | 240x180 window, 320x200 logical surface, `Y_OFF_SET=30` | 40 / 80 MHz | 60 lines | RGB565 | [`bench/README.md`](bench/README.md) |

The ST7789 is a bench row, not a configuration of this project. There is no
`esp32dev_ST7789` environment, and the bench rows were measured while the
firmware drove the square panel's pin map on the CYD (no CS, no backlight), so
nothing reached a panel: the SPI and DMA work is real but each figure is a
**pipeline** number, not a validated frame. Its recorded present cost for a
240x180 window, and the block-size effect beside it, are:

| Panel | SPI | Lines/block | `present_us` | Status |
| --- | --- | --- | --- | --- |
| ST7789 240x180 (bench) | 40 MHz | 60 | 18,198 | pipeline only |
| ST7789 240x180 (bench) | 40 MHz | 20 | 17,892 | pipeline only |
| ST7789 240x180 (bench) | 80 MHz | 60 | 9,557 | pipeline only |

On the shipped CYD the same 320x200 RGB565 window is 128,000 B, which is
25.6 ms of wire time at 40 MHz; the shipped `present_us` below is 26.98 ms, the
wire time plus the DMA tail and the Arduino `yield()`. That is the number the
present path is allowed to have, and it is measured, not modelled.

### The shipped image's frame (three 40 s runs)

`env:esp32dev_ILI9341_hud`, container at `0xC0000`. Each run is a separate reset
and a separate 40 s capture. The table below is **world frames only**: these
captures were taken while a bad solder joint on the Select pin (GPIO 35) kept
opening the automap by itself, and a map frame renders in about 0.9 ms, so those
samples are excluded and counted rather than left to flatter the band.

| phase | count | min | max | mean |
| --- | --- | --- | --- | --- |
| `frame_us` | 61 | 62,128 | 65,099 | 63,848.8 |
| `render_us` | 61 | 27,125 | 28,212 | 27,409.8 |
| `convert_us` | 61 | 0 | 0 | 0.0 |
| `present_us` | 61 | 26,967 | 26,993 | 26,978.6 |
| `update_us` | 61 | 1,461 | 3,972 | 3,005.9 |
| `hash_us` | 61 | 22,733 | 22,753 | 22,744.1 |
| `other_us` | 61 | 5,706 | 7,459 | 6,454.5 |
| `fps_x10` | 61 | 153 | 160 | 156.1 |

Three facts about that table. `convert_us` is 0 on this path: the engine's
present performs the 8bpp→RGB565 LUT, so `present_us` carries the conversion.
`hash_us` is the diagnostic SHA-256 over the index surface, ~22.7 ms, which the
firmware subtracts from the period: it is charged to the report, never to
`frame_us` or `fps_x10`. And `update_us` is the tic work -- input, thinkers and
`S_UpdateSounds` -- so the audio seam is there, not in `render_us`.

| run | `SHOW,frame` samples | world | excluded (automap) | `fps_x10` min-max (mean) | `render_us` mean |
| --- | --- | --- | --- | --- | --- |
| 1 | 36 | 17 | 19 | 154-160 (156.2) | 27,406.5 |
| 2 | 38 | 21 | 17 | 153-160 (155.7) | 27,429.8 |
| 3 | 37 | 23 | 14 | 153-160 (156.4) | 27,394.1 |
| **combined** | **111** | **61** | **50** | **153-160 (156.1)** | **27,409.8** |

The all-frame band, for completeness and labelled as such, is **153-190,
mean 168.6 over 111 samples**: every automap frame draws in under a millisecond
and raises the top of the band, which is exactly why it is not the shipping
number. The world band is stable across the three runs (153-160), and every
sample clears the rule's floor of 150.

**With the pin repaired.** A 45 s capture of the current shipped image (settings
reset to the defaults first) excludes **0** frames and reads `render_us`
**29,002.7** mean (28,849-29,663), `present_us` 26,987.9, `update_us` 2,667.4,
`frame_us` 64,996.7, `fps_x10` **151-157, mean 153.4** -- still above the 150
floor. The render is 1.6 ms above the three-run table: the image gained about
2.5 KB of code (level transitions) and moved, which is inside the ±1.3-3.3 ms
flash-placement band measured with the weapon A/B build
(`esp32dev_ILI9341_hud_noweapon`); no renderer code changed.

### The world-pass stage, and the reconciliation

`env:esp32dev_ILI9341_frame` prints one summary for the 20 sampled views, two
measured runs each. The line, verbatim from the capture; a second capture agrees
on the mean (20,035 us) and on the gate, with min/max moving by 8 us and 1 us:

```text
FRAMEPROF,summary,views=20,runs=2,samples=40,frame_mean_us=20035,frame_min_us=9051,frame_max_us=29337,walk_us=2755,walls_us=7788,planes_us=6033,masked_us=3291,other_us=168,residency_us=0,residency_permille=0,gate_us=20000,gate=fail
```

The probe's own gate is 20,000 us, so the summary reports `gate=fail` by 35 us.
Two captures agree on the mean to the microsecond, so this is not jitter, and the
honest record is a gate miss, not a rounding. It is within the ±1.3-3.3 ms
flash-placement sensitivity: neither the renderer's code nor the baked payloads
it reads sit at fixed places in the flash cache as the images around them change
(this probe image is 506,441 B). Which function pays it cannot be named from these
captures, and is not guessed here.

The shipped image's `render_us` mean is **27,409.8 us** against the world pass's
**20,035 us**, a difference of **7,374.8 us**. The draw-path features on top of
the world pass have recorded costs:

| added cost | recorded | source |
| --- | --- | --- |
| status bar / HUD | 4,780 us | `hud_us` 4.63-4.78 ms in the HUD captures |
| weapon sprite | 854-870 us | `weapon_us` 0.854-0.87 ms |
| letterbox | 0 | 20 px of unwritten panel outside the 320x200 surface; the present is unchanged |
| audio seam | not a render cost | `S_UpdateSounds` runs in the tic; its work is in `update_us` and no per-frame render figure was recorded |
| **attributed** | **5,650 us** | 76.6% of the difference |
| **unattributed** | **1,725 us** | 23.4% of the difference; 6.3% of the shipped render |

One like-for-like check before calling that gap final. The shipped image always
draws E1M1's spawn view, and `_frame`'s view 00 is 20,455 and 20,297 us in its
two runs (mean **20,376 us**), not the 20,035 us mean over all 20 sampled views.
Against the view the shipped image actually draws, the difference is **7,034 us**:
5,650 us is still attributed and **1,384 us** (19.7%) is not. The table above
uses the 20,035 us mean because that is the probe's summary; the view-00 baseline
is stated here so the reconciliation is not flattered by averaging in the
cheaper views.

The remainder does not close. The first place to look is the flag split the two
stages are built with: the shipped image is `-Os` (the framework appends it),
`_frame` removes `-Os` and keeps `-O2`, so the 20,035 us baseline is an `-O2`
world pass while the shipped world pass is compiled differently. That
difference lives inside the 1,725 us and cannot be separated from flash
placement with the captures that exist. It is reported as a gap, not assigned
to a feature to make the arithmetic tidy.

**No cheap win is left in the captures.** The frame is the renderer plus the
bus: `render_us` 27.4 ms and `present_us` 27.0 ms against a 63.8 ms period,
with `update_us` 3.0 ms and `other_us` 6.5 ms. There is no per-frame
recomputation in the captures that a small change removes; the remaining costs
are the features and the display settings below.

### Settings that produce the numbers

| Setting | Value | Why it is what it is | Measured or assumed |
| --- | --- | --- | --- |
| Build environment | `esp32dev_ILI9341_hud` (extends `_show` → `esp32dev_ILI9341`) | the shipped image; `_frame` is the world-pass measurement build and is never shipped | measured (capture) |
| Platform pin | `espressif32@7.1.2` = Arduino-ESP32 2.0.17 | load-bearing: an unpinned `pio pkg update` moves it to 8.0.0 (Arduino 3.x), which measured 686,446 B of flash and overflowed `dram0_0_seg` by 680 B. Bumping the pin is a repartition decision | measured |
| Optimization | shipped `-Os`; `_frame` `-O2` with `-Os` removed | the framework appends `-Os` after `build_flags`, so only `build_unflags = -Os` lets `-O2` win. The split is deliberate and is why the two stages are not byte-comparable | measured (`pio run -t envdump`: shipped `CCFLAGS` end `-O2, -Os`; `_frame` has no `-Os` and ends `-O2`) |
| DMA block | `PIXELROOT32_TFT_ESPI_LINES_PER_BLOCK=20` (`lib/platformio.ini` `[base_esp32]`) | two line buffers of `physicalWidth * 2 B * lines`; 20 lines at 320 px is 25,600 B, which is the DRAM budget; the engine's default is 60 (57,600 B at 240 px, 76,800 B at 320 px), which this project does not use | measured (40 MHz/60 18,198 vs 20 17,892 us on the bench) |
| SPI clock | 40 MHz (`SPI_FREQUENCY=40000000`) | 80 MHz corrupts this panel on this wiring and 55 MHz is not an achievable divider step | measured (bench) |
| Pixel format | RGB565 through a 256-entry LUT, byte-swapped for the panel | the engine's present path; DOOM renders 8bpp indices and pays one LUT lookup per pixel | measured (`present_us` matches 25.6 ms wire time) |
| Loop task stack | `ARDUINO_LOOP_STACK_SIZE=12288` | a fix, not a tuning knob: at the framework's 8 KB the firing path overflowed into the level's zone (see "Known limitations") | measured (`SHOW,stack`: peak 5,380 B) |
| Flash flags | `-fno-exceptions`, `-fno-rtti`, `-Wl,--gc-sections`, `-flto`/`-fno-lto`, `-ffunction-sections`, `-fdata-sections` | a flash/RAM budget decision from `[base]`/`[base_esp32]`, not a frame-time one | assumed frame-neutral (no A/B) |
| Letterbox | 20 px bars; `X_OFF_SET=20`, `Y_OFF_SET=0` pre-swap | the 320x200 window is 1:1 on the rotated 320x240 panel; the bars are panel rows nothing writes | measured (geometry) |
| Runtime defaults | `skill=2, volume=8, gamma=0, mute=0` | every capture boots `SHOW,menu,settings=default,...` after an NVS erase | measured (capture) |
| Settings storage | `nvs` partition, namespace `doom`, key `cfg`, 8 checksummed bytes | the record's bytes are a contract; the engine's `IKeyValueStore` keeps the same namespace and key so a stored record survives | measured (source + capture) |
| Save slots | six, 3,968 B each, `saves` at `0x3F8000` | not touched by a `_frame` flash because both environments use `partitions_wad.csv` | measured (`slots=6,record=3968`) |

### How to reproduce

```powershell
# World-pass stage: world pass alone, -O2, 20 sampled views x 2 runs
pio run -e esp32dev_ILI9341_frame
pio run -e esp32dev_ILI9341_frame -t upload --upload-port COM4
# reset: open COM4, RtsEnable=$true for 150 ms, RtsEnable=$false; capture 60 s
# the FRAMEPROF,summary,... line is the result

# Shipped image: three separate 40 s captures, each after its own reset
pio run -e esp32dev_ILI9341_hud -t upload --upload-port COM4
python tools/perf_report.py run1.log
python tools/perf_report.py run2.log
python tools/perf_report.py run3.log
python tools/perf_report.py run1.log run2.log run3.log   # combined band
```

The log format is the firmware's own, one line per reported frame:

```text
SHOW,frame=<n>,x=...,y=...,angle=...,viewz=...,tics=...,health=...,
render_us=...,convert_us=...,present_us=...,frame_us=...,fps_x10=...,
update_us=...,hash_us=...,other_us=...,refused=...,hash=...
```

`perf_report.py` reads the fields **by name**, so a field added later cannot
shift a column. It keeps `SHOW,frame` samples with `render_us > 20000` by
default, prints how many it excluded and why, and exits non-zero when a run has
no world frame or the world `fps_x10` minimum is under 150 (`--min-fps`).
`--all-frames` prints the unfiltered band, labelled. `--self-test` exercises
the parser and every rule on synthetic logs with no board; `--perturb` lowers
the parsed band (on a capture, or on its synthetic log when no file is given)
and must exit non-zero, which is what proves the gate can fail.
Both environments use `partitions_wad.csv`, so flashing `_frame` and then
`_hud` does not move `nvs`, `wad` or `saves`; leave the board on `_hud`.

## Testing

Run everything from **`games/doom`**. The suites and gates that read DOOM's data
need `tools/DOOM1.WAD` and `tools/DOOM1.whd` (see "Get the WAD and build the
container"); neither is committed.

**Host suites.** Nineteen `pio test` environments, one per `test/unit/` suite:

```powershell
foreach ($e in 'clock_test','frame_log_test','link_test','wad_test','whd_test',
               'zone_test','rdat_test','rbsp_test','rsegs_test','world_test',
               'frameprof_test','input_test','menu_test','baked_test',
               'flashprof_test','sound_test','boot_test','save_test','level_test') {
    pio test -e $e
}
```

**Gates.** Each `tools/*_check.py` compares the port's C or C++ against an
independent oracle or a committed record, and most take `--perturb` (and some
`--stub`) to prove the gate can fail:

```powershell
python tools\am_check.py --wad tools\DOOM1.WAD
python tools\fade_check.py --wad tools\DOOM1.WAD
python tools\hud_check.py --container tools\DOOM1.whd --wad tools\DOOM1.WAD
python tools\hud_draw_check.py --wad tools\DOOM1.WAD
python tools\weapon_draw_check.py --wad tools\DOOM1.WAD
python tools\menu_draw_check.py --wad tools\DOOM1.WAD
python tools\menu_check.py
python tools\menu_check.py --container tools\DOOM1.whd --wad tools\DOOM1.WAD
python tools\menu_check.py --arena tools\DOOM1.whd
python tools\palflash_check.py --wad tools\DOOM1.WAD --container tools\DOOM1.whd
python tools\rsegs_check.py --wad tools\DOOM1.WAD
python tools\capacity_check.py --wad tools\DOOM1.WAD
python tools\attack_check.py --wad tools\DOOM1.WAD
python tools\damage_check.py --wad tools\DOOM1.WAD
python tools\enemy_check.py --wad tools\DOOM1.WAD
python tools\door_check.py --wad tools\DOOM1.WAD
python tools\specials_check.py --wad tools\DOOM1.WAD
python tools\movement_check.py --wad tools\DOOM1.WAD
python tools\movement_check.py --wad tools\DOOM1.WAD --player
python tools\things_check.py --wad tools\DOOM1.WAD
python tools\level_check.py --wad tools\DOOM1.WAD
python tools\demo_check.py --wad tools\DOOM1.WAD --strict --demo DEMO1
python tools\demo_check.py --wad tools\DOOM1.WAD --strict --demo DEMO2
python tools\demo_check.py --wad tools\DOOM1.WAD --strict --demo DEMO3
python tools\sfx_check.py --wad tools\DOOM1.WAD
python tools\music_check.py --wad tools\DOOM1.WAD
python tools\boot_check.py
python tools\bake_graphics.py --check-container tools\DOOM1.whd --wad tools\DOOM1.WAD
python tools\whd_encode.py --check tools\DOOM1.whd
python tools\perf_report.py --self-test
python tools\perf_report.py --perturb          # must exit 1
```

Every gate exits 0 except `perf_report.py --perturb`, which exits 1 by design.
The `demo_check` runs replay the WAD's three demos through the port's simulation
and require every stream to be consumed with both readers agreeing.

## Known limitations

- **E1M1's music on every level.** The 4 MB build ships one theme; the
  intermission and title are silent (see "Music").
- **No screen melt, no finale, no intermission.** Level changes use a palette
  fade; E1M8's ending opens the main menu (see "Level transitions").
- **239 sprite frames are not in the container.** None is reachable: the sprite
  table is the union bake (216 names from the nine levels' THINGS and state
  chains) plus the 28 weapon patches kept raw by name. To restore one, first make
  it addressable: add it to the union bake (`tools/e1m1_sprites.py`,
  `tools/sprite_check.py`) or to a raw-by-name list, rebuild the container, and
  keep it under `kBakedCapacity` (384; currently `manifest=370/384`) and inside
  the container's spare (296,708 B; all 239 need 271,532 B).
- **A 16 MB ESP32-S3 profile is the home for the remaining cuts:** per-level
  music, the dropped sprite frames, vanilla's screen melt (two more 64,000 B
  surfaces) and a twin framebuffer. None of it exists yet.
- **The world-pass probe misses its own gate by 35 us** (20,035 against 20,000),
  and **1,725 us of the shipped render is unattributed** (see "Performance and
  settings"). Both are inside the flash-placement band; recovering them is a
  placement or renderer investigation.
- **`esp32dev_ILI9341_frameram` cannot link, by design.** It is an A/B control
  (the map decoded into RAM) and is never shipped; its environment comment
  records that `dram0_0_seg` has 256 B free where the walkable capacity set needs
  15,424 B. It exists to prove a measurement.
- **The loop task's stack is 12 KB, and that is a fix.** At the framework's 8 KB,
  entering the room before E1M1's exit and firing panicked the board
  (`LoadProhibited` inside `z_zone.c`'s `refreshLargestFree`) or hung it with no
  output: the firing path (`P_AimLineAttack` + `P_LineAttack`, each with a 768 B
  intercept array, then the intercept walk and `P_SpawnMobj`) overflowed the
  stack into the level's zone. Measured peak 5,380 B with `SHOW,stack`; 12 KB
  leaves 6.9 KB, the arena still fits (`chosen=18432`), and the boot's heap keeps
  25,912 B free. A change that deepens a call chain has to move this number with
  a measurement.
- **A reflash can lose the settings record** (`boot_app0.bin` at `0xE000`), never
  the saves (see "Saves").
- **Atomicity under power loss is not proven.** The save proof uses an RTS reset;
  the `RecordCodec` CRC is the guard for a torn write.
- **Proven by gates, not by eye or ear.** `sfx_check` proves each cue is the sound
  it claims to be, not that it sounds good; that needs a listening pass. The
  palette flashes and the map colours are proven by gates and hashes, not by
  looking at the panel, and GPIO 34's electrical path was proven with a scripted
  press, not a pressed button.

## Project layout

```text
games/doom/
├── platformio.ini          # every environment: native, the CYD builds, diagnostics, host tests
├── lib/platformio.ini      # [base] / [base_native] / [base_esp32] templates, platform pin
├── partitions_wad.csv      # the shipped flash table (nvs / factory / wad / saves)
├── partitions_nowad.csv    # a table with no wad row, for the failure-screen proof
├── lib/doom/               # the DOOM core (GPLv2, see License and lib/doom/NOTICE)
├── src/
│   ├── main.cpp            # platform selector only
│   ├── DoomScene.h/.cpp    # composition root: menu, automap, palette, audio, saves
│   ├── doomc/              # the C<->C++ seam: FrameDriver, WAD/container readers,
│   │                       # baked graphics, audio, saves, boot and failure screens
│   ├── palette/            # PLAYPAL, gamma and fade tables
│   ├── perf/               # the SHOW,frame log
│   ├── time/               # the drift-free 35 Hz tic clock
│   ├── assets/music/       # the generated E1M1 track
│   └── platforms/          # native.h, esp32_dev_ILI9341.h
├── tools/                  # container encoder, generators, gates (*_check.py), perf_report.py
├── test/unit/              # the 19 host suites
└── bench/                  # standalone measurement firmware (not shipped)
```

## Measurement bench

[`bench/`](bench/) is a standalone PlatformIO project, not a demo: it sits at
depth 4, so the demo CI does not discover it, and it is not shipped. It measured
the numbers the port is built on — SPI clock, DMA block size, fill throughput,
DRAM census, the flash partition map and the letterbox. Its own README carries
the env table, the serial commands and the results.

## License

The DOOM demo is distributed under the **GNU General Public License, version 2**
([`LICENSE`](LICENSE)). It links id Software's DOOM source, which is copyleft,
so the firmware built here is a GPLv2 combined work.

The port relies on the 1999 GPLv2 release of that source; the tree it ports
from (`linuxdoom-1.10`, pinned commit `4eb368a9`) still carries the 1997 Doom
Source Code License headers. [`lib/doom/NOTICE`](lib/doom/NOTICE) records both,
lists which files are derived from upstream and which are original to this port,
and is the document to read before redistributing anything here.

**This does not reach the rest of the repository.** The engine
(PixelRoot32-Game-Engine) is MIT, lives in its own repository and contains no
DOOM code; MIT is compatible with the GPL, so the combined firmware is GPLv2
while the engine stays MIT for every other game. The root [`LICENSE`](../../LICENSE)
carries the same scope note, and every other demo is unaffected because none of
them contains or links this code. The boundary is the binary, not the directory:
if DOOM's source ever entered the engine or a shared library, every game would
be reached, which is why the engine-side invariant is that it mentions DOOM
nowhere.

**Assets are not GPL.** `DOOM1.WAD` is id Software's shareware data under its own
terms; neither it nor the container built from it is committed. See
"Get the WAD and build the container" above.
