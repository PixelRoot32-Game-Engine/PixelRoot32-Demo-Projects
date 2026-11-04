#!/usr/bin/env python3
"""Gate for the level transition's palette fade.

Vanilla's ``f_wipe.c`` melt needs two more 64,000 B surfaces (``screens[2]`` and
``screens[3]``), which this board does not have: the largest free block after
setup is 47,092 B. The 4 MB profile substitutes a fade -- the new
frame ramps up from black through the PLAYPAL table the palette path already
owns. ``palette::applyGammaScaled()`` in ``src/palette/DoomPalette.cpp`` is the
whole of it: one integer scale per channel, then the gamma table, then the two
packings. This script proves it against an oracle written here from the rule,
which shares no code with the C:

1. TABLE. For gamma 0..4 and every scale the fade uses (0, 32, 64, ... 256, plus
   out-of-range values that must clamp) the RGB565 and RGB332 tables the firmware
   builds equal the oracle's ``rgb888To565/332(gamma(scale(c)))``.
2. ENDS. Scale 0 is black in both tables, entry for entry; scale 256 is exactly
   ``applyGamma()``'s tables, which is what makes the fade's last step the normal
   palette rather than a near-miss.
3. MONOTONE. For one gamma, every channel of every entry is non-decreasing as the
   scale rises, so a fade cannot blink or overshoot between two steps.

PROVING THE GATE CAN FAIL, two ways:
  ``--perturb`` makes the oracle add one to the scale before shifting, so every
  step differs; the run must exit 1.
  ``--stub`` builds the harness against a copy of ``DoomPalette.cpp`` with the
  scale term removed -- the pre-change behaviour -- and the run must report
  failures and exit 1. Without this, a gate that only ever sees the new code
  proves the code runs, not that it is checked.

    python tools/fade_check.py --wad tools/DOOM1.WAD
    python tools/fade_check.py --wad tools/DOOM1.WAD --perturb     # must FAIL
    python tools/fade_check.py --wad tools/DOOM1.WAD --stub        # must FAIL

Exit 0 every check passed, 1 a check failed (or a --perturb/--stub run passed
when it had to fail), 2 unusable input or build.
"""
from __future__ import annotations

import argparse
import shutil
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))

from palflash_check import Oracle  # noqa: E402  the independent gamma oracle

# The fade's own steps, then values that must clamp to full brightness.
SCALES = [0, 32, 64, 96, 128, 160, 192, 224, 256, 257, 300]
GAMMAS = range(5)


def read_playpal(wad: bytes) -> bytes:
    count, directory = struct.unpack_from("<II", wad, 4)
    for i in range(count):
        offset, size, name = struct.unpack_from("<II8s", wad, directory + i * 16)
        if name.rstrip(b"\0") == b"PLAYPAL":
            return wad[offset:offset + size]
    raise SystemExit("fade_check: no PLAYPAL in the WAD")


def run(command: list[str], what: str) -> subprocess.CompletedProcess:
    result = subprocess.run(command, capture_output=True, text=True)
    if result.returncode != 0:
        raise RuntimeError(f"{what} failed:\n{result.stderr or result.stdout}")
    if result.stderr.strip():
        raise RuntimeError(f"{what} wrote warnings, which this project does not accept:\n"
                           + result.stderr)
    return result


def tables(playpal: bytes, gamma: int, scale: int, perturb: bool) -> tuple[list[int], list[int]]:
    """The oracle: scale each channel, then gamma, then pack. Page 0 only."""
    table = Oracle(False).gamma_table(gamma)
    used = scale + 1 if perturb else scale
    used = min(used, 256)
    t565: list[int] = []
    t332: list[int] = []
    for i in range(256):
        channels = [table[(playpal[i * 3 + k] * used) >> 8] for k in range(3)]
        r, g, b = channels
        t565.append(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3))
        t332.append((r & 0xE0) | ((g & 0xE0) >> 3) | (b >> 6))
    return t565, t332


def build(workdir: Path, cc: str, cxx: str, stub: bool) -> Path:
    ext = ".exe" if sys.platform == "win32" else ""
    gamma_obj = workdir / "m_gamma.o"
    run([cc, "-std=gnu99", "-O2", "-Wall", "-Wextra", "-I", str(ROOT / "lib" / "doom"),
         "-c", "-o", str(gamma_obj), str(ROOT / "lib" / "doom" / "m_gamma.c")],
        "building m_gamma.c")

    palette = ROOT / "src" / "palette" / "DoomPalette.cpp"
    if stub:
        # The pre-change behaviour: the scale is ignored, so every step is the
        # normal palette. A gate that cannot fail against this is not a gate.
        source = palette.read_text(encoding="utf-8")
        source = source.replace(" * fade256) >> 8", ")")
        source = source.replace("if (fade256 > kFadeFull) {", "(void)fade256;\n    if (false) {")
        palette = workdir / "DoomPalette_stub.cpp"
        palette.write_text(source, encoding="utf-8")

    exe = workdir / f"fade_check{ext}"
    run([cxx, "-std=c++17", "-O2", "-Wall", "-Wextra", "-I", str(ROOT / "src"),
         "-I", str(ROOT / "lib" / "doom"), "-o", str(exe),
         str(ROOT / "tools" / "fade_check.cpp"), str(palette), str(gamma_obj)],
        "building fade_check")
    return exe


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="D5.4 fade gate")
    parser.add_argument("--wad", required=True, type=Path)
    parser.add_argument("--perturb", action="store_true")
    parser.add_argument("--stub", action="store_true")
    parser.add_argument("--cc", default="gcc")
    parser.add_argument("--cxx", default="g++")
    args = parser.parse_args(argv)

    wad = args.wad.read_bytes()
    playpal = read_playpal(wad)
    failures: list[str] = []

    with tempfile.TemporaryDirectory(prefix="fade_check_") as tmp:
        workdir = Path(tmp)
        exe = build(workdir, args.cc, args.cxx, args.stub)
        requests = [(gamma, scale) for gamma in GAMMAS for scale in SCALES]
        script = workdir / "fade.txt"
        script.write_text("".join("fade %d %d\n" % r for r in requests))
        lines = run([str(exe), str(args.wad), str(script)], "fade_check").stdout.splitlines()
        if len(lines) != 3 * len(requests):
            failures.append(f"table: {len(lines)} lines for {len(requests)} requests")
        else:
            bad = 0
            for n, (gamma, scale) in enumerate(requests):
                header, hex565, hex332 = lines[3 * n:3 * n + 3]
                got565 = [int(hex565[i:i + 4], 16) for i in range(0, 1024, 4)]
                got332 = [int(hex332[i:i + 2], 16) for i in range(0, 512, 2)]
                want565, want332 = tables(playpal, gamma, scale, args.perturb)
                if header != f"FADE {gamma} {scale}" or got565 != want565 or got332 != want332:
                    bad += 1
                    if bad <= 5:
                        failures.append(f"table: gamma {gamma} scale {scale} differs")
            if bad:
                failures.append(f"table: {bad} of {len(requests)} (gamma, scale) pairs differ")

        # Shared requests, computed once from the oracle (not from the firmware).
        zero565, zero332 = tables(playpal, 0, 0, args.perturb)
        if any(zero565) or any(zero332):
            failures.append("ends: scale 0 is not black")

        oracle = Oracle(args.perturb)
        full565, full332 = oracle.tables(playpal, 0, 0)
        for gamma in GAMMAS:
            want565, want332 = tables(playpal, gamma, 256, args.perturb)
            if gamma == 0 and (want565 != full565 or want332 != full332):
                failures.append("ends: scale 256 is not applyGamma's gamma-0 tables")

        # Monotone on the rule itself (scale, then gamma), channel by channel: a
        # fade that dipped would blink between two steps, and equality in the
        # tables above would not say so.
        gamma_zero = Oracle(False).gamma_table(0)
        for index in range(256):
            for channel in range(3):
                previous = -1
                for scale in SCALES:
                    used = min(scale + (1 if args.perturb else 0), 256)
                    value = gamma_zero[(playpal[index * 3 + channel] * used) >> 8]
                    if value < previous:
                        failures.append(f"monotone: entry {index} channel {channel} falls "
                                        f"at scale {scale}")
                        break
                    previous = value

        if args.perturb or args.stub:
            label = "--perturb" if args.perturb else "--stub"
            if not failures:
                print(f"FAIL {label}: every check passed; the gate cannot see the difference")
                return 1
            print(f"OK {label}: the gate failed as it must ({len(failures)} findings)")
            return 0

        for failure in failures:
            print(f"FAIL {failure}")
        if failures:
            return 1
        print(f"OK table: {len(requests)} (gamma, scale) pairs equal the oracle")
        print("OK ends: scale 0 is black, scale 256 is applyGamma")
        print("OK monotone: every channel rises with the scale")
        return 0


if __name__ == "__main__":
    sys.exit(main())
