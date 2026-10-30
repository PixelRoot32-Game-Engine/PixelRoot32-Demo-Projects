#!/usr/bin/env python3
"""Writes tools/e1m1_sprites.inc from one WAD, and checks the committed copy.

WHAT IT DOES

  1. reads the WAD's THINGS lump for E1M1 and collects the DoomEd numbers;
  2. refuses to run when a number is neither a player/deathmatch start (1-4,
     11: not mobjs, so not sprites) nor a row of the transcribed mobjinfo table
     in tools/e1m1_sprites.py;
  3. builds R_InitSpriteDefs()'s sprite table from the S_START..S_END lumps and
     proves every selected frame has each rotation the projection can ask for;
  4. emits the C table, or (with --check) compares it with the file on disk and
     exits 1 when they differ.

The emitted file holds DoomEd numbers, sprite names, frame indices and rotation
lump names -- fixture data. It holds no map coordinate, no texture byte and no
sprite pixel, and lib/doom/ never includes it.

USAGE

    python tools/gen_e1m1_sprites.py --wad path/to/DOOM1.WAD
    python tools/gen_e1m1_sprites.py --wad path/to/DOOM1.WAD --check

Standard library only.
"""

from __future__ import annotations

import argparse
import struct
import sys
from pathlib import Path

import doom_ref
import e1m1_sprites

PLAYER_START_TYPES = frozenset({1, 2, 3, 4, 11})
THING_RECORD_SIZE = 10


def project_root() -> Path:
    return Path(__file__).resolve().parent.parent


def thing_types(lumps: list[doom_ref.Lump], map_name: str) -> list[int]:
    """The DoomEd number of every record in the map's THINGS lump, in order."""
    try:
        marker = next(index for index, lump in enumerate(lumps)
                      if lump.name == map_name)
    except StopIteration as error:
        raise doom_ref.InputError(f"this WAD has no {map_name}") from error
    if marker + 1 >= len(lumps) or lumps[marker + 1].name != "THINGS":
        raise doom_ref.InputError(f"{map_name} is not followed by THINGS")
    data = lumps[marker + 1].data
    if len(data) % THING_RECORD_SIZE != 0:
        raise doom_ref.FormatError(f"THINGS is {len(data)} B, not a multiple of 10")
    types: list[int] = []
    for at in range(0, len(data), THING_RECORD_SIZE):
        _x, _y, _angle, kind, _options = struct.unpack_from("<hhHHH", data, at)
        types.append(kind)
    return types


def check_types(types: list[int]) -> dict[int, int]:
    """Counts the types and rejects one the fixture table does not cover."""
    counts: dict[int, int] = {}
    for kind in types:
        counts[kind] = counts.get(kind, 0) + 1
    missing = sorted(kind for kind in counts
                     if kind not in PLAYER_START_TYPES
                     and e1m1_sprites.rule_for(kind) is None)
    if missing:
        raise doom_ref.FormatError(
            "E1M1 contains a thing type the fixture table does not cover: "
            + ", ".join(str(kind) for kind in missing))
    return counts


def check_rotations(tables: dict[str, dict[int, e1m1_sprites.FrameTable]],
                    rows: list[e1m1_sprites.ThingSprite]) -> None:
    """Every rotation the projection can produce must have a lump."""
    for row in rows:
        for rotation in range(8):
            try:
                lump, _flip = e1m1_sprites.select_sprite(tables, row.sprite,
                                                         row.frame, rotation)
            except ValueError as error:
                raise doom_ref.FormatError(str(error)) from error
            if len(lump) not in (6, 8):
                raise doom_ref.FormatError(
                    f"{row.sprite} frame {row.frame}: lump {lump!r} is not 6 or 8 "
                    "characters")


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        prog="gen_e1m1_sprites.py",
        description="Emit tools/e1m1_sprites.inc from a WAD's E1M1.",
    )
    parser.add_argument("--wad", type=Path, required=True, metavar="PATH",
                        help="the IWAD to read; keep it outside the repository")
    parser.add_argument("--map", default="E1M1", metavar="NAME",
                        help="the level whose THINGS are transcribed (default: E1M1)")
    parser.add_argument("--out", type=Path, metavar="PATH",
                        help="write here instead of tools/e1m1_sprites.inc")
    parser.add_argument("--check", action="store_true",
                        help="compare with the file on disk; write nothing")
    arguments = parser.parse_args(argv)

    if not arguments.wad.is_file():
        print(f"gen_e1m1_sprites: {arguments.wad} is not a file", file=sys.stderr)
        return 2

    try:
        _identification, lumps = doom_ref.read_wad(arguments.wad)
        types = thing_types(lumps, arguments.map)
        check_types(types)
        tables = e1m1_sprites.build_sprite_tables(lump.name for lump in lumps)
        rows = [e1m1_sprites.RULE_BY_TYPE[kind] for kind in sorted(set(types))
                if kind in e1m1_sprites.RULE_BY_TYPE]
        check_rotations(tables, rows)
    except (doom_ref.InputError, doom_ref.FormatError, ValueError) as error:
        print(f"gen_e1m1_sprites: {error}", file=sys.stderr)
        return 2

    text = e1m1_sprites.emit_inc(tables, rows)
    target = arguments.out if arguments.out is not None else (
        project_root() / "tools" / "e1m1_sprites.inc")

    if arguments.check:
        try:
            current = target.read_text(encoding="ascii")
        except OSError as error:
            print(f"gen_e1m1_sprites: {error}", file=sys.stderr)
            return 2
        if current != text:
            print(f"gen_e1m1_sprites: {target} is out of date", file=sys.stderr)
            return 1
        print(f"gen_e1m1_sprites: {target} matches the WAD")
        return 0

    with open(target, "w", encoding="ascii", newline="\n") as handle:
        handle.write(text)
    print(f"gen_e1m1_sprites: wrote {target.name}: "
          f"{len(rows)} thing types, "
          f"{sum(1 for row in rows if row.fullbright)} full bright")
    return 0


if __name__ == "__main__":
    sys.exit(main())
