#!/usr/bin/env python3
"""Play the shareware demos through lib/doom and pin what they produce (D4.4).

WHAT THIS GATE IS

A recorded demo is 1993 DOOM's own input, so a port that replays it faithfully
has to agree with upstream on every P_Random() draw, every state transition and
every thinker's order. That is the strongest fidelity statement this project can
make without a reference port, and this gate is how it is made measurable:

    python tools/demo_check.py --wad ... [--demo DEMO1] [--perturb skip]

It builds tools/demo_check.c against the real world core, plays one demo from
its header to its terminator, and checks four things:

  1. THE STREAM IS THE DEMO'S. The C reader and this script both decode the lump
     (this one from the WAD, with its own header parser and its own four-byte
     command rule) and must agree on every header field, on the recorded tic
     count, on the last tic that carries input, and on a SHA-256 of the decoded
     command bytes. Two independent readers agreeing is what makes "the stream
     was read correctly" a check rather than a claim.
  2. THE WHOLE STREAM IS CONSUMED. `RUN tics` must equal the lump's own count and
     `stop` must be the marker: a playback that stops early, or that keeps
     reading past the marker, fails here.
  3. THE SIMULATION IS PINNED. The final state hash, the per-100-tic hash chain
     and every coherence number below are compared against the values in PINS.
     When they move, the simulation changed. Re-baselining them is a deliberate
     act that belongs in a commit message, not an accident of a refactor.
  4. THE RUN IS COHERENT. These are assertions, not pins, because a run that
     breaks one is wrong in a way a re-baseline cannot excuse: the player's
     centre never crosses a one-sided linedef, never leaves the map, its recorded
     turn is applied exactly, forward commands move it forward, the demo opens
     doors and triggers USE lines, it fires and spends ammo, and its health stays
     inside its own bounds.

WHAT THE STRICT VERDICT CHECKS

`--strict` turns every fidelity gap into a failure. There are four:

  1. an action the run reached that p_action.c does not implement (`ANOM` and the
     `MISSING` set);
  2. a line special the run reached that upstream's trigger function acts on and
     this port has no EV for (`SPECUNSUP`). A number upstream does *not* name for
     that trigger kind is not a gap: P_CrossSpecialLine falls out of its switch
     and P_UseSpecialLine returns true without acting, so doing nothing is the
     faithful answer. Those events are reported as `IGNORED` evidence instead,
     which is what DEMO2's 27 and DEMO3's 1 turned out to be;
  3. the run's *ending* not being one upstream can produce. Upstream ends a demo
     at the marker byte or at `G_CheckDemoStatus`, never on a death, so a
     recording whose last command kills the player simply ends dead and no
     respawn was possible. The conditions are therefore:
       (a) every death the stream still had commands for was answered by a
           reload inside the stream -- `DEATH` tics and `RELOAD` tics must pair
           up, and a death with input still to come and no later reload is the
           frozen corpse the old test was reaching for;
       (b) a run may end dead only when its death landed at or after the last
           input, and the message names that tic when this is how a run ends;
       (c) `alive` stays inside the pin, so a change that flips a run from dead
           to alive or back fails on the pin rather than on a rule upstream does
           not have.
     `deaths - reloads` is kept as the arithmetic behind (a) and (b): it is 0 for
     a run that ends alive and 1 for one whose last death had no tic left to
     answer it.
  4. a press whose 64-unit `P_UseLines` ray reached a special line that did not
     trigger, or a press that triggered where the independent walk finds no
     special line in reach. The walk below is re-derived from linuxdoom-1.10's
     `P_PathTraverse` + `PTR_UseTraverse`: the USERANGE ray, the blockmap-origin
     nudge, ascending intercepts, `P_LineOpening` stopping a closed non-special
     line, and the first special line ending the walk.

The earlier door conditions -- "some USE press triggered a line" and "the player
passed a moved door" -- were not derived from upstream and are false for a
faithful DEMO1, whose stream never meets a special line at all. They are replaced
by (4), which says what is falsifiable: a press that had a line in reach used it,
and a press with nothing in reach did not.

The walk uses the map's own initial sector heights and special numbers, read from
the WAD by `doom_ref.read_bsp_map`. All 58 presses of the three shareware demos
agree with the C under that model; a future demo whose press depends on a door
that has already moved would show as a disagreement here rather than be hidden.

WHAT A PIN DOES AND DOES NOT PROVE -- READ THIS BEFORE RE-BASELINING

There is no 1993 DOOM on this machine to diff against, and no oracle for a whole
level's simulation, so nothing here proves the run is what upstream would have
produced. The hashes are a REGRESSION PIN: the numbers this port produced once.
They catch any later change; they do not certify the past. The commit that
recorded them says so in its body, and a later commit that moves them must say
what moved and why.

What the data itself can falsify is the ALIVE check, and the reference matters.
In linuxdoom-1.10 a single-player death runs P_DeathThink to PST_REBORN, and
G_DoReborn answers it with `gameaction = ga_loadlevel` -- the same level,
reloaded -- after which playback continues; G_CheckDemoStatus is reached at the
marker, never on a death. Each demo's own lump agrees: all three die and then
carry input for thousands of tics, with a USE hold just after the death. So the
falsifiable statement is not "the player never dies" and not "the player is alive
at the end" -- a recording whose last command kills it simply ends dead -- but
"the corpse is never what the stream is played on": every death the stream still
had commands for was answered by a reload, and a run that ends dead may only do
so when its death landed at or after the last input. `ALIVE` reports the end
state, `DEATH` names every fall, `VERDICT` folds the ending rule together with the
missing-action set, and `--strict` turns `faithful=0` into a failure.

`RELOAD` and `ARENA` are this gate's evidence that the reload is the level reload
and not a teleport or a leak: every RELOAD line reports where the player
reappeared and how many bytes the level's arena holds, and the two checks below
compare them against the WAD's own THINGS record and against the first load.

THE COHERENCE NUMBERS

    wallcross   steps whose player centre crossed a one-sided linedef (>0 fails)
    outside     tics whose player centre had no subsector (>0 fails)
    anglebad    recorded angleturn not applied exactly (>0 fails)
    angleskip   tics where P_MovePlayer did not run: dead, or a reaction time
    sectorchanges  tics whose player sector differed from the previous tic
    sectors     distinct sectors the player stood in
    doorpass    sector changes into a sector whose ceiling a mover had raised
    doorsopen   sectors whose ceiling differs from its map value at the end
    forward     tics carrying a non-zero forwardmove
    netfwd/netback  displacement projected on the facing, in 16.16 map units
    shots       tics that spent ammo; ammo is the total spent
    attackticks tics carrying BT_ATTACK
    useticks/useedges/usetriggers   BT_USE tics, press edges, lines triggered
    hpmin/hpmax/hploss/hpgain       the health envelope and its changes
    deadtics    tics the player spent not PST_LIVE
    mobjmax     the peak live mobj count

THE RELOAD EVIDENCE

    ARENA       the level's z_stats().inUse after the load; `peak` must equal it,
                because the reload re-lays one free block over the same arena
    RELOAD      one line per answered death: the tic whose P_DeathThink set
                PST_REBORN, the avatar's position after the reload, its health,
                the live mobj count, leveltime (0) and the arena's bytes in use
    SPEC        the line specials the playback reached, as run totals

Every RELOAD's `arena` must equal ARENA's `load` (a reload that allocated a level
per death fails here), and its `x`/`y` must equal the map's own player-1 THING --
which this script reads from the WAD itself, so "the player respawned at the
map's start" is the map's record against the simulation's state.

PROVING THE GATE CAN FAIL

    --perturb skip         the C side drops the first command, so the stream runs
                           one tic out of step with the world
    --perturb viewheight   the C side is built with P_VIEWHEIGHT one map unit
                           short

Both must move the pin; the gate fails if they do not, because a pin that a
perturbation cannot move is not measuring anything. The pin is the pair (final
state, chain): the chain is the stronger aggregate, since it hashes every
CHECK_TICS snapshot plus the final state, and the final alone can be insensitive
once the player dies and `P_DeathThink` clamps the corpse's view.

USAGE

    python tools/demo_check.py --wad path/to/DOOM1.WAD [--demo DEMO1]
                              [--perturb none|skip|viewheight] [--strict]

The WAD is copyrighted shareware data and is never committed; point this tool at
a file outside the repository. The harness is compiled into a temporary directory
that is removed before the script returns.

EXIT CODES

    0  the pins matched and every coherence assertion held (and, with a
       perturbation, the hash moved)
    1  a pin or an assertion failed, or a perturbation did not move the hash
    2  the command line, the compiler or the input was unusable

Standard library only, plus this repository's `doom_ref.py` for the independently
parsed map the P_UseLines walk needs.
"""

from __future__ import annotations

import argparse
import hashlib
import shutil
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

# The independent map loader and fine tables: doom_ref.py is this project's
# from-linuxdoom-1.10 Python oracle, and the P_UseLines re-derivation below uses
# its geometry rather than a third parser. Everything it imports is stdlib.
sys.path.insert(0, str(Path(__file__).resolve().parent))
import doom_ref  # noqa: E402  (path-dependent by design)

FRACUNIT = 1 << 16
MAXHEALTH = 100
EXIT_OK = 0
EXIT_MISMATCH = 1
EXIT_USAGE = 2

SOURCES = (
    "g_game.c", "p_setup.c", "p_mobj.c", "p_map.c", "p_user.c", "p_tick.c",
    "p_action.c", "p_inter.c", "p_pspr.c", "p_sight.c", "p_enemy.c",
    "m_random.c", "z_zone.c", "doom_map.c", "r_bsp.c", "r_plane.c",
    "r_tables.c", "w_wad.c", "p_spec.c", "p_floor.c", "p_doors.c", "p_plats.c",
    "p_ceilng.c", "p_lights.c",
)

# --- The pins ---------------------------------------------------------------
#
# One entry per shareware demo. Each was recorded from the run this port produces
# today, with the command `python tools/demo_check.py --wad <wad> --demo <name>`
# and nothing else. The fields a reader should look at first are `tics` (the
# lump's own count, which must also equal the independent decode below), `final`
# and `cohere` (the whole coherence line, verbatim, so any number that moves names
# itself). `anom`, `missing`, `spec` and `alive` are the fidelity record:
# `missing` is the distinct action set (empty since D4.4's A_SPosAttack,
# A_SargAttack and the A_Scream sound draw landed), `spec` is the line-special
# set the playback reached, and `alive` is the end state -- PST_LIVE, or dead when
# the last command killed the player -- with the last input tic and the number of
# reloads that answered its deaths. `arena` and `reload` are the reload's
# own evidence: the level's bytes in use, and the tic of every answered death.
#
# All three demos die. Before D4.4's reload step this gate recorded the frozen
# corpse; every number below is from a run that answered each death with
# G_DoReborn's `ga_loadlevel` -- the same level, reloaded -- and kept playing,
# with one exception: DEMO1's seventh death lands on its own last command, so
# that run ends dead and its `alive` pin says so. The pins are still a regression
# target, not a fidelity proof.
#
# A demo whose pins are absent is a demo this gate has not measured yet: it
# reports the numbers and fails, which is how a new demo is added.
PINS: dict[str, dict[str, object]] = {
    "DEMO1": {
        "map": "E1M5",
        # Re-baselined once, for D4.5's sector damage: P_PlayerInSpecialSector
        # made E1M5's nukage real (sector 59, 26 damage windows, 130 points), so
        # the health track and every death after tic 1149 moved, and the seventh
        # death now lands on the stream's own last command (tic 5025). The run
        # ends dead, which is upstream's ending for this recording -- see the
        # ending rules in check_run().
        "tics": 5026,
        "final": "92ff2f08e0bc3616cd24d1b52afbee0e89ffc00ace14feb317ead37f5c981068",
        "chain": "a3371434b6d01d6120f0fb8c5fe1a421849a759cb0ed34593202510526d663d4",
        "cohere": (
            "COHERE wallcross=0 outside=0 angleskip=1156 anglebad=0 anglepunch=0 "
            "sectorchanges=37 sectors=4 doorpass=0 doorsopen=0 forward=4340 "
            "netfwd=635719608 netback=-130104468 shots=119 ammo=119 "
            "attackticks=1463 useticks=49 useedges=32 usetriggers=0 hpmin=0 "
            "hpmax=100 hploss=720 hpgain=20 deadtics=1163 mobjmax=224"
        ),
        "spec": "SPEC triggers=0 unsupported=0 deferred=0 scrolls=0",
        "anom": "ANOM missing=0 firsttic=0 first=0 last=0",
        "missing": "MISSING count=0 overflow=0",
        "specunsup": "SPECUNSUP count=0 overflow=0",
        "ignored": "IGNORED count=1 1@3521:476:shootx2",
        "alive": "ALIVE alive=0 lastinput=5001 deathtic=1149 reborns=6",
        "arena": 43616,
        "reload": [1424, 2031, 2652, 3164, 4019, 4363],
    },
    "DEMO2": {
        "map": "E1M3",
        "tics": 3836,
        "final": "88b719b0d04dbf0229956fce96aa5613b31ff4f1e5b64701d04607854bfee4f3",
        "chain": "3c78712732ad61c3bf6e5fa8320c7e432d383eea60c51ec9943db143241326f5",
        "cohere": (
            "COHERE wallcross=0 outside=0 angleskip=108 anglebad=0 anglepunch=0 "
            "sectorchanges=30 sectors=7 doorpass=2 doorsopen=0 forward=3485 "
            "netfwd=447647551 netback=-59094240 shots=53 ammo=53 attackticks=461 "
            "useticks=62 useedges=23 usetriggers=2 hpmin=0 hpmax=100 hploss=100 "
            "hpgain=0 deadtics=109 mobjmax=318"
        ),
        "spec": "SPEC triggers=2 unsupported=0 deferred=0 scrolls=0",
        "anom": "ANOM missing=0 firsttic=0 first=0 last=0",
        "missing": "MISSING count=0 overflow=0",
        "specunsup": "SPECUNSUP count=0 overflow=0",
        "ignored": ("IGNORED count=5 31@80:996:shootx8 31@91:996:crossx1 "
                    "2@160:988:shootx1 1@188:329:shootx15 1@391:329:crossx2"),
        "alive": "ALIVE alive=1 lastinput=3807 deathtic=551 reborns=1",
        "arena": 60048,
        "reload": [659],
    },
    "DEMO3": {
        "map": "E1M7",
        "tics": 2134,
        "final": "f1f9ae17b59eac07bc325bbedaef00c9b8b9e368292ab33e468a1f6a5727fcdc",
        "chain": "2dd5791136f6f53e973f4397a39bc751b53f03e16a6e130d220962b230d297c8",
        "cohere": (
            "COHERE wallcross=0 outside=0 angleskip=111 anglebad=0 anglepunch=0 "
            "sectorchanges=5 sectors=6 doorpass=1 doorsopen=0 forward=1776 "
            "netfwd=167641847 netback=-33826979 shots=25 ammo=25 attackticks=273 "
            "useticks=27 useedges=10 usetriggers=1 hpmin=0 hpmax=100 hploss=100 "
            "hpgain=0 deadtics=112 mobjmax=274"
        ),
        "spec": "SPEC triggers=1 unsupported=0 deferred=0 scrolls=0",
        "anom": "ANOM missing=0 firsttic=0 first=0 last=0",
        "missing": "MISSING count=0 overflow=0",
        "specunsup": "SPECUNSUP count=0 overflow=0",
        "ignored": "IGNORED count=1 31@110:949:crossx1",
        "alive": "ALIVE alive=1 lastinput=2104 deathtic=500 reborns=1",
        # Re-measured once, for T35: E1M7 is one of the two episode-1 maps that
        # carry line special 48, and p_spawn_specials() now allocates one
        # p_scroll_t per such line from the level's arena. Four lines x 12 B =
        # 48 B: 53,304 -> 53,352. Nothing else about this run moved -- `final`,
        # `chain` and `cohere` are the same values as before the change.
        "arena": 53352,
        "reload": [611],
    },
}

PERTURBATIONS: dict[str, tuple[list[str], list[str]]] = {
    "none": ([], []),
    # A one-tic offset in the command stream: every command lands on the wrong
    # world state, so the final hash must change.
    "skip": ([], ["--perturb", "skip"]),
    # P_VIEWHEIGHT (p_local.h's 41*FRACUNIT) one map unit short: the view state
    # in the hash must change, and nothing else in the stream should.
    "viewheight": (["-DP_VIEWHEIGHT=(40*65536)"], []),
}

DEMO_MARKER = 0x80
DEMO_VERSION = 109
DEMO_HEADER_SIZE = 13
DEMO_TICCMD_SIZE = 4
MAXPLAYERS = 4


def project_root() -> Path:
    """games/doom, the directory that holds lib/ and tools/."""
    return Path(__file__).resolve().parent.parent


# --- An independent reader (the C side has its own) -------------------------


class DemoHeader:
    """The version-109 header, parsed from the WAD, not from the C harness."""

    def __init__(self, data: bytes) -> None:
        if len(data) < DEMO_HEADER_SIZE + 1:
            raise ValueError("the lump is shorter than a header and a marker")
        if data[0] != DEMO_VERSION:
            raise ValueError(f"version {data[0]} is not {DEMO_VERSION}")
        (self.version, self.skill, self.episode, self.map, self.deathmatch,
         self.respawn, self.fast, self.nomonsters, self.consoleplayer) = data[:9]
        self.players = list(data[9:9 + MAXPLAYERS])


def read_wad_lump(wad: Path, name: str) -> bytes:
    """The lump's bytes, from the WAD directory. A name repeated in the
    directory answers its first occurrence, as W_CheckNumForName does."""
    data = wad.read_bytes()
    if data[:4] not in (b"IWAD", b"PWAD"):
        raise ValueError(f"{wad} is not a WAD")
    count, offset = struct.unpack_from("<II", data, 4)
    for index in range(count):
        entry = offset + index * 16
        position, size, raw_name = struct.unpack_from("<II8s", data, entry)
        if raw_name.split(b"\0")[0].decode("ascii", "replace") == name:
            return data[position:position + size]
    raise ValueError(f"{wad} carries no {name} lump")


def read_wad_map_things(wad: Path, map_name: str) -> bytes:
    """A map's THINGS lump: the entry after the `<map_name>` marker.

    A map in the 1993 directory is a zero-length marker lump followed by its
    eleven lumps in DoomEd order, THINGS first, which is the order this script's
    own directory walk relies on rather than any lump name (a map's THINGS is a
    zero-length lump in some editors' output, so the marker is the only anchor).
    """
    data = wad.read_bytes()
    if data[:4] not in (b"IWAD", b"PWAD"):
        raise ValueError(f"{wad} is not a WAD")
    count, offset = struct.unpack_from("<II", data, 4)
    entries: list[tuple[int, int, str]] = []
    for index in range(count):
        position, size, raw_name = struct.unpack_from("<II8s", data, offset + index * 16)
        entries.append((position, size, raw_name.split(b"\0")[0].decode("ascii", "replace")))
    for index, (_position, _size, name) in enumerate(entries):
        if name == map_name and index + 1 < len(entries):
            position, size, _next = entries[index + 1]
            return data[position:position + size]
    raise ValueError(f"{wad} carries no {map_name} map")


def player_start(things: bytes) -> tuple[int, int]:
    """The map's player-1 start, as P_SpawnMapThing reads it: the first type-1
    THING record, five little-endian shorts per record."""
    starts = []
    for index in range(len(things) // 10):
        x, y, _angle, kind, _options = struct.unpack_from("<hhhhh", things, index * 10)
        if kind == 1:
            starts.append((x, y))
    if len(starts) != 1:
        raise ValueError(f"the map carries {len(starts)} player-1 starts, expected 1")
    return starts[0]


# --- P_UseLines, re-derived from linuxdoom-1.10 ------------------------------
#
# p_user.c computes a 64-map-unit ray along the player's facing and hands it to
# P_PathTraverse(PT_ADDLINES, PTR_UseTraverse). Neither the ray nor the walk is
# taken from the C side: this is the oracle's own reading of the same source.

USERANGE = 64 * FRACUNIT
MAPBLOCKSIZE = 128 * FRACUNIT


def blockmap_origin(lumps: list, map_name: str) -> tuple[int, int]:
    """P_PathTraverse's `bmaporgx/bmaporgy`: the BLOCKMAP header's origin.

    It is 16.16 like every other coordinate; the header stores map units.
    """
    marker = doom_ref.first_index(lumps, map_name)
    if marker < 0 or marker + 10 >= len(lumps):
        raise ValueError(f"{map_name}: no BLOCKMAP after the marker")
    data = lumps[marker + 1 + 9].data
    if len(data) < 4:
        raise ValueError(f"{map_name}: the BLOCKMAP header is short")
    x, y = struct.unpack_from("<hh", data, 0)
    return x * FRACUNIT, y * FRACUNIT


def use_lines_reach(level: "doom_ref.SpecLevel", origin: tuple[int, int],
                    x1: int, y1: int, angle: int) -> int | None:
    """The first special line P_UseLines' ray reaches, or None.

    Upstream's rules, in its order:

      * `x2 = x1 + (USERANGE>>FRACBITS)*finecosine[angle]` -- a map-unit product,
        not a FixedMul;
      * P_PathTraverse nudges its source by one unit on each axis that is not on a
        blockmap boundary ("don't side exactly on a line"), and the direction is
        the nudged source to the un-nudged end;
      * intercepts ascending; a tied fraction is one visit, as upstream's
        `frac <= last` skip makes it;
      * PTR_UseTraverse: a non-special line runs P_LineOpening, and
        `openrange <= 0` stops the walk ("can't use through a wall"); a special
        line is dispatched and ends the walk.

    The level is built from the WAD's initial sector heights and special
    numbers, which is the state all 58 presses of the three demos agree on.
    """
    fine = (angle & 0xFFFFFFFF) >> doom_ref.ANGLETOFINESHIFT
    x2 = doom_ref.i32(x1 + 64 * doom_ref.FINE_SINE[fine + doom_ref.FINEANGLES // 4])
    y2 = doom_ref.i32(y1 + 64 * doom_ref.FINE_SINE[fine])
    if (x1 - origin[0]) & (MAPBLOCKSIZE - 1):
        x1 = doom_ref.i32(x1 + FRACUNIT)
    if (y1 - origin[1]) & (MAPBLOCKSIZE - 1):
        y1 = doom_ref.i32(y1 + FRACUNIT)
    dx = doom_ref.i32(x2 - x1)
    dy = doom_ref.i32(y2 - y1)

    last = -1
    while True:
        best = FRACUNIT + 1
        best_line = None
        for index in range(len(level.bsp.linedefs)):
            frac = doom_ref.movement_trace_line(level, x1, y1, dx, dy, index)
            if frac is None or frac <= last:
                continue
            if frac < best:
                best = frac
                best_line = index
        if best_line is None:
            return None
        last = best
        if level.line_special[best_line] == 0:
            if doom_ref.movement_line_opening(level, best_line)["range"] <= 0:
                return None
            continue
        return best_line


def decode_stream(data: bytes) -> tuple[list[tuple[int, int, int, int]], int]:
    """The commands, with upstream's rule: four bytes until the 0x80 first byte.

    The third byte is the *high* byte of angleturn (G_WriteDemoTiccmd writes
    `cmd->angleturn>>8`), so the decoded value is that byte shifted left 16 bits
    as a signed short.
    """
    commands: list[tuple[int, int, int, int]] = []
    cursor = DEMO_HEADER_SIZE
    while cursor < len(data):
        if data[cursor] == DEMO_MARKER:
            return commands, cursor
        if cursor + DEMO_TICCMD_SIZE > len(data):
            raise ValueError(f"a command starts at {cursor} and crosses the end")
        forward = struct.unpack_from("<b", data, cursor)[0]
        side = struct.unpack_from("<b", data, cursor + 1)[0]
        # The third byte is angleturn's *high* byte; the low byte is not stored.
        angle = data[cursor + 2] << 8
        if angle >= 0x8000:
            angle -= 0x10000
        buttons = data[cursor + 3]
        commands.append((forward, side, angle, buttons))
        cursor += DEMO_TICCMD_SIZE
    raise ValueError("the stream has no 0x80 terminator")


def command_digest(commands: list[tuple[int, int, int, int]]) -> str:
    """SHA-256 over the four bytes of every command, in the lump's own encoding.

    This mirrors tools/demo_check.c's digestCommand(): forwardmove and sidemove
    as bytes, angleturn's high byte, buttons. It is the raw stream segment, which
    is what makes the comparison between two readers meaningful.
    """
    digest = hashlib.sha256()
    for forward, side, angle, buttons in commands:
        digest.update(bytes((forward & 0xFF, side & 0xFF,
                             (angle >> 8) & 0xFF, buttons & 0xFF)))
    return digest.hexdigest()


# --- Running the C side -----------------------------------------------------


def compile_harness(workdir: Path, compiler: str, perturbation: str) -> Path:
    root = project_root()
    defines, _flags = PERTURBATIONS[perturbation]
    executable = workdir / ("demo_check.exe" if sys.platform == "win32"
                            else "demo_check")
    command = [
        compiler, "-std=gnu99", "-O2", "-Wall", "-Wextra", *defines,
        "-I", str(root / "lib" / "doom"),
        "-o", str(executable),
        str(root / "tools" / "demo_check.c"),
    ]
    command.extend(str(root / "lib" / "doom" / name) for name in SOURCES)
    result = subprocess.run(command, capture_output=True, text=True)
    if result.returncode != 0:
        raise RuntimeError("the harness did not compile:\n"
                           + (result.stderr or result.stdout))
    if result.stderr.strip():
        raise RuntimeError("the harness compiled with warnings, which this project "
                           "does not accept:\n" + result.stderr)
    return executable


def fields_of(line: str) -> tuple[str, dict[str, str]]:
    parts = line.split()
    kind = parts[0] if parts else ""
    values: dict[str, str] = {}
    for part in parts[1:]:
        if "=" in part:
            key, value = part.split("=", 1)
            values[key] = value
    return kind, values


def record_of(text: str, kind: str) -> dict[str, str]:
    for line in text.splitlines():
        name, values = fields_of(line)
        if name == kind:
            return values
    return {}


def line_of(text: str, kind: str) -> str:
    for line in text.splitlines():
        if fields_of(line)[0] == kind:
            return line
    return ""


def lines_of(text: str, kind: str) -> list[str]:
    """Every line of a kind, in output order. RELOAD is one line per answered
    death, so `line_of` would only ever report the first."""
    return [line for line in text.splitlines() if fields_of(line)[0] == kind]


def number(record: dict[str, str], key: str, default: int = 0) -> int:
    value = record.get(key)
    if value is None:
        return default
    try:
        return int(value)
    except ValueError:
        return default


# --- The verdict ------------------------------------------------------------


def check_run(text: str, name: str, commands: list[tuple[int, int, int, int]],
              header: DemoHeader, start: tuple[int, int],
              strict: bool,
              use_walk) -> list[str]:
    """Every failure, each named independently, so one run reports them all."""
    failures: list[str] = []
    pins = PINS.get(name)
    demo = record_of(text, "DEMO")
    world = record_of(text, "WORLD")
    cmd = record_of(text, "CMD")
    run = record_of(text, "RUN")
    state = record_of(text, "STATE")
    cohere = record_of(text, "COHERE")
    arena = record_of(text, "ARENA")
    reloads = [record_of(line, "RELOAD") for line in lines_of(text, "RELOAD")]
    deaths = [record_of(line, "DEATH") for line in lines_of(text, "DEATH")]
    anom = record_of(text, "ANOM")
    alive = record_of(text, "ALIVE")
    verdict = record_of(text, "VERDICT")
    specunsup = record_of(text, "SPECUNSUP")
    presses = [record_of(line, "PRESS") for line in lines_of(text, "PRESS")]

    if not demo or not run:
        return [f"{name}: the harness produced no DEMO/RUN line"]

    # 1. The two readers agree on the stream.
    expected_map = f"E{header.episode}M{header.map}"
    if demo.get("map") != str(header.map) or demo.get("episode") != str(header.episode):
        failures.append(f"{name}: header episode/map {demo.get('episode')}/"
                        f"{demo.get('map')} != the WAD's {header.episode}/{header.map}")
    if demo.get("skill") != str(header.skill):
        failures.append(f"{name}: header skill {demo.get('skill')} != {header.skill}")
    expected_players = ",".join(str(slot) for slot in header.players)
    if demo.get("players") != expected_players:
        failures.append(f"{name}: playeringame {demo.get('players')!r} != "
                        f"{expected_players!r}")
    if demo.get("tics") != str(len(commands)):
        failures.append(f"{name}: the C reader counted {demo.get('tics')} tics, "
                        f"this reader {len(commands)}")
    last_input = max((index for index, command in enumerate(commands)
                      if any(command)), default=-1)
    if cmd.get("lastinput") != str(last_input):
        failures.append(f"{name}: last input tic {cmd.get('lastinput')} != "
                        f"{last_input}")
    if cmd.get("digest") != command_digest(commands):
        failures.append(f"{name}: the command digests differ "
                        f"(C {cmd.get('digest')}, here {command_digest(commands)})")

    # 2. The whole stream was consumed and nothing past it.
    if run.get("stop") != "marker":
        failures.append(f"{name}: playback stopped on {run.get('stop')!r}, not the "
                        "0x80 marker")
    if run.get("tics") != str(len(commands)):
        failures.append(f"{name}: playback consumed {run.get('tics')} commands of "
                        f"{len(commands)}")
    if run.get("declared") != str(len(commands)):
        failures.append(f"{name}: the run says the lump declared "
                        f"{run.get('declared')} commands, not {len(commands)}")

    # 3. The coherence assertions. These are wrong-run detectors, not pins.
    if number(cohere, "wallcross") != 0:
        failures.append(f"{name}: the player crossed a one-sided linedef on "
                        f"{cohere.get('wallcross')} tics")
    if number(cohere, "outside") != 0:
        failures.append(f"{name}: the player was outside every sector on "
                        f"{cohere.get('outside')} tics")
    if number(cohere, "anglebad") != 0:
        failures.append(f"{name}: the recorded turn was not applied exactly on "
                        f"{cohere.get('anglebad')} tics")
    if number(cohere, "forward") == 0:
        failures.append(f"{name}: no command carried a forwardmove")
    if number(cohere, "netfwd") <= 0:
        failures.append(f"{name}: the forward commands did not move the player "
                        f"forward (netfwd {cohere.get('netfwd')})")
    if number(cohere, "sectors") <= 1:
        failures.append(f"{name}: the player never left its starting sector")
    if number(cohere, "attackticks") == 0:
        failures.append(f"{name}: the demo never pressed fire")
    if number(cohere, "shots") == 0:
        failures.append(f"{name}: fire was pressed but no ammo was spent")
    if number(cohere, "hpmax") > 100:
        failures.append(f"{name}: the player's health reached "
                        f"{cohere.get('hpmax')}, above MAXHEALTH")
    if number(cohere, "hpmin") < 0:
        failures.append(f"{name}: the player's health went negative")

    # 3b. The reload. These are assertions, not pins: a reload that teleported the
    # player, restarted the clock, left the world dead, or leaked an arena would
    # be wrong in a way a re-baseline cannot excuse. The comparison is against
    # the WAD's own player-1 THING and against the first load's arena, both read
    # here rather than taken from the C side's report of itself.
    for record in reloads:
        where = f"the reload at tic {record.get('n')}"
        # The THING record is in map units; the harness reports 16.16 fixed point.
        if (number(record, "x"), number(record, "y")) != (start[0] * FRACUNIT,
                                                         start[1] * FRACUNIT):
            failures.append(f"{name}: {where} put the player at "
                            f"({record.get('x')}, {record.get('y')}), not at the "
                            f"map's start {start}")
        if number(record, "leveltime") != 0:
            failures.append(f"{name}: {where} left leveltime at "
                            f"{record.get('leveltime')}, not P_SetupLevel's 0")
        if number(record, "health") != MAXHEALTH:
            failures.append(f"{name}: {where} did not respawn the player at "
                            f"{MAXHEALTH} health ({record.get('health')})")
        if number(record, "arena") != number(arena, "load"):
            failures.append(f"{name}: {where} left the arena at "
                            f"{record.get('arena')} B, not the load's "
                            f"{arena.get('load')} B: the reload is not reusing it")
    if number(arena, "peak") > number(arena, "load"):
        failures.append(f"{name}: the level's arena peaked at {arena.get('peak')} B "
                        f"over a {arena.get('load')} B load, so a reload added a block")
    if number(alive, "reborns") != len(reloads):
        failures.append(f"{name}: {alive.get('reborns')} reloads reported but "
                        f"{len(reloads)} RELOAD lines printed")
    if number(alive, "deathtic") >= 0 and number(alive, "reborns") == 0:
        failures.append(f"{name}: the player died at tic {alive.get('deathtic')} and "
                        "no reload answered it")

    # 4. The pins.
    if pins is None:
        failures.append(f"{name}: this gate has no pin for it yet; record the "
                        "numbers above and add one")
        return failures
    if world.get("map") != pins["map"]:
        failures.append(f"{name}: the level opened is {world.get('map')!r}, not "
                        f"{pins['map']!r}")
    if demo.get("tics") != str(pins["tics"]):
        failures.append(f"{name}: the lump's tic count is {demo.get('tics')}, "
                        f"pinned {pins['tics']} (the WAD changed, or the reader did)")
    for key, kind in (("final", "FINAL"), ("chain", "CHAIN")):
        pinned = pins[key]
        actual = record_of(text, kind).get("hash")
        if actual != pinned:
            failures.append(f"{name}: {kind.lower()} hash {actual} != pinned {pinned}")
    if line_of(text, "COHERE") != pins["cohere"]:
        failures.append(f"{name}: the coherence line moved.\n"
                        f"    now:  {line_of(text, 'COHERE')}\n"
                        f"    pin:  {pins['cohere']}")
    if line_of(text, "ANOM") != pins["anom"]:
        failures.append(f"{name}: the missing-action line moved.\n"
                        f"    now:  {line_of(text, 'ANOM')}\n"
                        f"    pin:  {pins['anom']}")
    if "missing" in pins and line_of(text, "MISSING") != pins["missing"]:
        failures.append(f"{name}: the distinct missing-action set moved.\n"
                        f"    now:  {line_of(text, 'MISSING')}\n"
                        f"    pin:  {pins['missing']}")
    if "spec" in pins and line_of(text, "SPEC") != pins["spec"]:
        failures.append(f"{name}: the line-special set moved.\n"
                        f"    now:  {line_of(text, 'SPEC')}\n"
                        f"    pin:  {pins['spec']}")
    if "specunsup" in pins and line_of(text, "SPECUNSUP") != pins["specunsup"]:
        failures.append(f"{name}: the unsupported-special set moved.\n"
                        f"    now:  {line_of(text, 'SPECUNSUP')}\n"
                        f"    pin:  {pins['specunsup']}")
    if "ignored" in pins and line_of(text, "IGNORED") != pins["ignored"]:
        failures.append(f"{name}: the upstream-ignored set moved.\n"
                        f"    now:  {line_of(text, 'IGNORED')}\n"
                        f"    pin:  {pins['ignored']}")
    if "arena" in pins and number(arena, "load") != pins["arena"]:
        failures.append(f"{name}: the level's arena load is {arena.get('load')} B, "
                        f"pinned {pins['arena']} B")
    if "reload" in pins:
        tics = [number(record, "n") for record in reloads]
        if tics != pins["reload"]:
            failures.append(f"{name}: the reload tics are {tics}, pinned "
                            f"{pins['reload']}")
    if line_of(text, "ALIVE") != pins["alive"]:
        failures.append(f"{name}: the alive line moved.\n"
                        f"    now:  {line_of(text, 'ALIVE')}\n"
                        f"    pin:  {pins['alive']}")

    # 4a/5. The ending. Upstream ends a demo at the marker or at
    # G_CheckDemoStatus, never on a death, so "the player is alive at the last
    # command" is not an upstream requirement: a recording whose last command
    # kills the player ends dead, and no respawn was possible. The conditions
    # are (a) every death the stream still had commands for was answered by a
    # reload inside it, (b) a run may end dead only when its death landed at or
    # after the last input, and (c) `alive` is pinned, so a change that flips
    # dead<->alive fails on the pin instead of on invented physics.
    #
    # The arithmetic behind (a) and (b): the harness prints one DEATH per fall
    # and one RELOAD per PST_REBORN, so a run that ends alive must have as many
    # reloads as deaths, and one that ends dead exactly one fewer.
    alive_at_end = number(alive, "alive")
    last_input = number(alive, "lastinput")
    death_tics = [number(record, "n") for record in deaths]
    reload_tics = [number(record, "n") for record in reloads]
    unanswered = len(deaths) - len(reloads)
    if unanswered != (0 if alive_at_end else 1):
        failures.append(f"{name}: {len(deaths)} death(s) and {len(reloads)} "
                        f"reload(s) with alive={alive_at_end}: {unanswered} "
                        "unanswered")

    for tic in death_tics:
        if tic >= last_input:
            # A death at or after the last input had no command left to answer
            # it; (b) judges that one.
            continue
        if not any(reload_tic > tic for reload_tic in reload_tics):
            failures.append(
                f"{name}: the death at tic {tic} has no later reload, and the "
                f"stream still had commands after it (last input tic "
                f"{last_input}): the corpse was played on")
    if alive_at_end == 0:
        final_death = max(death_tics) if death_tics else -1
        if final_death < last_input:
            failures.append(
                f"{name}: the run ends dead, but its last death is at tic "
                f"{final_death}, before the last input tic {last_input}, so a "
                "respawn was still possible")
        else:
            print(f"  ENDS-DEAD death N={final_death} at or after the last input "
                  f"N={last_input}: no respawn was possible")

    # The harness's own summary, for everything the ending rules do not cover:
    # an unimplemented action (`action`) or a line special with no EV handler
    # (`special`). `endsdead` is a permitted ending; `frozen` is (a) failing.
    reason = verdict.get("reason")
    if verdict.get("faithful") != "1" and reason not in ("endsdead",):
        message = (f"{name}: the run is not faithful: reason={reason} "
                   f"missing={anom.get('missing')} (first {anom.get('first')} at tic "
                   f"{anom.get('firsttic')}), player alive at the end={alive.get('alive')}")
        if strict:
            failures.append(message)
        else:
            print(f"  UNFAITHFUL {message}")

    # 5a. The line-special gap, named. A run that reaches a special upstream acts
    # on and this port cannot execute is not upstream's world after the first
    # one; the set (<special>@<tic>:<line>:<kind>x<hits>) is the evidence.
    if strict and number(specunsup, "count") > 0:
        tokens = [part for line in lines_of(text, "SPECUNSUP")
                  for part in line.split()[1:] if "@" in part]
        failures.append(f"{name}: the run reached {specunsup.get('count')} distinct "
                        "special(s) upstream handles and this port does not: "
                        + " ".join(tokens))

    # 5b. P_UseLines, re-derived. Each PRESS line is a live player's press with
    # the position and facing the tic ended on -- P_UseLines runs after
    # P_MovePlayer, and nothing later in P_PlayerThink moves the avatar. The walk
    # below decides whether upstream's 64-unit ray would have reached a special
    # line. A press that had one in reach must have triggered it, and a press that
    # triggered must have had one in reach. This replaces the old "some press
    # triggered" / "the player passed a door" pair, neither of which upstream
    # requires and both of which a faithful DEMO1 must fail.
    if strict:
        for press in presses:
            tic = number(press, "n")
            line = use_walk(number(press, "x"), number(press, "y"),
                            number(press, "angle"))
            reached = 1 if line is not None else 0
            triggered = number(press, "reached")
            if reached != triggered:
                if reached:
                    failures.append(
                        f"{name}: the press at tic {tic} had special line {line} "
                        "within USERANGE and triggered nothing")
                else:
                    failures.append(
                        f"{name}: the press at tic {tic} triggered a line but the "
                        "independent P_UseLines walk found no special line in reach")
    return failures


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        prog="demo_check.py",
        description="Play one shareware demo through lib/doom, compare the stream "
                    "against an independent decode, and pin the final state hash, "
                    "the per-100-tic chain and the coherence numbers.",
        epilog="The harness is built in a temporary directory and nothing is "
               "written into the worktree. A pin is a regression pin: it proves "
               "the simulation did not change, not that it was ever right.\n",
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    parser.add_argument("--wad", type=Path, required=True, metavar="PATH",
                        help="the IWAD to read; keep it outside the repository")
    parser.add_argument("--demo", default="DEMO1", metavar="NAME",
                        help="the demo lump to play (default: DEMO1)")
    parser.add_argument("--cc", default="gcc", metavar="NAME",
                        help="C compiler to build the harness with (default: gcc)")
    parser.add_argument("--perturb", choices=sorted(PERTURBATIONS), default="none",
                        help="build or run the C side with one deliberate change, "
                             "to prove the pin can fail (default: none)")
    parser.add_argument("--strict", action="store_true",
                        help="treat `faithful=0` as a failure: the run reached an "
                             "unimplemented action or an upstream line special this "
                             "port cannot execute, its player was not live at the "
                             "end, or a USE press's 64-unit ray disagrees with the "
                             "independent P_UseLines walk")
    arguments = parser.parse_args(argv)

    if not arguments.wad.is_file():
        print(f"demo_check: {arguments.wad} is not a file", file=sys.stderr)
        return EXIT_USAGE

    try:
        lump = read_wad_lump(arguments.wad, arguments.demo)
        header = DemoHeader(lump)
        commands, marker = decode_stream(lump)
        map_name = f"E{header.episode}M{header.map}"
        start = player_start(read_wad_map_things(arguments.wad, map_name))
        # The P_UseLines model: the oracle's own WAD/BSP reader, built from the
        # map's initial state. doom_ref's exceptions are not ValueError, so they
        # are caught by name below.
        _wad_kind, lumps = doom_ref.read_wad(arguments.wad)
        use_level = doom_ref.SpecLevel(doom_ref.read_bsp_map(lumps, map_name),
                                       doom_ref.RandomState(0))
        use_origin = blockmap_origin(lumps, map_name)
    except (OSError, ValueError) as error:
        print(f"demo_check: {arguments.demo}: {error}", file=sys.stderr)
        return EXIT_USAGE
    except Exception as error:  # doom_ref.InputError / FormatError, by name
        print(f"demo_check: {arguments.demo}: {error}", file=sys.stderr)
        return EXIT_USAGE

    workdir = Path(tempfile.mkdtemp(prefix="demo_check_"))
    try:
        executable = compile_harness(workdir, arguments.cc, arguments.perturb)
        _defines, flags = PERTURBATIONS[arguments.perturb]
        result = subprocess.run([str(executable), str(arguments.wad),
                                 arguments.demo, *flags],
                                capture_output=True, text=True)
        if result.returncode != 0:
            print(result.stderr or result.stdout, file=sys.stderr)
            print("demo_check: the C harness failed", file=sys.stderr)
            return EXIT_USAGE

        text = result.stdout
        print(f"{arguments.demo}: {len(commands)} recorded commands, "
              f"{marker - DEMO_HEADER_SIZE} bytes of stream + 1 marker byte, "
              f"{len(lump)} bytes of lump")
        for kind in ("DEMO", "WORLD", "ARENA", "CMD", "STATE", "RUN", "COHERE",
                     "SPEC", "ANOM", "MISSING", "SPECUNSUP", "IGNORED", "ALIVE",
                     "VERDICT"):
            line = line_of(text, kind)
            if line:
                print(f"  {line}")
        for line in lines_of(text, "RELOAD"):
            print(f"  {line}")
        for line in lines_of(text, "DEATH"):
            print(f"  {line}")
        final = record_of(text, "FINAL")
        chain = record_of(text, "CHAIN")
        print(f"  FINAL n={final.get('n')} hash={final.get('hash')}")
        print(f"  CHAIN count={chain.get('count')} hash={chain.get('hash')}")

        pins = PINS.get(arguments.demo, {})
        if arguments.perturb != "none":
            # The pin must move. The chain is the strongest aggregate this gate
            # records: every CHECK_TICS snapshot plus the final state, hashed in
            # order. A perturbation that moves any checkpoint moves the chain.
            # The final hash alone is no longer sufficient once the player dies:
            # P_DeathThink clamps the corpse's view at 6*FRACUNIT, so a
            # P_VIEWHEIGHT change can leave the last tic byte-identical while
            # every tic before the death moved.
            moved = (final.get("hash") != pins.get("final") or
                     chain.get("hash") != pins.get("chain"))
            print(f"  perturb={arguments.perturb}: pin "
                  f"{'moved, as it must' if moved else 'did NOT move'} "
                  f"(final {final.get('hash')}, chain {chain.get('hash')})")
            if not moved:
                print(f"demo_check: --perturb {arguments.perturb} did not move the "
                      "pin, so it does not measure the run", file=sys.stderr)
                return EXIT_MISMATCH
            return EXIT_OK

        failures = check_run(text, arguments.demo, commands, header, start,
                             arguments.strict,
                             lambda x, y, angle: use_lines_reach(use_level, use_origin,
                                                                 x, y, angle))
        for failure in failures:
            print(f"demo_check: {failure}", file=sys.stderr)
        if failures:
            return EXIT_MISMATCH
    except RuntimeError as error:
        print(f"demo_check: {error}", file=sys.stderr)
        return EXIT_USAGE
    finally:
        shutil.rmtree(workdir, ignore_errors=True)

    print(f"demo_check: {arguments.demo} consumed its whole stream, the two readers "
          "agree, and every coherence assertion holds")
    return EXIT_OK


if __name__ == "__main__":
    sys.exit(main())
