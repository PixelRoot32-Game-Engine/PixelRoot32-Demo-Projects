#!/usr/bin/env python3
"""Gate for the per-level music walk (task S5, ``DOOM_LEVEL_WALK``).

The bank selects one theme per level on native, and ``tools/music_check.py``
proves every committed arrangement opens inside one second.  Neither says the
running game reaches the level and plays the theme: this script walks the
diagnostic build to one level per run, captures the APU output and reads the
result back.

HOW A RUN WORKS

The diagnostic build (``DOOM_LEVEL_WALK`` + ``DOOM_LEVEL_TEST``) tours
E1M1 -> E1M2 -> E1M3 -> E1M9 -> E1M4 -> E1M5 -> E1M6 -> E1M7 -> E1M8: each
level gets a dwell, then the fixture is stood on the level's own exit and B
(the world's USE) is pressed through the real input byte.  On native the dwell
and the press are counted in world tics (105 and 4), not frames, because the
desktop loop draws several hundred frames a second.

``DOOM_WALK_STOP=N`` (read only under ``DOOM_LEVEL_WALK && PLATFORM_NATIVE``)
ends the run with exit code 0 once level N has had its dwell.  The captured raw
file then ends at that boundary, so its last 3.0 s -- 105 tics at 35 Hz -- is
exactly the target level's dwell and nothing after it.  Unset, the same binary
walks all nine levels and never exits (the board's viewing tour).

The binary is the SDL2 native build with the two diagnostic defines.  No
committed environment carries them (``platformio.ini`` is not part of this
change), so the first run builds it once into ``.pio/walk_build`` through
PlatformIO's ``PLATFORMIO_BUILD_FLAGS`` / ``PLATFORMIO_BUILD_DIR`` overrides and
every later run reuses that binary.  ``--binary`` points the tool at another
build instead.

EVIDENCE PER LEVEL

* **theme** -- the last ``SHOW,music,event=start,track=e1mN`` line.  It must
  name the level's own theme, not the previous level's.
* **first-sound** -- seconds from the start of the target dwell to the first
  0.25 s window with any non-zero sample.  The theme must be audible from the
  level's first moments (S5: "the first seconds of every level are not silent").
* **rms** -- RMS of the final 1.0 s, in int16 sample units.
* **zero** -- whether any 0.25 s window in the whole captured run is all zeros
  (reported, not failed: the boot before E1M1 is allowed to be silent).  A
  pure-zero window *inside the target dwell* is a failure.

THE RMS FLOOR

The floor is ``RMS_FLOOR = 300`` int16 units (about -41 dBFS).  Measured over
the nine committed themes on this port the quietest final second is 977 units
(E1M6) and the loudest 2,799 (E1M8); a genuinely silent or disconnected APU path
reports 0, and a level whose arrangement collapsed to a stray click is two
orders of magnitude below the floor.  300 is therefore more than 3x below every
measured theme and far above the silence it must reject.  The figure is pinned
here, next to the data it came from, rather than inferred per run.

The raw capture and the stdout log live in a temporary directory that the run
removes; nothing is written inside the repository.

    python tools/music_walk_check.py
    python tools/music_walk_check.py --binary .pio/walk_build/native/program.exe

Exit 0 every level played its own theme above the floor with no silent window in
its dwell, 1 a level failed, 2 unusable input or a failed build.
"""
from __future__ import annotations

import argparse
import math
import os
import re
import shutil
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

#: The native walk binary the tool builds and reuses.
WALK_BUILD_DIR = ROOT / ".pio" / "walk_build"
WALK_BINARY = WALK_BUILD_DIR / "native" / ("program.exe" if sys.platform == "win32" else "program")
WALK_FLAGS = "-D DOOM_LEVEL_TEST=1 -D DOOM_LEVEL_WALK=1"

#: The dwell the walk's native arm gives every level: 105 tics at DOOM's 35 Hz.
DWELL_TICS = 105
TIC_HZ = 35.0
DWELL_SECONDS = DWELL_TICS / TIC_HZ

#: The SDL2 backend runs the APU at 22050 Hz, mono, signed 16-bit little-endian.
SAMPLE_RATE = 22050
SAMPLE_BYTES = 2

#: Analysis windows: a 0.25 s probe for silence/first sound, a 1.0 s final RMS.
WINDOW_SECONDS = 0.25
FINAL_SECONDS = 1.0

#: See "THE RMS FLOOR" above: 3x below the quietest measured theme, far above 0.
RMS_FLOOR = 300.0

LEVELS = range(1, 10)


class Failure(Exception):
    """A run that cannot produce evidence, as opposed to a failed assertion."""


def pio_executable() -> str:
    found = shutil.which("pio")
    if found:
        return found
    home = Path.home()
    for name in ("pio.exe", "pio"):
        candidate = home / ".platformio" / "penv" / "Scripts" / name
        if candidate.exists():
            return str(candidate)
    raise Failure("PlatformIO (pio) not found: put it on PATH or in %USERPROFILE%\\.platformio\\penv\\Scripts")


def build_walk_binary() -> Path:
    """Builds the diagnostic native binary once, into its own build directory."""
    pio = pio_executable()
    env = os.environ.copy()
    env["PLATFORMIO_BUILD_FLAGS"] = WALK_FLAGS
    env["PLATFORMIO_BUILD_DIR"] = str(WALK_BUILD_DIR)
    print(f"building the walk diagnostic once ({WALK_FLAGS})", flush=True)
    result = subprocess.run([pio, "run", "-e", "native"], cwd=ROOT, env=env,
                            capture_output=True, text=True)
    if result.returncode != 0:
        raise Failure("the walk build failed:\n" + (result.stderr or result.stdout)[-3000:])
    if not WALK_BINARY.exists():
        raise Failure(f"the walk build produced no {WALK_BINARY}")
    return WALK_BINARY


def walk_binary(argument: Path | None) -> Path:
    if argument is not None:
        if not argument.exists():
            raise Failure(f"--binary {argument} does not exist")
        return argument
    if WALK_BINARY.exists():
        return WALK_BINARY
    return build_walk_binary()


def read_samples(path: Path) -> list[int]:
    data = path.read_bytes()
    count = len(data) // SAMPLE_BYTES
    if count == 0:
        raise Failure(f"{path} captured no samples")
    return list(struct.unpack(f"<{count}h", data[:count * SAMPLE_BYTES]))


def rms(samples: list[int]) -> float:
    if not samples:
        return 0.0
    return math.sqrt(sum(value * value for value in samples) / len(samples))


def windows(samples: list[int], start: int, size: int):
    """Yields (offset, window) over `samples` from `start`, whole windows only."""
    offset = start
    while offset + size <= len(samples):
        yield offset, samples[offset:offset + size]
        offset += size


def run_level(binary: Path, level: int, workdir: Path, timeout: float) -> tuple[subprocess.CompletedProcess, Path]:
    raw = workdir / f"level{level}.raw"
    env = os.environ.copy()
    env["DOOM_WALK_STOP"] = str(level)
    env["SDL_VIDEODRIVER"] = "dummy"
    env["SDL_AUDIODRIVER"] = "disk"
    env["SDL_DISKAUDIOFILE"] = str(raw)
    try:
        result = subprocess.run([str(binary)], cwd=ROOT, env=env, capture_output=True,
                                text=True, timeout=timeout)
    except subprocess.TimeoutExpired:
        raise Failure(f"the run did not exit within {timeout:.0f} s "
                      "(DOOM_WALK_STOP did not stop it)")
    return result, raw


def parse_stdout(level: int, stdout: str) -> list[str]:
    """Validates the run's own log and returns the failures it shows."""
    failures: list[str] = []
    theme = f"e1m{level}"
    marker = f"E1M{level}"

    starts = re.findall(r"SHOW,music,event=start,track=(e1m[1-9])", stdout)
    exits = re.findall(r"SHOW,walk,exit=level=([1-9])", stdout)
    transitions = re.findall(r"SHOW,level,from=(E1M[1-9]),to=(E1M[1-9])", stdout)

    if not starts:
        failures.append("no SHOW,music,event=start line: the theme never started")
    elif starts[-1] != theme:
        failures.append(f"the last theme started was {starts[-1]}, not {theme}")

    if not exits:
        failures.append("no SHOW,walk,exit line: DOOM_WALK_STOP did not end the run")
    elif int(exits[-1]) != level:
        failures.append(f"the run exited on level {exits[-1]}, not {level}")

    if level != 1:
        if not transitions:
            failures.append("no SHOW,level,from line: the walk never changed level")
        elif transitions[-1][1] != marker:
            failures.append(f"the last transition was {transitions[-1][0]}->{transitions[-1][1]}, "
                            f"not {marker}")
    return failures


def analyse(level: int, samples: list[int], log_failures: list[str]) -> tuple[str, float, float, int, bool]:
    """Returns (theme, first_sound, final_rms, zero_windows, ok)."""
    theme = f"e1m{level}"
    probe = max(1, int(round(WINDOW_SECONDS * SAMPLE_RATE)))
    final = max(1, int(round(FINAL_SECONDS * SAMPLE_RATE)))
    dwell = int(round(DWELL_SECONDS * SAMPLE_RATE))

    failures = list(log_failures)
    if len(samples) < dwell:
        failures.append(f"captured {len(samples) / SAMPLE_RATE:.2f} s, shorter than the "
                        f"{DWELL_SECONDS:.2f} s target dwell")
        dwell_start = 0
    else:
        # The run exits at the dwell's end, so the tail is the target level.
        dwell_start = len(samples) - dwell

    first_sound = math.inf
    silent_in_dwell = 0
    for offset, window in windows(samples, dwell_start, probe):
        if any(window):
            if first_sound is math.inf:
                first_sound = (offset - dwell_start) / SAMPLE_RATE
        else:
            silent_in_dwell += 1

    if first_sound is math.inf:
        failures.append("no non-zero window inside the target dwell: the theme is silent")
        first_sound = 0.0
    if silent_in_dwell:
        failures.append(f"{silent_in_dwell} pure-zero window(s) inside the target dwell")

    final_rms = rms(samples[-final:])
    if final_rms < RMS_FLOOR:
        failures.append(f"final-second RMS {final_rms:.1f} is below the {RMS_FLOOR:.0f} floor")

    zero_windows = sum(1 for _, window in windows(samples, 0, probe) if not any(window))

    for failure in failures:
        print(f"FAIL level {level}: {failure}")
    return theme, first_sound, final_rms, zero_windows, not failures


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="S5 per-level music walk gate")
    parser.add_argument("--binary", type=Path, default=None,
                        help="an already-built DOOM_LEVEL_WALK native binary")
    parser.add_argument("--timeout", type=float, default=180.0,
                        help="seconds allowed for one level's run")
    args = parser.parse_args(argv)

    try:
        binary = walk_binary(args.binary)
    except Failure as error:
        print(f"FAIL {error}")
        return 2
    print(f"walk binary: {binary}")

    rows: list[tuple[int, str, float, float, int, bool]] = []
    with tempfile.TemporaryDirectory(prefix="doom_music_walk_") as temporary:
        workdir = Path(temporary)
        for level in LEVELS:
            try:
                result, raw = run_level(binary, level, workdir, args.timeout)
            except Failure as error:
                print(f"FAIL level {level}: {error}")
                rows.append((level, f"e1m{level}", math.inf, 0.0, 0, False))
                continue
            log_failures: list[str] = []
            if result.returncode != 0:
                log_failures.append(f"the run exited {result.returncode}, not 0")
            log_failures += parse_stdout(level, result.stdout)
            try:
                samples = read_samples(raw)
            except Failure as error:
                print(f"FAIL level {level}: {error}")
                rows.append((level, f"e1m{level}", math.inf, 0.0, 0, False))
                continue
            rows.append((level, *analyse(level, samples, log_failures)))

    print()
    print("level  theme  first-sound  rms     zero-windows  status")
    for level, theme, first_sound, final_rms, zero_windows, ok in rows:
        first = "  -  " if first_sound is math.inf else f"{first_sound:5.2f}"
        print(f"{level:5d}  {theme:<5}  {first:>10}  {final_rms:7.1f}  "
              f"{zero_windows:12d}  {'ok' if ok else 'FAIL'}")

    failed = [row[0] for row in rows if not row[5]]
    if failed:
        print(f"\nFAIL {len(failed)} of 9 levels: {', '.join(str(level) for level in failed)}")
        return 1
    print("\nOK every level played its own theme above the floor with no silent window in its dwell")
    return 0


if __name__ == "__main__":
    sys.exit(main())
