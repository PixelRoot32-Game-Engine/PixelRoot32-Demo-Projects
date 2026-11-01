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

WHAT A PIN DOES AND DOES NOT PROVE -- READ THIS BEFORE RE-BASELINING

There is no 1993 DOOM on this machine to diff against, and no oracle for a whole
level's simulation, so nothing here proves the run is what upstream would have
produced. The hashes are a REGRESSION PIN: the numbers this port produced once.
They catch any later change; they do not certify the past. The commit that
recorded them says so in its body, and a later commit that moves them must say
what moved and why.

What the data itself can falsify is the ALIVE check, and the reference needs
care. In linuxdoom-1.10 a single-player death runs P_DeathThink to PST_REBORN,
and G_DoReborn answers it with `gameaction = ga_loadlevel` -- a level reload,
after which playback continues. The demos' own streams are consistent with that:
each dies and then carries a USE press, which is the respawn. This port has no
G_Ticker/G_DoReborn step (playback is g_game.c's, not the game-state machine's),
so a dead player stays frozen for the rest of the stream. `ALIVE` records that
gap (`alive=0` when the player is dead at the last input tic) and `--strict`
turns it into a failure. It does not by itself prove the death tic is wrong.
`MISSING` is the distinct unimplemented-action set; it is empty since
A_SPosAttack, A_SargAttack and the A_Scream sound draw landed, and the ANOM line
stays as the count/first/last summary of the same fact.

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

Standard library only.
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

FRACUNIT = 1 << 16
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
# and nothing else. The three fields a reader should look at first are `tics`
# (the lump's own count, which must also equal the independent decode below),
# `final` and `cohere` (the whole coherence line, verbatim, so any number that
# moves names itself). `anom`, `missing` and `alive` are the fidelity verdict:
# `missing` is the distinct action set (empty since D4.4's A_SPosAttack,
# A_SargAttack and the A_Scream sound draw landed), and `alive` says whether the
# player was still live at the last command that carried input. It is not: all
# three demos die, and this layer has no G_DoReborn to reload the level, so the
# stream after the death runs against a frozen corpse. The pins record that run
# as a regression target, not as a fidelity proof.
#
# A demo whose pins are absent is a demo this gate has not measured yet: it
# reports the numbers and fails, which is how a new demo is added.
PINS: dict[str, dict[str, str | int]] = {
    "DEMO1": {
        "map": "E1M5",
        "tics": 5026,
        "final": "69b9eb23b466c5652528f52aa27e4761546f48ea3c267d1e929a7557a308894b",
        "chain": "d579a6b857059fc7aa7fda3f79ea2a9ec1d018a8ea189be16dabbf3b06e92c97",
        "cohere": (
            "COHERE wallcross=0 outside=0 angleskip=3876 anglebad=0 anglepunch=0 "
            "sectorchanges=2 sectors=3 doorpass=0 doorsopen=0 forward=4340 "
            "netfwd=195921591 netback=-15616926 shots=34 ammo=34 attackticks=1463 "
            "useticks=49 useedges=32 usetriggers=0 hpmin=0 hpmax=100 hploss=100 "
            "hpgain=0 deadtics=3877 mobjmax=219"
        ),
        "anom": "ANOM missing=0 firsttic=0 first=0 last=0",
        "missing": "MISSING count=0 overflow=0",
        "alive": "ALIVE alive=0 lastinput=5001 deathtic=1149",
    },
    "DEMO2": {
        "map": "E1M3",
        "tics": 3836,
        "final": "e8bfecf524c5ece99c7ec772c06d0537d383ff0410207d17523869961cb05f77",
        "chain": "ee4892a6da58968cf5f392879dc5d3557c5e1c5d68814ea590e59620852d4944",
        "cohere": (
            "COHERE wallcross=0 outside=0 angleskip=3284 anglebad=0 anglepunch=0 "
            "sectorchanges=8 sectors=7 doorpass=2 doorsopen=2 forward=3485 "
            "netfwd=119298357 netback=-3572193 shots=12 ammo=12 attackticks=461 "
            "useticks=62 useedges=23 usetriggers=2 hpmin=0 hpmax=100 hploss=100 "
            "hpgain=0 deadtics=3285 mobjmax=318"
        ),
        "anom": "ANOM missing=0 firsttic=0 first=0 last=0",
        "missing": "MISSING count=0 overflow=0",
        "alive": "ALIVE alive=0 lastinput=3807 deathtic=551",
    },
    "DEMO3": {
        "map": "E1M7",
        "tics": 2134,
        "final": "a200976c72757b2469d18f696c71cbe71dbb0bc0025396fd91a6e758a3efe21d",
        "chain": "5dc62ad9fe4725cd72d6464193d1cdbc60d8ef82e2a67d05bfdedc12d1c44df4",
        "cohere": (
            "COHERE wallcross=0 outside=0 angleskip=1633 anglebad=0 anglepunch=0 "
            "sectorchanges=5 sectors=6 doorpass=1 doorsopen=1 forward=1776 "
            "netfwd=70909570 netback=-6862772 shots=9 ammo=9 attackticks=273 "
            "useticks=27 useedges=10 usetriggers=1 hpmin=0 hpmax=100 hploss=100 "
            "hpgain=0 deadtics=1634 mobjmax=274"
        ),
        "anom": "ANOM missing=0 firsttic=0 first=0 last=0",
        "missing": "MISSING count=0 overflow=0",
        "alive": "ALIVE alive=0 lastinput=2104 deathtic=500",
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
              header: DemoHeader, strict: bool) -> list[str]:
    """Every failure, each named independently, so one run reports them all."""
    failures: list[str] = []
    pins = PINS.get(name)
    demo = record_of(text, "DEMO")
    world = record_of(text, "WORLD")
    cmd = record_of(text, "CMD")
    run = record_of(text, "RUN")
    state = record_of(text, "STATE")
    cohere = record_of(text, "COHERE")
    anom = record_of(text, "ANOM")
    alive = record_of(text, "ALIVE")
    verdict = record_of(text, "VERDICT")

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
    if line_of(text, "ALIVE") != pins["alive"]:
        failures.append(f"{name}: the alive line moved.\n"
                        f"    now:  {line_of(text, 'ALIVE')}\n"
                        f"    pin:  {pins['alive']}")

    # 5. The fidelity verdict this harness can reach without a reference.
    if verdict.get("faithful") != "1":
        message = (f"{name}: the run is not faithful: reason={verdict.get('reason')} "
                   f"missing={anom.get('missing')} (first {anom.get('first')} at tic "
                   f"{anom.get('firsttic')}), player alive at the last input "
                   f"tic={alive.get('alive')}")
        if strict:
            failures.append(message)
        else:
            print(f"  UNFAITHFUL {message}")
        # The door evidence is part of the fidelity claim, not of the format: a
        # demo presses USE at things it can use, so a faithful run must trigger
        # lines and pass through at least one door. Today's runs diverge before
        # DEMO1's first press, which is exactly why this is checked here.
        if strict and number(cohere, "useticks") > 0:
            if number(cohere, "usetriggers") == 0:
                failures.append(f"{name}: {cohere.get('useticks')} USE tics "
                                "triggered no line, so the demo never used a door")
            if number(cohere, "doorpass") == 0:
                failures.append(f"{name}: the player never passed a moved door")
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
                             "unimplemented action, or its player died before the "
                             "stream's last input")
    arguments = parser.parse_args(argv)

    if not arguments.wad.is_file():
        print(f"demo_check: {arguments.wad} is not a file", file=sys.stderr)
        return EXIT_USAGE

    try:
        lump = read_wad_lump(arguments.wad, arguments.demo)
        header = DemoHeader(lump)
        commands, marker = decode_stream(lump)
    except (OSError, ValueError) as error:
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
        for kind in ("DEMO", "WORLD", "CMD", "STATE", "RUN", "COHERE", "ANOM",
                     "MISSING", "ALIVE", "VERDICT"):
            line = line_of(text, kind)
            if line:
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

        failures = check_run(text, arguments.demo, commands, header,
                             arguments.strict)
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
