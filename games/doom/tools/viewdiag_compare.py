#!/usr/bin/env python3
"""Diffs a board's `VD,...` frame records against the host's render of the same view.

WHAT IT IS FOR

  A board-only tear that the host cannot reproduce is a disagreement about the
  *inputs* of one drawing pass: the drawsegs the walk stored, the clip codes the
  masked pass reads back, the visplanes the walk allocated, the submitted sprite
  list. The picture says "wrong"; this says which record and which field.

HOW TO GET THE TWO SIDES

  Board: flash `esp32dev_ILI9341_hud_dump`, stand at the viewpoint, hold A+B for
  one burst and capture the console. The burst prints the frame as RLE
  (`DUMP,...`) and then the records (`VD,...`) plus the sector table
  (`SHOW,s,...`). Any capture that holds those lines works; nothing else is
  needed.

      python tools/viewdiag_compare.py board_capture.log host_reference.txt

  Host: the harness prints the same lines for the same viewpoint, so build it
  once and keep its output:

      gcc -std=gnu99 -O2 -Wall -Wextra -D DOOM_HUD=1 -I lib/doom -I tools \\
          -o rsegs_check tools/rsegs_check.c \\
          lib/doom/r_segs.c lib/doom/r_light.c lib/doom/r_bsp.c \\
          lib/doom/r_plane.c lib/doom/r_things.c lib/doom/r_view_diag.c \\
          lib/doom/r_tables.c lib/doom/r_data.c lib/doom/doom_map.c \\
          lib/doom/w_wad.c
      ./rsegs_check tools/DOOM1.WAD --view X,Y,Z,ANGLE --viewdiag > host.txt

  Use the `DUMP,view,x=...,y=...,z=...,angle=...` line of the same capture as
  `--view`, and feed the host the board's own sector table if the room strobes
  (the harness's `--lights FILE` option; without it the host renders the WAD's static light and every lit field differs).

WHAT A DIFFERENCE MEANS

  VD,cfg    the two `viewh/centery` pairs are the CALLER's macros and lib/doom's
            own translation unit's. A disagreement between them is a build that
            drew a view into a window it did not size -- check first, because it
            invalidates every other line.
  VD,pool   need/used from the wall preflight and the wall draw; a `need` above
            the board's clip capacity is a refused frame, a `used` above it is a
            frame the pool could not hold.
  VD,d      one drawseg: x1..x2, the three scales (8 hex digits), the masked
            texture's column block `mc` (and its name), the two clip codes
            `ct`/`cb`, the silhouette and the light index. The masked pass reads
            `mc`/`ct`/`cb` straight out of the shared clip pool, so a mid texture
            clipped with another fragment's numbers fans visibly on the
            panel.
  VD,p      one visplane: height, flat, light, the recorded column range, the
            number of recorded columns and the extrema of top[]/bottom[]. A
            correct header over a corrupted span map shows up in the extrema.
  VD,s      one submitted sprite: its world position. A row whose x/y equal the
            view's is the camera itself, which is the one submission the host's
            THINGS fixture can never contain.

  Exit code 0 when the two sides agree, 1 when they differ (so a script can use
  it), 2 when an input cannot be read.

Standard library only.
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

#: The fields of each record, in the order `r_view_diag_*` writes them.
FIELDS = {
    "cfg": ("caller_viewh", "caller_centery", "lib_viewh", "lib_centery",
            "screen_w", "drawseg_cap", "plane_cap", "clip_cap", "vis_cap"),
    "pool": ("need", "used", "drawsegs", "planes"),
    "d": ("i", "seg", "mid", "x1", "x2", "s1", "s2", "step", "mc", "ct", "cb",
          "sil", "ln"),
    "p": ("i", "height", "flat", "light", "minx", "maxx", "spans", "tmin",
          "tmax", "bmin", "bmax"),
    "s": ("i", "x", "y", "z", "angle", "light", "lump"),
}

#: Records that stand alone; every other tag is keyed by its leading index.
SINGLETONS = frozenset({"cfg", "pool"})


def read_records(path: Path) -> dict[tuple[str, str], list[str]]:
    """Every `VD,...` line in @p path, keyed by (tag, index).

    Lines that are not `VD,` are ignored, so a whole console capture -- the RLE
    frame, the sector table, the tic lines -- can be handed in unedited.
    """
    records: dict[tuple[str, str], list[str]] = {}
    try:
        text = path.read_text(encoding="utf-8", errors="replace")
    except OSError as error:
        raise SystemExit("viewdiag_compare: %s: %s" % (path, error.strerror))
    for line in text.splitlines():
        if not line.startswith("VD,"):
            continue
        parts = line.split(",")
        if len(parts) < 2:
            continue
        tag = parts[1]
        if tag == "end":
            continue
        if tag not in FIELDS:
            continue
        body = parts[2:]
        key = (tag, "-") if tag in SINGLETONS else (tag, body[0] if body else "?")
        records[key] = body
    return records


def describe(tag: str, body: list[str], at: int) -> str:
    """The field name of @p body[@p at], or its position when it has no name."""
    names = FIELDS.get(tag, ())
    return names[at] if 0 <= at < len(names) else "#%d" % at


def compare(board: dict, host: dict, limit: int) -> int:
    """Prints the differences; returns the number of records that differ."""
    differing = 0
    only_board = sorted(set(board) - set(host))
    only_host = sorted(set(host) - set(board))

    for key in sorted(set(board) & set(host)):
        mine, theirs = board[key], host[key]
        tag = key[0]
        if mine == theirs:
            continue
        differing += 1
        if differing > limit:
            continue
        label = "%s%s" % (tag, "" if tag in SINGLETONS else " " + key[1])
        if len(mine) != len(theirs):
            print("%s: %d fields on the board, %d on the host" % (label, len(mine),
                                                                  len(theirs)))
        for at in range(min(len(mine), len(theirs))):
            if mine[at] != theirs[at]:
                print("%s: %s board=%s host=%s" % (label, describe(tag, mine, at),
                                                   mine[at], theirs[at]))

    for key in only_board[:limit]:
        print("%s %s: on the board, absent on the host" % key)
    for key in only_host[:limit]:
        print("%s %s: on the host, absent on the board" % key)
    if differing > limit:
        print("... %d records differ in total (limit %d)" % (differing, limit))
    return differing + len(only_board) + len(only_host)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        prog="viewdiag_compare.py",
        description="Diff a board capture's VD records against the host's.",
    )
    parser.add_argument("board", type=Path,
                        help="console capture holding the board's VD,... lines")
    parser.add_argument("host", type=Path,
                        help="rsegs_check --viewdiag output for the same viewpoint")
    parser.add_argument("--limit", type=int, default=40, metavar="N",
                        help="stop listing after N differing records (default 40)")
    arguments = parser.parse_args(argv)

    board = read_records(arguments.board)
    host = read_records(arguments.host)
    if not board:
        print("viewdiag_compare: %s holds no VD lines" % arguments.board,
              file=sys.stderr)
        return 2
    if not host:
        print("viewdiag_compare: %s holds no VD lines" % arguments.host,
              file=sys.stderr)
        return 2

    differing = compare(board, host, arguments.limit)
    if differing == 0:
        print("viewdiag_compare: %d records agree" % len(board))
        return 0
    print("viewdiag_compare: %d record(s) differ (board %d, host %d)"
          % (differing, len(board), len(host)))
    return 1


if __name__ == "__main__":
    sys.exit(main())
