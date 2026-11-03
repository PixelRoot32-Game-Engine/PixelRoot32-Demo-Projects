# DOOM on PixelRoot32 — ESP32 port: promotion handover (D7.4)

This is the note to read before promoting the DOOM port. It records both tracker
branches at the heads they had when it was written, what each carries, what the
engine dependency pin must become, the evidence behind the claims, and what is
still open. It changes no code, opens no pull request, merges no branch and
flips no pin (S20; the plan's §4.6: promotion is outside every phase).

**The heads this note is accurate at.** Both checkouts were clean at those heads
before this note was added (`git status -sb`), and both tracker branches matched
their origin copies.

| Repository | Branch | Head | Base | Ahead of base |
| --- | --- | --- | --- | --- |
| `PixelRoot32-Demo-Projects` | `feature/doom-esp32` | `3552cff` | `origin/main` `1ede84f` | 118 commits |
| `PixelRoot32-Game-Engine` | `feature/doom-esp32` | `08e9c3a` | `origin/develop` `59d0154` | 30 commits: 18 APU + 12 port |

The note was first written against `2dbe26f` (T55) and refreshed at `3552cff`
after T57 (the exit transition) and T58 (the mapped PLAYPAL page). Documentation
commits follow that code head; they change no code, and the evidence below is
from the code head. The engine
head is `08e9c3a`, the port's twelfth commit: the audio-test fix described in
section 4.4. The suite and gate results below name the commit each was measured
at; each board capture names the commit that produced it.

## 1. The two branches

### The demo branch

`feature/doom-esp32` in `PixelRoot32-Demo-Projects` is **118 commits ahead of
`origin/main` (`1ede84f`), 245 files changed, +128,169 / −1**
(`git rev-list --count origin/main..HEAD`, `git diff --stat origin/main...HEAD`).
The delta is `games/doom/**` (243 files) plus two repository-level files:
`README.md` (the demo table, T20) and `LICENSE` (the directory-scoped GPLv2
scope note, T19). Nothing else in the repository moves.

`games/doom/` contains:

- `src/` — the engine seam and everything the port owns in C++: `DoomScene`,
  `FrameDriver` (session, open, render), `BootScreen`/`LevelFailure` (D7.2),
  `SaveGame`/`DoomPersistence` (D7.1), the SFX and music banks, `SfxCueEvent`,
  `AudioTelemetry`, `InputMap`, `TicClock`, `palette/`.
- `lib/doom/` — the GPLv2 DOOM core: the renderer (`r_*`), the simulation
  (`p_*`), `s_sound`/`sounds`, `m_menu`, `am_map`, `st_stuff`, `r_psprite`,
  the WAD/PWHD readers and the zone. See `lib/doom/NOTICE`.
- `bench/` — the Phase 0 measurement project (a separate PlatformIO project
  inside the demo, at `games/doom/bench`, **not** a top-level
  `games/doom-bench`). It is not shipped and the demo CI does not discover it.
- `tools/` — the gates (`*_check.py`), the deterministic generators
  (`sfx_design`, `mus2tracks`, `gen_*_lumps`), the container encoder
  (`whd_encode.py`, `make_container.ps1`) and `perf_report.py` (D7.3).
- `test/unit/` — the 19 host suites.
- `platformio.ini` (42 environments: 23 device/measurement and 19 host test)
  and `partitions_wad.csv`.

### The engine branch

`feature/doom-esp32` in `PixelRoot32-Game-Engine` is **30 commits ahead of
`origin/develop` (`59d0154`)**, but it is **not a DOOM-only delta**.
`origin/feature/apu-hybrid-synthesis` (`10d9060`, 18 commits over `develop`) is
an **ancestor** of it:

```text
$ git merge-base --is-ancestor origin/feature/apu-hybrid-synthesis feature/doom-esp32
$ echo $?
0
$ git rev-list --count origin/develop..feature/doom-esp32
30
$ git rev-list --count origin/develop..origin/feature/apu-hybrid-synthesis
18
```

The port's own commits are the **12 on top** (section 2.2). The other 18 are the
APU hybrid-synthesis line: sound profiles, the render-block contract, the DAC
and I2S backend work, and the audio documentation/example commits. **This is the
fact the note exists to surface:** promoting the engine branch as-is also brings
the APU line, which is not on `develop` -- and the maintainer's decision (3.2)
is that 1.14.0 ships it.

The engine side exists because the port needed three things the released engine
did not have: a present path whose frame transfer can be closed before the next
frame (the 20-line DMA block, S10), a caller-supplied 8bpp expansion table (the
port's PLAYPAL path), and the persistence module (D7.1's save slots and the
settings record). It contains no DOOM source (S3); the GPL boundary is the
demo's binary, not the engine tree.

### Where the images and artifacts live

- **Shipped environment:** `esp32dev_ILI9341_hud` (extends
  `esp32dev_ILI9341_show` → `esp32dev_ILI9341`) over
  `partitions_wad.csv`. The shipped app is **663,049 B of the 720,896 B
  `factory` row** (57,847 B free, `seg_free=1,124`; README "Level transitions"
  and T58).
- **Container:** `tools/DOOM1.whd`, **3,168,508 B**, flashed at **`0xC0000`**
  into the `wad` row (`0xC0000..0x3F8000`, 3,375,104 B). It is built by
  `tools/make_container.ps1` from the shareware `DOOM1.WAD`; neither the WAD nor
  any `.whd` is committed (`tools/.gitignore`).
- **Save slots:** `saves` at `0x3F8000..0x400000`, 32 KB, six 3,968 B slots.
- **Board state** left by T54/T55/T57/T58: the shipped `_hud` image, `nvs`
  intact (defaults, unmuted), `saves` erased, six `EMPTY` slots, `frame00`
  `1d8b3849ab3f76750056937f2827bf2bcbb2b98e82976fb3a7ce263d5273c9d3`.
- **Captures and host logs are not committed.** They live outside the tree;
  this note quotes their decisive lines.

## 2. The commit series per phase

The demo's 118 commits bucket into the plan's phases by their subjects and the
feature record's T rows. Counts and ranges were read from
`git log --reverse --format='%h %s' origin/main..HEAD`.

| Phase | Commits | Range (oldest → newest) | What it delivered |
| --- | --- | --- | --- |
| D0 | 12 | `7c8b937` → `2cba7e1` | Scaffold and the Phase 0 bench: fill throughput, DRAM census, present timing, letterbox, partition/mmap; the CYD correction, 40 MHz / 20-line present and the pushed tracker branch. |
| D1 | 6 | `6308d92` → `8ffe22e` | `DoomScene` shell, the palette contract, the drift-free 35 Hz tic clock, the frame-pacing harness, the C↔C++ seam, Start/Select on the spare inputs. |
| D2 | 7 | `008e9d4` → `24821f0` | WAD reader, the compressed PWHD container, mmap/lump resolution, the zone arena, picture decoders, the flash-read profile, root-table inflate. |
| D3 | 12 | `54efdf9` → `934e0d6` | BSP walk, walls, planes and sky, masked sprites, the frame probe, the flash-resident map, the graphics bake, the 19.8 ms gate and the 3 MiB episode container. |
| D4 | 33 | `ac9cda7` → `56d7002` | Thinkers, doors/lifts/floors, first light, line specials, movement and player at 35 Hz, GPL/README, damage, weapons, monster AI, the three demos, exits, stairs/teleport, scrolling/animation. |
| D5 | 19 | `06e2b02` → `fcca065` | Status bar and face, the weapon, the menu and settings, palette flashes, the automap, the palette fade. |
| D6 | 10 | `74feff3` → `9c595f4` | The `S_*` layer, the fitted SFX bank, MUS→four-track music, APU telemetry, the ten-minute soak, volume/mute. |
| D7 | 19 | `df0250b` → `3552cff` | Boot/error screens and the WAD-missing board proof, the save record and six slots with board proof, the performance report and the settings record. |

Totals: 12 + 6 + 7 + 12 + 33 + 19 + 10 + 19 = **118**.

### 2.1 The engine's 12 port commits

`git log --oneline origin/feature/apu-hybrid-synthesis..feature/doom-esp32`
returns exactly these, newest first:

| Commit | What it changed |
| --- | --- |
| `08e9c3a` | The two audio queue tests now size to `AudioCommandQueue::CAPACITY - 1`, the ring's usable slots; APU 2.2.0 lowered the capacity from 128 to 64 (section 4.4). |
| `6229ae3` | The smoke tool's partition-table copy follows the DOOM table's D7.1 split. |
| `1ff5b64` | Document the persistence module. |
| `6e8155b` | The ESP32 NVS store (`Esp32NvsSlotStore`/`Esp32NvsKeyValueStore`) and its board smoke tool. |
| `83e8af6` | The host-file store for native builds. |
| `6ab6fe3` | The persistence service facade. |
| `8b9c2af` | The key-value and slot store contracts (`IKeyValueStore`, `ISlotStore`). |
| `123e650` | Store results, key rules and the `RecordCodec` envelope (16-byte header + CRC-32). |
| `f5ceb33` | The default-off `PIXELROOT32_ENABLE_PERSISTENCE` flag. |
| `b630a8e` | Keep driver names out of the draw-surface contract. |
| `3a5f1d7` | Let a caller supply the 8bpp expansion table. |
| `f82d838` | Allow the frame transfer to be closed before the next frame. |

### 2.2 The APU-line caveat

`origin/feature/apu-hybrid-synthesis` (`10d9060`) is 18 commits over `develop`
and is an ancestor of the port branch:

```text
1298a9f feat(audio): raise the default audio block to 512/1024
807378a feat(audio): make the backends honour the render block
574ee47 feat(audio): honour the render block in the DAC backend
5260ff9 docs(audio): document the render block contract
d7d83dd test(audio): add the on-board underrun gate
6f8b9d0 test(audio): gate each board at its own block size
ee90b86 test(audio): make the underrun gate prove it rendered something
cabf963 test(audio): account the render cost separately from the block
... plus the sound-profile example and its PR merges
```

Those commits are why the port could be built against the APU the way S2 asked,
but they are **not** part of the DOOM work and are **not** on `develop`.
Promoting the engine branch as it stands promotes them too. The pin
recommendation in section 3 is written around that fact.

## 3. What the pin should become

### 3.1 The pin today

Both firmware environments in `games/doom/platformio.ini` name the engine
repository **by branch**:

```ini
[env:native]                                              ; line 36
lib_deps = https://github.com/PixelRoot32-Game-Engine/PixelRoot32-Game-Engine.git#feature/doom-esp32

[env:esp32dev_ILI9341]                                    ; line 74
lib_deps = https://github.com/PixelRoot32-Game-Engine/PixelRoot32-Game-Engine.git#feature/doom-esp32
```

The host-only `link_test` and `sound_test` environments add
`gperez88/PixelRoot32-APU@^2.0.0` explicitly (lines 702 and 1191); the firmware
gets the same APU transitively through the engine package's own dependency. The
caret resolves to **2.2.0** today. The platform is pinned at
`espressif32@7.1.2` (Arduino-ESP32 2.0.17) in `lib/platformio.ini:44`.

After promotion both engine lines become the registry form the other demos use
(`games/pong/platformio.ini:28` pins
`gperez88/PixelRoot32-Game-Engine@^1.13.0`):

```ini
[env:native]                                              ; line 36
lib_deps = gperez88/PixelRoot32-Game-Engine@^1.14.0

[env:esp32dev_ILI9341]                                    ; line 74
lib_deps = gperez88/PixelRoot32-Game-Engine@^1.14.0
```

The caret matches the team's convention. If byte-reproducible builds matter more
than patch uptake, drop it and pin `1.14.0` exactly -- section 3.5 is the
argument for that reading.

### 3.2 The release is 1.14.0, and what it carries is decided

Maintainer, verbatim:

> Sobre el engine la version 1.14.0 saldra tanto con los dos pr que estan abierto como con los cambios de la rama feature/doom-esp32

So **1.14.0 = `develop` (`59d0154`) + PR #278 (`feature/apu-hybrid-synthesis`) +
PR #279 (`fix/native-sdl-teardown`) + the 12 port commits**. That settles the
choice this note would otherwise have left open: the APU line ships inside
1.14.0, and the port's commits are not rebased onto a bare `develop`.

The checklist below is therefore how the release is **verified**, not an open
question:

- `PIXELROOT32_ENABLE_PERSISTENCE`, default 0, defined in
  `include/platforms/PlatformDefaults.h:355-356`;
- `include/persistence/*` (`KeyValueStore.h`, `SlotStore.h`, `StoreTypes.h`,
  `RecordCodec.h`, `PersistenceService.h`, `MemoryStores.h`) and
  `src/persistence/*`;
- `include/drivers/esp32/Esp32NvsStore.h` and
  `include/drivers/native/HostFileStore.h`;
- the two graphics changes (`f82d838`, `3a5f1d7`) and the draw-surface change
  (`b630a8e`);
- the two audio test fixes in `08e9c3a` (so 1.14.0's own suites run green on
  APU 2.2.0).

PR #279 is not the port's work; it belongs to the native SDL path and its own
review. It matters here only because a failure in that path in 1.14.0 is not a
DOOM regression.

### 3.3 The one path, and what it settles

Because 1.14.0 carries both open PRs and the tracker branch, the earlier
question -- APU line first, or the 12 rebased onto `develop` -- is answered by
the release itself: the APU line lands with it. Consequences:

- The 12 stay as commits. Until 1.14.0 is cut,
  `git log --oneline origin/feature/apu-hybrid-synthesis..feature/doom-esp32`
  is the exact list; after it, their subjects identify them in the release.
- The port's pin needs no rebase step, and the promotion order loses a branch.
- The release notes should name the APU line, because it is a real part of what
  1.14.0 ships even though it has its own PR (#278).
- If #278 or #279 changes before the release, re-check the 12 against the final
  tree: only `f82d838` and `3a5f1d7` touch shared graphics code, and both are
  build-level contracts rather than behavior changes.

### 3.4 The order

1. **Engine 1.14.0.** Merge PR #278 and PR #279, land the tracker branch's 12
   commits, cut 1.14.0.
2. **Demo pin second.** Change the two `lib_deps` lines (36 and 74) to
   `gperez88/PixelRoot32-Game-Engine@^1.14.0`; align the explicit APU line to
   the version 1.14.0 declares (today `^2.0.0` resolves to 2.2.0, which the
   demo's suites already pass).
3. **Demo merge last.** Only after the pin resolves and the tree builds from the
   released engine.

### 3.5 Do not run `pio pkg update` before the pin changes

D7.1 measured this the hard way. The platform was unpinned, an update moved it
from `espressif32@7.1.2` (Arduino-ESP32 2.0.17) to `8.0.0` (Arduino 3.x), and
the same sources measured **686,446 B of flash** against the 646,177 B the slot
depended on — an overflow of `dram0_0_seg` by 680 B, and **+40,269 B** of flash
(README "Settings that produce the numbers"; T53). The pin was restored to
`7.1.2`; bumping it is a repartition decision, not a routine update.

The APU caret is the same class of risk. `^2.0.0` resolved to **2.2.0**, whose
`AudioCommandQueue::CAPACITY` shrank from 128 to 64; two engine unit tests
written against 128 assumed the old capacity, and the port fixed them in
`08e9c3a` (section 4.4). The demo's own suites are green on 2.2.0, but the
lesson stands: align the APU line to whatever 1.14.0 declares, and prefer an
exact version if reproducibility matters more than patch uptake.

### 3.6 What does not change

The platform pin (`espressif32@7.1.2`), the partition table, the container, the
20-line DMA block and the 40 MHz SPI clock are **not** part of promotion. The
only line that changes is the engine `lib_deps` in the two environments.

## 4. Evidence

The demo's suites were last re-run after T57 and T58; the gate table is from
the T58 run at `1b74081` (the container changed there, so its container gates
are the relevant ones). Engine results are at `08e9c3a`. Each board capture
names the commit that produced it.

### 4.1 The demo's host suites

All **19** host test environments pass (`level_test`, added by T57, is the
nineteenth). Total: **401 test cases, 0 failed**.

| Environment | Last line |
| --- | --- |
| `clock_test` | `7 test cases: 7 succeeded` |
| `frame_log_test` | `6 test cases: 6 succeeded` |
| `link_test` | `8 test cases: 8 succeeded` |
| `wad_test` | `23 test cases: 23 succeeded` |
| `whd_test` | `42 test cases: 42 succeeded` |
| `zone_test` | `22 test cases: 22 succeeded` |
| `rdat_test` | `21 test cases: 21 succeeded` |
| `rbsp_test` | `13 test cases: 13 succeeded` |
| `rsegs_test` | `40 test cases: 40 succeeded` |
| `world_test` | `91 test cases: 91 succeeded` |
| `frameprof_test` | `17 test cases: 17 succeeded` |
| `input_test` | `13 test cases: 13 succeeded` |
| `menu_test` | `21 test cases: 21 succeeded` |
| `baked_test` | `15 test cases: 15 succeeded` |
| `flashprof_test` | `13 test cases: 13 succeeded` |
| `sound_test` | `24 test cases: 24 succeeded` |
| `boot_test` | `8 test cases: 8 succeeded` |
| `save_test` | `8 test cases: 8 succeeded` |
| `level_test` | `9 test cases: 9 succeeded` |

### 4.2 The demo's gates

All gate invocations are green on the tree that holds the WAD and the container
`tools/DOOM1.WAD` / `tools/DOOM1.whd`. The brief said "27 gates"; the set it
enumerated is **25 gate scripts, run as 29 invocations** (three `menu_check`
modes and three `demo_check` demos). `perf_report --perturb` exits **1 by
design** and is listed last.

| Gate | Last decisive line |
| --- | --- |
| `am_check --wad tools/DOOM1.WAD` | `am_check: 10 states, 0 failing` |
| `fade_check --wad tools/DOOM1.WAD` | `OK monotone: every channel rises with the scale` |
| `hud_check --container tools/DOOM1.whd --wad tools/DOOM1.WAD` | `HUDCHECK,names=86,failures=0,raw_bytes=61628,manifest=370/384,perturb=0` |
| `hud_draw_check --wad tools/DOOM1.WAD` | `hud_draw_check: 11 states, 0 failing`; `frame00 1d8b3849...` |
| `weapon_draw_check --wad tools/DOOM1.WAD` | `weapon_draw_check: 15 states, 0 failing` |
| `menu_draw_check --wad tools/DOOM1.WAD` | `menu_draw_check: 13 states, 0 failing` |
| `menu_check` (model) | `MENUCHECK,commands=3277,failures=0,perturb=0` |
| `menu_check --container ... --wad ...` | `MENULUMPS,names=80,failures=0,raw_bytes=46692,manifest=370/384,perturb=0` |
| `menu_check --arena tools/DOOM1.whd` | `ARENASUMMARY chosen=30720 skills=5 ok=1` |
| `palflash_check --wad ... --container ...` | `PALFLASH,states=8779,tables=70,decay=5,failures=0,perturb=0` |
| `rsegs_check --wad tools/DOOM1.WAD` | `rsegs_check: C and the oracle agree on all 84 lines` |
| `capacity_check --wad tools/DOOM1.WAD` | `capacity_check: every walkable viewpoint and every scripted tic rendered` |
| `attack_check --wad tools/DOOM1.WAD` | `attack_check: C and the oracle agree on every line` |
| `damage_check --wad tools/DOOM1.WAD` | `damage_check: C and the oracle agree on every line` |
| `enemy_check --wad tools/DOOM1.WAD` | `enemy_check: C and the oracle agree, and all nine assertions hold` |
| `door_check --wad tools/DOOM1.WAD` | `door_check: C and the oracle agree, and all three door assertions hold` |
| `specials_check --wad tools/DOOM1.WAD` | `specials_check: C and the oracle agree on every line` |
| `movement_check --wad tools/DOOM1.WAD` | `movement_check: C and the oracle agree on every line` |
| `movement_check --wad ... --player` | `movement_check: C and the oracle agree on every line` |
| `things_check --wad tools/DOOM1.WAD` | `things_check: C and the oracle agree on every line, for all five skills` |
| `level_check --wad tools/DOOM1.WAD` | `level_check: nine level changes, every world the WAD's own, the arena re-laid and E1M9 reached` |
| `demo_check --strict --demo DEMO1` | `DEMO1 consumed its whole stream, the two readers agree, and every coherence assertion holds` |
| `demo_check --strict --demo DEMO2` | `DEMO2 consumed its whole stream, the two readers agree, and every coherence assertion holds` |
| `demo_check --strict --demo DEMO3` | `DEMO3 consumed its whole stream, the two readers agree, and every coherence assertion holds` |
| `sfx_check --wad tools/DOOM1.WAD` | `OK sfx_check: 108 measured, 49 pass, 59 accepted-deviation, 0 failed` |
| `music_check --wad tools/DOOM1.WAD` | `OK music_check: 3 lumps converted, 12 tracks, D_E1M1 committed` |
| `boot_check` | `OK record: every title, line, footer and hash matches tools/boot_screens.txt` |
| `bake_graphics --check-container tools/DOOM1.whd --wad tools/DOOM1.WAD` | `BAKECHECK,entries=370,walls=101,sky=1,flats=52,sprites=216,compared=370,mismatches=0,bytes=1742916` |
| `whd_encode --check tools/DOOM1.whd` | `tools/DOOM1.whd: 913 lumps, 3,168,508 B`; `659 raw lumps, every payload on a 4-byte boundary` |
| `perf_report --self-test` | `SELFTEST: 12 cases passed` |
| `perf_report --perturb` | `PERTURB: simulated verdict FAIL (exit 1) -- the rule can fail` |

### 4.3 Board captures

These are the readings the record pins; they were taken on COM4 at 115200 with
the shipped `_hud` image unless named otherwise.

**D7.2 — the two WAD-missing branches (T51, commits `9d38337`, `d92933f`).**

- No `wad` row (`esp32dev_ILI9341_hud_nowad` over `partitions_nowad.csv`):
  `SHOW,setup,status=fail,reason=open,code=3` and
  `SHOW,boot,screen=error,reason=partition_missing,code=3,detail=`; panel
  `NO WAD SLOT`; no `SHOW,frame`, no panic.
- Erased container over the shipped table:
  `SHOW,setup,status=fail,reason=open,code=5` and
  `SHOW,boot,screen=error,reason=container_invalid,code=5,detail=`; panel
  `BAD CONTAINER`. The container was restored with `write_flash`, verified with
  `verify_flash`, read back byte for byte (the 3,439,476 B container in the tree
  at T51; the post-T53b container is 3,167,944 B — 0 differing,
  `sha256 1CEF57C7...`), and the good boot returned with `open_ms=196` and
  `frame00 1d8b3849...`.

**D7.1 — save, reset, load, and the corrupted-record refusal (T54, commits
`c279ed5`, `db1b748`).** One image, two boots, no reflash between them:

- Boot A (empty slot 0, full script): `SHOW,save,slot=0,status=ok,bytes=2613`
  then `SHOW,load,slot=0,status=ok,level=1,skill=3,tics=358`; the frame line
  `...x=...,y=...,angle=...,viewz=...,tics=358,health=100,...,hash=5e8959...`.
- Boot B (RTS reset, load-only script): the same record loads with the same
  `x/y/angle/viewz/tics/health` and the same `hash=5e8959...`; the settings line
  is identical in both boots.
- `esp32dev_ILI9341_hud_savetest`: `SHOW,savetest,valid=ok,corrupt=corrupt` —
  the engine's `RecordCodec` CRC rejects the flipped-byte record on the board.
- The six slots are labelled from headers (`E1M1 HURT 00:10`, `E1M1 HURT
  00:17`, four `EMPTY`), and the final board state is NVS + `saves` erased,
  defaults, unmuted, six `EMPTY` slots.

**D7.3 — the final frame record (T55, commits `54d652f`, `2dbe26f`).** World
frames only, three 40 s `_hud` runs (61 of 111 samples; 50 automap frames are excluded and counted -- the capture
predates the GPIO-35 repair, when the pin floated open):

- `render_us` 27,409.8 mean (min 27,125 / max 28,212),
  `present_us` 26,978.6, `update_us` 3,005.9, `other_us` 6,454.5,
  `frame_us` 63,848.8, `fps_x10` **153-160, mean 156.1**.
- Phase 3 stage, `esp32dev_ILI9341_frame`: `FRAMEPROF,summary,...frame_mean_us=20035,...gate_us=20000,gate=fail`
  — +59 us (0.30 %) over L17's 19,976 and +235 us (1.19 %) over D3.9's 19,800,
  i.e. 35 us over the probe's own gate. It is inside the ±1.3-3.3 ms
  flash-placement sensitivity D5.1 measured and is recorded as a gate miss, not
  assigned to a change.
- Reconciliation: the shipped render minus the world pass is 7,374.8 us; HUD
  (4,780) + weapon (870) explain 5,650 us (76.6 %); **1,725 us** (23.4 %) is
  unattributed, or **1,384 us** against the like-for-like view-00 baseline
  (20,376 us). `-Os`/`-O2` and placement are the named candidates; the captures
  cannot separate them.

### 4.4 The engine's suites

`pio test` on the five engine environments, at `08e9c3a`, with the APU
dependency resolving to **2.2.0**. One invocation with all five `-e` flags:

| Environment | Result |
| --- | --- |
| `native_test` | green |
| `native_test_gameplay` | green |
| `native_test_physics_rest` | green |
| `native_test_networking` | green |
| `native_test_persistence` | green |

```text
========= 8202 test cases: 33 skipped, 8169 succeeded in 00:29:03.867 =========
```

**What was red, and what fixed it.** `PixelRoot32-APU` **2.2.0** shrank
`AudioCommandQueue::CAPACITY` from 128 to 64 (its
`include/pixelroot32/apu/AudioCommandQueue.h` documents both the RAM reason and
the rule that the ring keeps one slot free, so a capacity of N accepts N - 1
commands). The engine's test files are unchanged since `develop`
(`git diff --stat origin/develop..feature/doom-esp32 -- test/unit/test_audio_command_queue test/unit/test_audio_scheduler`
is empty), and `develop` declares the same `gperez88/PixelRoot32-APU@^2.0.0`
caret, so the two tests failed on the base as well -- a floating-dependency
finding, not a port regression.

- `test_audio_command_queue_mpsc_single_producer_baseline` enqueued a literal 64
  and expected every enqueue to succeed; the 64th is the reserved-slot refusal.
- `test_audio_scheduler_command_queue_overflow` submitted 64 and expected zero
  drops; 2.2.0 drops the 64th.

`08e9c3a` sizes both loops to `AudioCommandQueue::CAPACITY - 1`. The tests keep
the intent they were written for (fill the ring, drain it, prove no command is
dropped) and stay correct if the capacity changes again; the scheduler test now
includes the queue header explicitly instead of relying on a transitive one.
Verified in isolation before the full run: `unit/test_audio_command_queue` 7/7
and `unit/test_audio_scheduler` 44/44.

The port's own audio proofs are on the demo side and are green: `sound_test`
(24 cases) and `link_test` (8 cases).

### 4.5 CI did not run, and cannot be made to run without a forbidden action

- The demo workflow (`.github/workflows/build.yml`) triggers on **`pull_request`
  and `workflow_dispatch` only**. A tracker branch has no run, and opening a PR
  to get one is forbidden (S5). The local evidence above is the proof.
- The engine's `tests.yml` triggers on **push to `main`/`develop` and PRs to
  those branches**; it has no `workflow_dispatch`. `examples.yml` does expose
  `workflow_dispatch` but builds examples, not `pio test`.
- Nothing was dispatched and no PR was opened. If the maintainer wants a remote
  demo run, the one line is (from `PixelRoot32-Demo-Projects`):

  ```bash
  gh workflow run build.yml --ref feature/doom-esp32
  ```

  Dispatching it is a remote action and was left to the maintainer.

## 5. What is not done and known caveats

These are the open items the maintainer should weigh, stated with the reason
each exists rather than hidden.

- **239 dropped sprite frames.** T53b trimmed the container on the maintainer's
  decision to fit D7.1: 239 sprite lumps that no list can name (264,819 B
  stored, 402,464 B uncompressed) were dropped by
  `tools/whd_encode.py --drop-unused-sprites` (`partitions_wad.csv` header).
  None was reachable — the sprite table is the union bake (216 names from the
  nine levels' THINGS and state chains) plus the 28 weapon patches kept raw by
  name. **To restore one**, a future feature must first make the frame
  addressable: add it to the union bake (`tools/e1m1_sprites.py`,
  `tools/sprite_check.py`) or to a raw-by-name list, rebuild the container, keep
  it under `kBakedCapacity` (384; currently `manifest=370/384`), and fit it in
  the 207,160 B of container spare (all 239 would need 271,532 B, so a bulk
  restore is a repartition decision, not a rebuild).
- **`boot_app0` at `0xE000` and `keep_nvs.py`.** `pio run -t upload` always
  writes `boot_app0.bin` (8 KB) at `0xE000`, the last page of `nvs`
  (`0x9000..0xF000`), so a reflash **can** lose the settings record. It
  **cannot** touch `saves` at `0x3F8000`. The engine already ships the fix in
  its smoke tool: `tools/persist_nvs_smoke/keep_nvs.py` is an
  `extra_scripts = post:` hook that drops the `0xE000` image from
  `FLASH_EXTRA_IMAGES`/`UPLOADERFLAGS`. It is not wired into this project
  because the T54 record survived and the board was left on erased defaults.
- **Reset is not unplug.** T54's power-cycle proof is the ESP32's **RTS reset**,
  not a physical power cut. The claim is flash retention across a run: the
  record is bytes in the `saves` NVS partition, no boot path erases it, and only
  a menu SAVE writes it. What the reset does **not** prove is atomicity during a
  collapsing supply; the engine's `RecordCodec` CRC and the `savetest` rejection
  are the guard for a torn write.
- **GPIO 35 is fixed in hardware (2026-11-02).** The maintainer found the
external pull-up resistor badly soldered with a false contact, and re-soldered
it. Select is now the only way into the automap; the D7.3 captures predate the
repair, and their world-frame filter (`render_us > 20000`) stays because map
frames still exist whenever Select is pressed. The rule is unchanged: GPIO
34/35 are input-only with no internal pull-up, so the board's external resistor
is what holds the line, and `analogRead()` remains forbidden there.
- **`_frame` is 20,035 us, 35 us over its own 20,000 gate.** Two captures agree
  to the microsecond, so it is a real gate miss, not jitter; it is +59 us over
  L17's 19,976 and inside the ±1.3-3.3 ms placement band. Recovering those 35 us
  is a placement/renderer investigation, not a D7.3 edit (L60).
- **1,725 us of render is unattributed.** HUD 4,780 + weapon 870 explain 5,650
  of the 7,374.8 us delta from the Phase 3 stage; the remaining 1,725 us
  (1,384 us against view 00) is reported as a gap. `-Os`/`-O2` and flash
  placement are the candidates; the captures cannot separate them.
- **`esp32dev_ILI9341_frameram` is a control that cannot link.** It is D3.7's
  A/B build (the map decoded into RAM) and is **never shipped**; its own
  environment comment records that `dram0_0_seg` has 256 B free where the
  walkable capacity set needs 15,424 B (`platformio.ini:286-306`). It exists to
  prove a measurement, not to be a candidate.
- **The listening pass and the panel visual checks are still the maintainer's.**
  `sfx_check` proves a cue *is* the sound it claims to be (49 pass, 59 recorded
  accepted-deviation, 0 fail); whether it sounds good is a listening pass. The
  palette flashes and the map colours were proven by gates and hashes, but the
  panel was not looked at (T44, T46, T49). GPIO 34's real electrical path was
  proven with a scripted press, not a pressed button (T40).
- **The S3 profile (16 MB board) is the home for the remaining cuts.**
  **Per-level music**: the 4 MB build plays E1M1's track on every level by
  design (S23); the other eight themes are ~270 KB, more than the app's 57,843 B
  free and more than the container's 206,596 B of spare at 16 B/note. The 16 MB
  profile is where each id gets its own track, and only `MusicBank`'s lookup
  changes. Intermission music (`D_INTER`, 63,600 B) does not fit either; title
  music (`D_INTRO`, 1,280 B) would fit but no scene requests `mus_intro`. The
  239 dropped sprite frames, vanilla's screen melt (`f_wipe`, two more 64,000 B
  surfaces) and the twin framebuffer are all S3-profile work (S12, S15, README
  "Music", `partitions_wad.csv`).
- **Documentation debt.** The root `README.md`'s DOOM row still says "Still in
  Phase 4: … monsters and weapons do not yet" and the demo README's opening
  banner still calls the project a scaffold. Both are stale relative to
  `3552cff`; they are documentation, not behaviour, and were left untouched by
  this note.

## 6. No PR, no merge, no pin flip

- **No pull request was opened** in either repository.
- **No branch was merged** into `develop` or `main`.
- **No pin was flipped.** Both `lib_deps` lines still name
  `#feature/doom-esp32`.
- **No CI was dispatched.**
- Promotion — naming 1.14.0, changing the
  pin and merging the demo — is the maintainer's, per S5 and PLAN §4.6.

## 7. How to verify it yourself

`pio` is not on `PATH` on this machine; use
`C:\Users\<user>\.platformio\penv\Scripts\pio.exe`. Run from each repository at
the heads named in the table above.

### 7.1 Demo

```powershell
git -C PixelRoot32-Demo-Projects rev-parse HEAD        # 3552cff...
git -C PixelRoot32-Demo-Projects status --short        # clean

cd games/doom

# The 19 host suites
foreach ($e in 'clock_test','frame_log_test','link_test','wad_test','whd_test',
               'zone_test','rdat_test','rbsp_test','rsegs_test','world_test',
               'frameprof_test','input_test','menu_test','baked_test',
               'flashprof_test','sound_test','boot_test','save_test','level_test') {
    pio test -e $e
}

# The gates that matter most (all 29 are in section 4.2)
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

The WAD and container are not committed. `tools/DOOM1.WAD` is the shareware
v1.9 file and `tools/DOOM1.whd` is built by `tools/make_container.ps1 -Wad
<path>`; both are gitignored.

### 7.2 The board capture sequence

D7.1 (save / reset / load) uses `esp32dev_ILI9341_hud_menutest`; one image, two
boots, **no reflash between them**:

```text
esptool erase_region 0x3F8000 0x8000          # six EMPTY slots
pio run -e esp32dev_ILI9341_hud_menutest -t upload --upload-port COM4
# boot A: capture 100 s (settings, six New Games, SAVE slot 0, LOAD slot 0, SAVE slot 1)
# reset: open COM4, RtsEnable=$true for 150 ms, RtsEnable=$false
# boot B: capture 30 s (load-only script)
```

D7.2 (the two WAD-missing branches) uses `partitions_nowad.csv` for
`partition_missing`, then erases and restores the container for
`container_invalid`:

```text
pio run -e esp32dev_ILI9341_hud_nowad -t upload --upload-port COM4
esptool erase_region 0xC0000 0x338000
esptool write_flash 0xC0000 tools/DOOM1.whd
esptool verify_flash 0xC0000 tools/DOOM1.whd
```

D7.3 (the frame record) is three separate 40 s `_hud` captures, each after its
own reset, reduced by `tools/perf_report.py`:

```text
pio run -e esp32dev_ILI9341_hud -t upload --upload-port COM4
python tools\perf_report.py run1.log
python tools\perf_report.py run1.log run2.log run3.log
pio run -e esp32dev_ILI9341_frame -t upload --upload-port COM4
# capture 60 s; the FRAMEPROF,summary line is the Phase 3 result
```

### 7.3 Engine

```powershell
git -C PixelRoot32-Game-Engine rev-parse HEAD        # 08e9c3a...
git -C PixelRoot32-Game-Engine status --short        # clean
cd PixelRoot32-Game-Engine

pio test -e native_test -e native_test_gameplay -e native_test_physics_rest `
         -e native_test_networking -e native_test_persistence
```

Expect `8202 test cases: 33 skipped, 8169 succeeded`, 0 failed, at `08e9c3a`
(section 4.4).
