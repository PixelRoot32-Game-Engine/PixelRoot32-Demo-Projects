#!/usr/bin/env python3
"""Gate for the menu state machine, the settings codec and the gamma table (T39, D5.2).

``lib/doom/m_menu.c`` is driven by ``tools/menu_check.c`` from scripts and prints one
line of state per command. This file holds the expectation, and it is built two
ways so a wrong C file cannot agree with its own mistake:

* **anchors** -- literal expected lines, written by hand from linuxdoom's
  ``m_menu.c`` rules: the Start edge opens and closes the menu, the cursor wraps, BACK closes the
  main screen, a New Game reports its skill once, a slider clamps at both ends and
  reports a change only when the value moved;
* **a reference model** -- an independent Python implementation of the same rules
  (it shares no code with the C), replaying 3,000 pseudo-random button frames; the
  C and the model must print the same state after every frame.

It also pins the persisted form (golden bytes, and eight kinds of damage that must
fall back to the DEFAULTS rather than be half-trusted), the clamping, and the gamma
table (level 0 is the identity, every level is monotonic and brighter than the one
before, and each equals ``round(255 * (i / 255) ** (1 - g / 8))``).

Run it:

    python tools/menu_check.py                         # state machine, codec, gamma
    python tools/menu_check.py --perturb               # the model clamps wrongly: must FAIL
    python tools/menu_check.py --container tools/DOOM1.whd --wad tools/DOOM1.WAD
                                                       # the menu art is raw, aligned, equal
    python tools/menu_check.py --container ... --wad ... --perturb   # must FAIL
    python tools/menu_check.py --arena tools/DOOM1.whd # E1M1 loads at every skill in the board's arena

Exit 0 when every check passed, 1 when one failed, 2 on unusable input.
"""
from __future__ import annotations

import argparse
import random
import re
import shutil
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

ROOT = Path(__file__).resolve().parent.parent
LIB = ROOT / "lib" / "doom"

UP, DOWN, LEFT, RIGHT, A, B, START = 0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40
DEFAULTS = (2, 8, 0)
SKILLS, VOLUME_MAX, GAMMA_MAX = 5, 15, 4

# screen -> number of rows
ROWS = {0: 2, 1: 5, 2: 3}


# --- The reference model (no code shared with the C) ------------------------------

class Model:
    """m_menu.c's menu_t / M_Responder rules, restated from the spec."""

    def __init__(self, settings=DEFAULTS, perturb=False):
        self.skill, self.volume, self.gamma = (self._clamp(settings))
        self.perturb = perturb
        self.active = 0
        self.screen = 0
        self.item = [0, self.skill, 0]
        self.skull = 0
        self.counter = 10
        self.latched = 0
        self.prev = 0
        self.changed = 0
        self.pending = -1

    @staticmethod
    def _clamp(settings):
        s, v, g = settings
        return (min(s, SKILLS - 1), min(v, VOLUME_MAX), min(g, GAMMA_MAX))

    def line(self, world=None):
        text = (f"STATE active={self.active} screen={self.screen} item={self.item[self.screen]}"
                f" skull={self.skull} skill={self.skill} volume={self.volume} gamma={self.gamma}")
        if world is not None:
            text += f" world=0x{world:02X}"
        return text

    def open(self):
        if not self.active:
            self.active = 1
            self.screen = 0

    def close(self):
        self.active = 0
        self.screen = 0

    def _slide(self, row, direction):
        if row == 0:
            new = self.skill + direction
            low, high = 0, SKILLS - 1
        elif row == 1:
            new = self.volume + direction
            low, high = 0, VOLUME_MAX
        else:
            new = self.gamma + direction
            low, high = 0, GAMMA_MAX
        if self.perturb:
            high += 1  # the injected bug: one step past the top
        new = max(low, min(high, new))
        old = (self.skill, self.volume, self.gamma)[row]
        if new != old:
            self.changed = 1
            if row == 0:
                self.skill = new
            elif row == 1:
                self.volume = new
            else:
                self.gamma = new

    def key(self, key):
        if not self.active:
            return
        rows = ROWS[self.screen]
        current = self.item[self.screen]
        if key == "down":
            self.item[self.screen] = (current + 1) % rows
        elif key == "up":
            self.item[self.screen] = (current - 1) % rows
        elif key in ("left", "right"):
            if self.screen == 2:
                self._slide(current, -1 if key == "left" else 1)
        elif key == "enter":
            if self.screen == 0:
                if current == 0:
                    self.screen = 1
                    self.item[1] = self.skill
                else:
                    self.screen = 2
            elif self.screen == 1:
                self.pending = current
                if self.skill != current:
                    self.skill = current
                    self.changed = 1
                self.close()
            else:
                self._slide(current, 1)
        elif key == "back":
            if self.screen == 0:
                self.close()
            else:
                self.screen = 0

    def buttons(self, held):
        rising = held & ~self.prev
        self.prev = held
        if self.latched:
            if held == 0:
                self.latched = 0
            return 0
        if not self.active:
            if rising & START:
                self.open()
                self.latched = 1
                return 0
            return held & ~START
        if rising & START:
            self.close()
            self.latched = 1
            return 0
        if (rising & (LEFT | RIGHT)) == (LEFT | RIGHT):
            rising &= ~(LEFT | RIGHT)
        for mask, name in ((UP, "up"), (DOWN, "down"), (LEFT, "left"), (RIGHT, "right"),
                           (A, "enter"), (B, "back")):
            if rising & mask and self.active:
                self.key(name)
        if not self.active:
            self.latched = 1
        return 0

    def tick(self, tics):
        for _ in range(tics):
            self.counter -= 1
            if self.counter <= 0:
                self.skull ^= 1
                self.counter = 8

    def take(self):
        started = self.pending >= 0
        skill = self.pending
        self.pending = -1
        changed = self.changed
        self.changed = 0
        return f"TAKE newgame={int(started)} skill={skill if started else -1} changed={changed}"


# --- A scripted run: commands in, expected lines out -------------------------------

class Run:
    def __init__(self):
        self.commands: list[str] = []
        self.expected: list[str] = []

    def add(self, command: str, expected: str) -> None:
        self.commands.append(command)
        self.expected.append(expected)


def state(active=0, screen=0, item=0, skull=0, skill=2, volume=8, gamma=0, world=None):
    text = (f"STATE active={active} screen={screen} item={item} skull={skull}"
            f" skill={skill} volume={volume} gamma={gamma}")
    if world is not None:
        text += f" world=0x{world:02X}"
    return text


def anchors() -> Run:
    """Hand-written expectations, one per rule."""
    run = Run()
    add = run.add
    add("init", state())
    # LEFT alone is the game's: the world sees it and the menu stays shut.
    add("buttons 0x04", state(world=LEFT))
    add("buttons 0x00", state(world=0))
    # The LEFT+RIGHT chord is the game's now (both cancel to no turn): it does not open.
    add("buttons 0x0C", state(world=LEFT | RIGHT))
    add("buttons 0x00", state(world=0))
    # Start opens the menu on the main screen and the world sees nothing; holding it
    # is no second edge, and the open menu stays open.
    add("buttons 0x40", state(active=1, world=0))
    add("buttons 0x40", state(active=1, world=0))
    add("buttons 0x00", state(active=1, world=0))
    # DOWN moves to OPTIONS, DOWN again wraps to NEW GAME, UP wraps back.
    add("buttons 0x02", state(active=1, item=1, world=0))
    add("buttons 0x00", state(active=1, item=1, world=0))
    add("buttons 0x02", state(active=1, item=0, world=0))
    add("buttons 0x00", state(active=1, item=0, world=0))
    add("buttons 0x01", state(active=1, item=1, world=0))
    add("buttons 0x00", state(active=1, item=1, world=0))
    # OPTIONS, then the three rows in order, each reachable.
    add("buttons 0x10", state(active=1, screen=2, item=0, world=0))
    add("buttons 0x00", state(active=1, screen=2, item=0, world=0))
    add("buttons 0x02", state(active=1, screen=2, item=1, world=0))
    add("buttons 0x00", state(active=1, screen=2, item=1, world=0))
    add("buttons 0x02", state(active=1, screen=2, item=2, world=0))
    add("buttons 0x00", state(active=1, screen=2, item=2, world=0))
    # GAMMA: RIGHT up to the top, one more clamps and reports nothing new.
    for value in (1, 2, 3, 4):
        add("buttons 0x08", state(active=1, screen=2, item=2, gamma=value, world=0))
        add("buttons 0x00", state(active=1, screen=2, item=2, gamma=value, world=0))
    add("take", "TAKE newgame=0 skill=-1 changed=1")
    add("take", "TAKE newgame=0 skill=-1 changed=0")
    add("buttons 0x08", state(active=1, screen=2, item=2, gamma=4, world=0))
    add("buttons 0x00", state(active=1, screen=2, item=2, gamma=4, world=0))
    add("take", "TAKE newgame=0 skill=-1 changed=0")
    # BACK returns to MAIN with the cursor where it was (OPTIONS), BACK closes.
    add("buttons 0x20", state(active=1, screen=0, item=1, gamma=4, world=0))
    add("buttons 0x00", state(active=1, screen=0, item=1, gamma=4, world=0))
    add("buttons 0x20", state(active=0, screen=0, item=1, gamma=4, world=0))
    # B is still held after closing: the world must not see a USE.
    add("buttons 0x20", state(active=0, screen=0, item=1, gamma=4, world=0))
    add("buttons 0x00", state(active=0, screen=0, item=1, gamma=4, world=0))
    add("buttons 0x20", state(active=0, screen=0, item=1, gamma=4, world=B))
    # Start closes an open menu from any screen (Esc toggles in DOOM), once per press,
    # and the world sees nothing until it is released.
    add("init", state())
    add("buttons 0x40", state(active=1, world=0))
    add("buttons 0x00", state(active=1, world=0))
    add("buttons 0x10", state(active=1, screen=1, item=2, world=0))
    add("buttons 0x00", state(active=1, screen=1, item=2, world=0))
    add("buttons 0x40", state(active=0, screen=0, item=0, world=0))
    add("buttons 0x40", state(active=0, screen=0, item=0, world=0))
    add("buttons 0x00", state(active=0, screen=0, item=0, world=0))
    # Start with a game button in the same frame: the menu opens, the world sees nothing.
    add("buttons 0x50", state(active=1, screen=0, item=0, world=0))
    add("buttons 0x00", state(active=1, screen=0, item=0, world=0))
    add("init", state())
    # New Game: the skill row starts on the saved skill, DOWN moves, ENTER starts.
    add("init 2 8 0", state())
    add("open", state(active=1))
    add("buttons 0x10", state(active=1, screen=1, item=2, world=0))  # opened by `open`, not latched
    add("buttons 0x00", state(active=1, screen=1, item=2, world=0))
    add("buttons 0x02", state(active=1, screen=1, item=3, world=0))
    add("buttons 0x00", state(active=1, screen=1, item=3, world=0))
    add("buttons 0x10", state(active=0, screen=0, item=0, skill=3, world=0))
    add("take", "TAKE newgame=1 skill=3 changed=1")
    add("take", "TAKE newgame=0 skill=-1 changed=0")
    return run


def fuzz(seed: int, frames: int, perturb: bool) -> Run:
    """Random button frames and ticks, mirrored by the model."""
    rng = random.Random(seed)
    model = Model(perturb=perturb)
    run = Run()
    run.add("init", model.line())
    held = 0
    for _ in range(frames):
        roll = rng.random()
        if roll < 0.08:
            tics = rng.randint(1, 12)
            model.tick(tics)
            run.add(f"tick {tics}", model.line())
        elif roll < 0.12:
            run.add("take", model.take())
        elif roll < 0.15:
            held = START
            world = model.buttons(held)
            run.add(f"buttons 0x{held:02X}", model.line(world))
        else:
            if rng.random() < 0.55:
                held = 0
            else:
                held = 1 << rng.randint(0, 6)
                if rng.random() < 0.2:
                    held |= 1 << rng.randint(0, 6)
            world = model.buttons(held)
            run.add(f"buttons 0x{held:02X}", model.line(world))
    run.add("take", model.take())
    return run


def sliders(perturb: bool) -> Run:
    """Every slider driven to both ends and past them, with `enter` as a step up."""
    model = Model(perturb=perturb)
    run = Run()
    run.add("init", model.line())
    model.open()
    run.add("open", model.line())
    plan = ["down", "enter"]                       # OPTIONS
    plan += ["left"] * 6 + ["right"] * 7           # skill: 2 -> 0 (clamp), then to 4 (clamp)
    plan += ["down"] + ["right"] * 9 + ["enter"]   # volume: 8 -> 15 (clamp), enter at the top
    plan += ["left"] * 17 + ["enter", "enter"]     # volume: down to 0 (clamp), enter steps up
    plan += ["down"] + ["left"] * 2 + ["enter"] * 6  # gamma: 0 (clamp), then up to 4 (clamp)
    plan += ["down", "up", "up", "back", "back"]
    for key in plan:
        model.key(key)
        run.add(f"key {key}", model.line())
    run.add("take", model.take())
    return run


def skull_run(perturb: bool) -> Run:
    model = Model(perturb=perturb)
    run = Run()
    run.add("init", model.line())
    for tics in (9, 1, 7, 1, 8, 8, 30):
        model.tick(tics)
        run.add(f"tick {tics}", model.line())
    # Hand-written: 10 tics to the first flip, then 8 each.
    run.add("init", state())
    run.add("tick 9", state(skull=0))
    run.add("tick 1", state(skull=1))
    run.add("tick 7", state(skull=1))
    run.add("tick 1", state(skull=0))
    return run


# --- The persisted form ---------------------------------------------------------------

def checksum(body: bytes) -> int:
    return (0xA5 + sum(body)) & 0xFF


def blob(skill, volume, gamma) -> bytes:
    body = bytes([0x44, 0x4D, 1, skill, volume, gamma, 0])
    return body + bytes([checksum(body)])


def codec_run(perturb: bool) -> Run:
    run = Run()
    add = run.add
    default_line = "DECODE ok=0 skill=2 volume=8 gamma=0"
    for settings in ((3, 12, 2), (2, 8, 0), (0, 0, 0), (4, 15, 4)):
        encoded = blob(*settings).hex().upper()
        add(f"encode {settings[0]} {settings[1]} {settings[2]}", f"BLOB {encoded}")
        add(f"decode {encoded}", f"DECODE ok=1 skill={settings[0]} volume={settings[1]} gamma={settings[2]}")
    # Golden bytes, computed by hand: 'D' 'M' v1 skill vol gamma 0 + (0xA5 + sum).
    add("encode 3 12 2", "BLOB 444D01030C020048")
    add("encode 2 8 0", "BLOB 444D010208000041")
    good = blob(3, 12, 2)
    damaged = {
        "empty": "-",
        "short": good[:7].hex(),
        "long": (good + b"\x00").hex(),
        "magic": (b"X" + good[1:]).hex(),
        "version": (good[:2] + b"\x02" + good[3:]).hex(),
        "checksum": (good[:7] + bytes([good[7] ^ 0x01])).hex(),
        "skill_out_of_range": blob(5, 8, 0).hex(),
        "volume_out_of_range": blob(2, 16, 0).hex(),
        "gamma_out_of_range": blob(2, 8, 5).hex(),
        "erased_flash": ("FF" * 8),
        "zeros": ("00" * 8),
    }
    for name, hexed in damaged.items():
        add(f"decode {hexed}", default_line if not perturb or name != "checksum"
            else "DECODE ok=1 skill=3 volume=12 gamma=2")
    add("clamp 9 99 9", "CLAMP skill=4 volume=15 gamma=4")
    add("clamp 1 2 3", "CLAMP skill=1 volume=2 gamma=3")
    return run


def gamma_checks(output: dict[str, str]) -> list[str]:
    failures = []
    tables = {}
    for line in output.values():
        match = re.match(r"GAMMADUMP level=(\d+) (.*)", line)
        if match:
            tables[int(match.group(1))] = [int(v) for v in match.group(2).split()]
    for level in range(GAMMA_MAX + 1):
        table = tables.get(level)
        if table is None or len(table) != 256:
            failures.append(f"gamma level {level}: no 256-entry dump")
            continue
        if level == 0 and table != list(range(256)):
            failures.append("gamma level 0 is not the identity")
        expected = [min(255, int(255.0 * (v / 255.0) ** (1.0 - level / 8.0) + 0.5)) if level else v
                    for v in range(256)]
        if table != expected:
            first = next(i for i in range(256) if table[i] != expected[i])
            failures.append(f"gamma level {level}: value {first} is {table[first]}, formula says {expected[first]}")
        if any(table[i] > table[i + 1] for i in range(255)):
            failures.append(f"gamma level {level} is not monotonic")
        if table[0] != 0 or table[255] != 255:
            failures.append(f"gamma level {level} does not keep black and white")
        if level and any(tables[level][i] < tables[level - 1][i] for i in range(256)):
            failures.append(f"gamma level {level} is darker than level {level - 1}")
        if level and sum(tables[level]) <= sum(tables[level - 1]):
            failures.append(f"gamma level {level} is not brighter overall than level {level - 1}")
    # Entries of id's table the maintainer's notes carry: [1][1]=2, [2][1]=4, [4][1]=16, [4][2]=23.
    for level, value, wanted in ((1, 1, 2), (2, 1, 4), (3, 1, 8), (4, 1, 16), (4, 2, 23)):
        if tables.get(level) and tables[level][value] != wanted:
            failures.append(f"gamma[{level}][{value}] is {tables[level][value]}, expected {wanted}")
    return failures


# --- Running the C -------------------------------------------------------------------

def build(workdir: Path, compiler: str) -> Path:
    executable = workdir / ("menu_check.exe" if sys.platform == "win32" else "menu_check")
    command = [compiler, "-std=gnu99", "-O2", "-Wall", "-Wextra", "-I", str(LIB),
               "-o", str(executable), str(ROOT / "tools" / "menu_check.c"),
               str(LIB / "m_menu.c"), str(LIB / "m_gamma.c")]
    result = subprocess.run(command, capture_output=True, text=True)
    if result.returncode != 0:
        raise RuntimeError("the harness did not compile:\n" + (result.stderr or result.stdout))
    return executable


def execute(executable: Path, workdir: Path, run: Run, tag: str) -> list[str]:
    script = workdir / f"{tag}.txt"
    script.write_text("\n".join(run.commands) + "\n", encoding="utf-8")
    result = subprocess.run([str(executable), str(script)], capture_output=True, text=True)
    if result.returncode != 0:
        raise RuntimeError(f"harness exited {result.returncode}: {result.stderr}")
    return result.stdout.splitlines()


def compare(name: str, run: Run, actual: list[str]) -> list[str]:
    failures = []
    if len(actual) != len(run.expected):
        failures.append(f"{name}: {len(actual)} lines from C, {len(run.expected)} expected")
    for index, (want, got) in enumerate(zip(run.expected, actual)):
        if want != got:
            failures.append(f"{name}: line {index + 1} after `{run.commands[index]}`\n"
                            f"    expected {want}\n    actual   {got}")
            break
    return failures


def state_machine(arguments) -> int:
    workdir = Path(tempfile.mkdtemp(prefix="menu_check_"))
    try:
        try:
            executable = build(workdir, arguments.cc)
        except (RuntimeError, OSError) as error:
            print(f"menu_check: {error}", file=sys.stderr)
            return 2
        failures: list[str] = []
        runs = {
            "anchors": anchors(),
            "skull": skull_run(arguments.perturb),
            "sliders": sliders(arguments.perturb),
            "codec": codec_run(arguments.perturb),
        }
        for seed in range(1, 7):
            runs[f"fuzz{seed}"] = fuzz(seed, 500, arguments.perturb)
        total = 0
        for name, run in runs.items():
            try:
                actual = execute(executable, workdir, run, name)
            except RuntimeError as error:
                print(f"menu_check: {error}", file=sys.stderr)
                return 2
            total += len(run.commands)
            failures += compare(name, run, actual)
        # The gamma dumps are compared with the formula, not with a model.
        dump = Run()
        for level in range(GAMMA_MAX + 1):
            dump.add(f"gammadump {level}", "")
        dump_lines = execute(executable, workdir, dump, "gamma")
        failures += gamma_checks({str(i): line for i, line in enumerate(dump_lines)})
        for failure in failures:
            print(f"MENUCHECK,fail,{failure}")
        print(f"MENUCHECK,commands={total},failures={len(failures)},perturb={int(arguments.perturb)}")
        return 1 if failures else 0
    finally:
        shutil.rmtree(workdir, ignore_errors=True)


# --- The container gate (the menu art is raw, aligned, equal to the WAD's) -----------------

def container_gate(arguments) -> int:
    import bake_graphics as bake
    import gen_menu_lumps
    import whd_encode
    from weapon_check import capacity, lump_table

    try:
        container = bytearray(arguments.container.read_bytes())
        _, lumps, _ = whd_encode.read_wad(arguments.wad)
    except (OSError, whd_encode.InputError) as error:
        print(f"menu_check: {error}", file=sys.stderr)
        return 2
    source = {lump.name.upper(): lump.data for lump in lumps}
    names, rows = lump_table(bytes(container))
    manifest_index = names.get(bake.MANIFEST_NAME)
    if manifest_index is None:
        print("menu_check: the container has no ~PWBAKE~ manifest", file=sys.stderr)
        return 1
    start, stored, _, _ = rows[manifest_index]
    entries = bake.parse_manifest(bytes(container[start:start + stored]))

    wanted = gen_menu_lumps.menu_names(set(source))
    failures: list[str] = []
    if len(entries) > capacity():
        failures.append(f"manifest holds {len(entries)} records, kBakedCapacity is {capacity()}")
    registered = [entry.name for entry in entries if entry.name.upper() in set(wanted)]
    if registered:
        failures.append(f"menu names registered in the manifest (DRAM slots): {registered[:3]}")
    if arguments.perturb and wanted[0] in names:
        container[rows[names[wanted[0]]][0]] ^= 0xFF

    raw_bytes = 0
    for name in wanted:
        index = names.get(name)
        if index is None:
            failures.append(f"{name}: not in the container")
            continue
        payload_start, stored_size, _, method = rows[index]
        if method != whd_encode.METHOD_RAW:
            failures.append(f"{name}: stored with method {method}, a menu picture is raw")
            continue
        if payload_start % whd_encode.RAW_ALIGNMENT != 0:
            failures.append(f"{name}: raw payload at {payload_start} is not 4-byte aligned")
            continue
        payload = bytes(container[payload_start:payload_start + stored_size])
        if payload != source[name]:
            failures.append(f"{name}: payload differs from the source WAD lump")
            continue
        try:
            if bake.parse_patch(name, payload) != bake.parse_patch(name, source[name]):
                failures.append(f"{name}: decoded columns differ from the source patch")
                continue
        except bake.BakeError as error:
            failures.append(f"{name}: does not decode as a patch: {error}")
            continue
        raw_bytes += stored_size
    for failure in failures:
        print(f"MENULUMPS,fail,{failure}")
    print(f"MENULUMPS,names={len(wanted)},failures={len(failures)},raw_bytes={raw_bytes},"
          f"manifest={len(entries)}/{capacity()},perturb={int(arguments.perturb)}")
    return 1 if failures else 0


# --- The arena gate (New Game reloads E1M1 at every skill inside the board's arena) ------

ARENA_SOURCES = (
    "m_menu.c", "m_gamma.c", "p_setup.c", "p_mobj.c", "p_map.c", "p_user.c", "p_tick.c",
    "p_action.c", "p_inter.c", "p_pspr.c", "p_sight.c", "p_enemy.c", "m_random.c", "z_zone.c",
    "doom_map.c", "g_game.c", "whd.c", "r_segs.c", "r_light.c", "r_bsp.c", "r_plane.c",
    "r_tables.c", "r_data.c", "w_wad.c", "p_spec.c", "p_floor.c", "p_doors.c", "p_plats.c",
    "p_ceilng.c", "p_lights.c",
)


def arena_gate(arguments) -> int:
    workdir = Path(tempfile.mkdtemp(prefix="menu_arena_"))
    try:
        executable = workdir / ("menu_arena.exe" if sys.platform == "win32" else "menu_arena")
        command = [arguments.cc, "-std=gnu99", "-O2", "-Wall", "-Wextra", "-DMENU_CHECK_ARENA",
                   "-I", str(LIB), "-o", str(executable), str(ROOT / "tools" / "menu_check.c")]
        command += [str(LIB / name) for name in ARENA_SOURCES]
        result = subprocess.run(command, capture_output=True, text=True)
        if result.returncode != 0:
            print("menu_check: the arena harness did not compile:\n" + (result.stderr or result.stdout),
                  file=sys.stderr)
            return 2
        # --perturb leaves the renderer's live reader bound across the reload, which is
        # the defect the board showed (every frame refused after the first New Game).
        extra = ["--bound"] if arguments.perturb else []
        result = subprocess.run([str(executable), "--arena", str(arguments.arena)] + extra,
                                capture_output=True, text=True)
        sys.stdout.write(result.stdout)
        if result.stderr:
            sys.stderr.write(result.stderr)
        return result.returncode
    finally:
        shutil.rmtree(workdir, ignore_errors=True)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--cc", default="gcc")
    parser.add_argument("--container", type=Path, help="gate the finished container's menu art")
    parser.add_argument("--wad", type=Path, help="the source IWAD (with --container)")
    parser.add_argument("--arena", type=Path, help="prove New Game fits the board's arena at every skill")
    parser.add_argument("--perturb", action="store_true", help="inject a bug; the run must then fail")
    arguments = parser.parse_args(argv)
    if arguments.arena is not None:
        return arena_gate(arguments)
    if arguments.container is not None:
        if arguments.wad is None:
            parser.error("--container needs --wad")
        return container_gate(arguments)
    return state_machine(arguments)


if __name__ == "__main__":
    sys.exit(main())
