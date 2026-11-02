#!/usr/bin/env python3
"""Gate for the boot and failure screens (D7.2).

A boot that cannot open the WAD used to leave the panel on the Phase 0 shell: a
geometric pattern with no text, indistinguishable from a hung board. The fix is a
screen that owns its pixels -- a 5x7 column font for ASCII 32..95 and an RGB332
painter in ``src/doomc/BootScreen.cpp`` -- so it can report "the WAD is missing"
without a WAD font to borrow.

This gate compiles the firmware's own painter and taxonomy on the host, with no
engine, no WAD and no APU, and hashes each screen state's 320x200 bytes:

1. LOADING plus one screen per ``LevelFailureReason``: every title, body line,
   hint and footer is printable ASCII 32..95 (the font's alphabet), each
   rendered line is at most 25 characters, and ``bootScreenFits()`` holds.
2. TAXONOMY: no two failure reasons share a title, every screen paints something,
   and every error screen's footer carries the machine pair (``CODE <n> <LEVEL>``)
   so a photograph of the panel identifies the branch.
3. RECORD: the title, lines, footer and SHA-256 of every state must equal
   ``tools/boot_screens.txt``. Any picture or text change is a named failure and
   not only a silent firmware change.

PROVING THE GATE CAN FAIL, two ways:
  ``--perturb`` shifts one string (a leading space in every title): the pixels and
  the hashes differ, so the run must report findings and exit 1.
  ``--stub`` renders a black surface: every state is blank and every hash differs,
  so the run must report findings and exit 1.

    python tools/boot_check.py                # green against the record
    python tools/boot_check.py --perturb      # must fail
    python tools/boot_check.py --stub         # must fail
    python tools/boot_check.py --record       # rewrite tools/boot_screens.txt

Exit 0 every check passed, 1 a check failed (or a --perturb/--stub run passed
when it had to fail), 2 unusable input or build.
"""
from __future__ import annotations

import argparse
import re
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
RECORD = ROOT / "tools" / "boot_screens.txt"
WIDTH, HEIGHT = 320, 200
MAX_LINE = 25
HASH = re.compile(r"^[0-9a-f]{64}$")


def run(command: list[str], what: str) -> subprocess.CompletedProcess:
    result = subprocess.run(command, capture_output=True, text=True)
    if result.returncode != 0:
        raise RuntimeError(f"{what} failed:\n{result.stderr or result.stdout}")
    if result.stderr.strip():
        raise RuntimeError(f"{what} wrote warnings, which this project does not accept:\n"
                           + result.stderr)
    return result


def build(workdir: Path, cxx: str) -> Path:
    ext = ".exe" if sys.platform == "win32" else ""
    exe = workdir / f"boot_check{ext}"
    run([cxx, "-std=c++17", "-O2", "-Wall", "-Wextra",
         "-I", str(ROOT / "src"), "-I", str(ROOT / "src" / "doomc"),
         "-o", str(exe),
         str(ROOT / "tools" / "boot_check.cpp"),
         str(ROOT / "src" / "doomc" / "BootScreen.cpp"),
         str(ROOT / "src" / "doomc" / "LevelFailure.cpp")],
        "building boot_check")
    return exe


def parse(text: str) -> tuple[list[dict[str, object]], int]:
    states: list[dict[str, object]] = []
    reasons = -1
    current: dict[str, object] | None = None
    for raw in text.splitlines():
        if raw.startswith("STATE "):
            current = {"name": raw[6:], "title": "", "lines": [], "footer": "",
                       "code": -1, "fits": 0, "lit": 0, "hash": ""}
            states.append(current)
        elif raw == "END":
            current = None
        elif raw.startswith("REASONS "):
            reasons = int(raw.split()[1])
        elif current is not None:
            key, _, value = raw.partition(" ")
            if key == "LINE":
                current["lines"].append(value)
            elif key in {"TITLE", "FOOTER", "HASH"}:
                current[key.lower()] = value
            elif key == "CODE":
                current["code"] = int(value)
            elif key in {"FITS", "LIT"}:
                current[key.lower()] = int(value)
    return states, reasons


def ascii_upper(text: str) -> bool:
    """The font's alphabet: printable ASCII 32..95, no lowercase, no accents."""
    return all(32 <= ord(character) <= 95 for character in text)


def findings(states: list[dict[str, object]], reasons: int) -> list[str]:
    problems: list[str] = []

    if reasons <= 0:
        problems.append("taxonomy: the harness reported no LevelFailureReason count")
    if len(states) != reasons + 1:
        problems.append(f"states: {len(states)} blocks for {reasons} reasons plus loading")
    if not states or states[0]["name"] != "LOADING":
        problems.append("states: the first block is not LOADING")

    titles: dict[str, str] = {}
    for state in states[1:]:
        name = str(state["name"])
        title = str(state["title"])
        if title in titles:
            problems.append(f"taxonomy: {name} and {titles[title]} share the title {title!r}")
        titles[title] = name

    for state in states:
        name = str(state["name"])
        title = str(state["title"])
        lines = [str(line) for line in state["lines"]]  # type: ignore[union-attr]
        footer = str(state["footer"])
        digest = str(state["hash"])

        if state["fits"] != 1:
            problems.append(f"{name}: bootScreenFits() is false at {WIDTH}x{HEIGHT}")
        if state["lit"] == 0:
            problems.append(f"{name}: the screen paints nothing")
        if not title:
            problems.append(f"{name}: empty title")
        if not lines:
            problems.append(f"{name}: no body line")
        if not footer:
            problems.append(f"{name}: empty footer")
        if not HASH.match(digest):
            problems.append(f"{name}: malformed hash {digest!r}")

        for label, text in [("title", title), ("footer", footer)] + \
                [(f"line {index + 1}", line) for index, line in enumerate(lines)]:
            if not ascii_upper(text):
                problems.append(f"{name}: {label} is not ASCII 32..95: {text!r}")
            if len(text) > MAX_LINE and label.startswith("line"):
                problems.append(f"{name}: {label} is {len(text)} characters (max {MAX_LINE})")

        if name != "LOADING":
            code = state["code"]
            if "CODE" not in footer or str(code) not in footer:
                problems.append(f"{name}: footer {footer!r} lacks CODE {code}")
            if "E1M1" not in footer:
                problems.append(f"{name}: footer {footer!r} lacks the level E1M1")

    return problems


def record_text(states: list[dict[str, object]]) -> str:
    out = ["# Boot screen record (D7.2). Generated by tools/boot_check.py --record;",
           "# commit it. One block per state: the text the panel shows and the",
           "# SHA-256 of its 320x200 RGB332 bytes."]
    for state in states:
        out.append(str(state["name"]))
        out.append(f"  title: {state['title']}")
        for line in state["lines"]:  # type: ignore[union-attr]
            out.append(f"  line: {line}")
        out.append(f"  footer: {state['footer']}")
        out.append(f"  hash: {state['hash']}")
    return "\n".join(out) + "\n"


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="D7.2 boot/error screen gate")
    parser.add_argument("--cxx", default="g++")
    parser.add_argument("--perturb", action="store_true",
                        help="shift one string; the run must then fail")
    parser.add_argument("--stub", action="store_true",
                        help="render a blank surface; the run must then fail")
    parser.add_argument("--record", action="store_true",
                        help="write tools/boot_screens.txt from this run")
    arguments = parser.parse_args(argv)

    if arguments.perturb and arguments.stub:
        print("boot_check: --perturb and --stub are separate proofs", file=sys.stderr)
        return 2

    with tempfile.TemporaryDirectory(prefix="boot_check_") as tmp:
        workdir = Path(tmp)
        try:
            exe = build(workdir, arguments.cxx)
            command = [str(exe)]
            if arguments.perturb:
                command.append("--perturb")
            if arguments.stub:
                command.append("--stub")
            output = run(command, "boot_check harness").stdout
        except (RuntimeError, OSError) as error:
            print(f"boot_check: {error}", file=sys.stderr)
            return 2

    states, reasons = parse(output)
    problems = findings(states, reasons)

    if arguments.record:
        if problems:
            for problem in problems:
                print(f"FAIL {problem}")
            return 1
        RECORD.write_text(record_text(states), encoding="utf-8")
        print(f"OK record: {len(states)} states written to {RECORD.relative_to(ROOT)}")
        return 0

    try:
        committed = RECORD.read_text(encoding="utf-8")
    except OSError:
        print(f"boot_check: {RECORD} is missing; run --record and commit it",
              file=sys.stderr)
        return 2

    if record_text(states) != committed:
        old_lines = committed.splitlines()
        new_lines = record_text(states).splitlines()
        reported = False
        for index, (old, new) in enumerate(zip(old_lines, new_lines)):
            if old != new:
                problems.append(f"record: line {index + 1} differs: "
                                f"{old.strip()!r} became {new.strip()!r}")
                reported = True
                break
        if not reported:
            problems.append("record: the file has a different number of lines")

    if arguments.perturb or arguments.stub:
        label = "--perturb" if arguments.perturb else "--stub"
        if not problems:
            print(f"FAIL {label}: every check passed; the gate cannot see the difference")
            return 1
        print(f"OK {label}: the gate failed as it must ({len(problems)} findings)")
        return 0

    for problem in problems:
        print(f"FAIL {problem}")
    if problems:
        return 1

    print(f"OK states: loading plus {reasons} failure screens, all fitting {WIDTH}x{HEIGHT}")
    print(f"OK text: ASCII 32..95, every rendered line <= {MAX_LINE} characters")
    print("OK taxonomy: distinct titles, CODE/level pair in every error footer")
    print("OK record: every title, line, footer and hash matches tools/boot_screens.txt")
    return 0


if __name__ == "__main__":
    sys.exit(main())
