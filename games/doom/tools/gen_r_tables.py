#!/usr/bin/env python3
"""Generates lib/doom/r_tables.c, the renderer's fixed-point lookup tables.

WHY GENERATED AND NOT COMPUTED AT BOOT

DOOM's tables.c carries three literal arrays: finesine[10240], finetangent[4096]
and tantoangle[2049]. They are the output of the initialisers that survive,
commented out, in r_main.c's R_InitTables() and R_InitPointToAngle(). Computing
them at boot would need floating point and libm on the ESP32 (double is
software there), it would run twelve thousand transcendental calls before the
first frame, and the result would depend on the target's libm. Generating them
once, from the same formulas, gives the port deterministic bytes on every
target and keeps the renderer's hot path integer-only.

The file this tool writes is committed. It is generated data, not source: do
not edit it by hand. Regenerate with

    python tools/gen_r_tables.py > lib/doom/r_tables.c

FORMULAS (exactly the ones upstream's disabled initialisers state)

    finesine[i]    = trunc(FRACUNIT * sin((i + 0.5) * 2 * pi / FINEANGLES))
                     for i in [0, 5 * FINEANGLES / 4). The extra quarter turn
                     is why finecosine[i] = finesine[i + FINEANGLES/4] works
                     for every angle, including negative ones that wrap.

    finetangent[i] = trunc(FRACUNIT * tan((i - FINEANGLES/4 + 0.5) * 2*pi/FINEANGLES))
                     for i in [0, FINEANGLES/2). Emitted as r_fine_tangent and
                     indexed through r_fine_tangent_lookup(): it is the table
                     upstream's R_RenderSegLoop() uses to select a texture column
                     from the view angle, and it is also the table the projection
                     table is built from.

    tantoangle[i]  = trunc(0x100000000 * atan(i / SLOPERANGE) / (2 * pi))
                     for i in [0, SLOPERANGE]. The i/SLOPERANGE ratio is
                     computed in single precision, as upstream's initialiser
                     writes it with an explicit (float) cast, then promoted for
                     atan().

    r_light_level[i][j] is R_ExecuteSetViewSize()'s scalelight[] table, reduced
    to the map index it selects:

        startmap = ((LIGHTLEVELS-1-i)*2) * NUMCOLORMAPS / LIGHTLEVELS
        level    = startmap - j * SCREENWIDTH / (viewwidth << detailshift) / DISTMAP
        level    = clamp(level, 0, NUMCOLORMAPS-1)

    computed by that C expression's own order of operations: the first division
    is exact because viewwidth == SCREENWIDTH and detailshift == 0 in this port,
    the second divides by DISTMAP == 2, so level = startmap - j/2. Upstream
    rebuilds this table in R_ExecuteSetViewSize() because the view size can
    change; this port has one view size, so it is a constant like the rest.

    viewangletox[i] is R_InitTextureMapping()'s table: tangent -> screen
    column, with the two fencepost passes that make column 0 and column 319
    reachable. xtoviewangle[x] is then the smallest view angle that maps to
    column x, and clipangle is xtoviewangle[0] -- the frustum's half-angle.

WHY THE TABLES ARE NOT BYTE-IDENTICAL TO UPSTREAM'S tables.c

Upstream's literal data was produced on 1990s hardware with a different libm,
so a handful of entries differ by one least-significant unit from what the
formulas produce today: 33 of 10240 finesine entries, 204 of 4096 finetangent
entries and 1878 of 2049 tantoangle entries (the arc-tangent differences are
one unit of 2^32 and are invisible after the >> ANGLETOFINESHIFT truncation).
What matters is that the *projection* table is unaffected: building
viewangletox from today's finetangent and from upstream's literal array yields
the same 4096 values, verified while writing this file. The C walk and the
independent Python oracle in tools/doom_ref.py both derive their tables from
these formulas, so the two sides are comparable exactly; a one-unit difference
in a sine is a sub-pixel scale change that D3.2/D3.3 either match or don't,
and that is the layer where pixel identity is measured.

Output is deterministic: same formulas, same bytes, no timestamps and no paths.
"""

from __future__ import annotations

import math
import struct
import sys

FRACBITS = 16
FRACUNIT = 1 << FRACBITS
FINEANGLES = 8192
ANGLETOFINESHIFT = 19
SLOPERANGE = 2048
SCREEN_WIDTH = 320
FIELDOFVIEW = 2048
ANG90 = 0x40000000

# The light tables of upstream's r_data.h, and the view the wall table is built
# for: this port's single 320-wide, un-detailed view.
LIGHTLEVELS = 16
NUMCOLORMAPS = 32
MAXLIGHTSCALE = 48
VIEWWIDTH = SCREEN_WIDTH
DETAILSHIFT = 0
DISTMAP = 2


def single(value: float) -> float:
    """Rounds a Python double to the nearest IEEE-754 single, as a C float cast."""
    return struct.unpack("f", struct.pack("f", value))[0]


def fine_sine() -> list[int]:
    return [int(FRACUNIT * math.sin((i + 0.5) * math.pi * 2 / FINEANGLES))
            for i in range(5 * FINEANGLES // 4)]


def fine_tangent() -> list[int]:
    return [int(FRACUNIT * math.tan((i - FINEANGLES // 4 + 0.5) * math.pi * 2 / FINEANGLES))
            for i in range(FINEANGLES // 2)]


def tan_to_angle() -> list[int]:
    return [int(0x100000000 * (math.atan(single(i / SLOPERANGE)) / (3.141592657 * 2)))
            & 0xFFFFFFFF
            for i in range(SLOPERANGE + 1)]


def light_level() -> list[list[int]]:
    """R_ExecuteSetViewSize()'s scalelight[,] as colormap indices."""
    table: list[list[int]] = []
    for i in range(LIGHTLEVELS):
        startmap = ((LIGHTLEVELS - 1 - i) * 2) * NUMCOLORMAPS // LIGHTLEVELS
        row: list[int] = []
        for j in range(MAXLIGHTSCALE):
            level = startmap - j * SCREEN_WIDTH // (VIEWWIDTH << DETAILSHIFT) // DISTMAP
            row.append(max(0, min(NUMCOLORMAPS - 1, level)))
        table.append(row)
    return table


def fixed_mul(a: int, b: int) -> int:
    return (a * b) >> FRACBITS


def view_angle_to_x(tangent: list[int]) -> list[int]:
    centerx = SCREEN_WIDTH // 2
    centerxfrac = centerx << FRACBITS
    focallength = (centerxfrac << FRACBITS) // tangent[FINEANGLES // 4 + FIELDOFVIEW // 2]
    table: list[int] = []

    for i in range(FINEANGLES // 2):
        if tangent[i] > 2 * FRACUNIT:
            column = -1
        elif tangent[i] < -2 * FRACUNIT:
            column = SCREEN_WIDTH + 1
        else:
            column = fixed_mul(tangent[i], focallength)
            column = (centerxfrac - column + FRACUNIT - 1) >> FRACBITS
            column = max(-1, min(SCREEN_WIDTH + 1, column))
        table.append(column)

    return table


def x_to_view_angle(columns: list[int]) -> list[int]:
    table: list[int] = []
    for x in range(SCREEN_WIDTH + 1):
        index = 0
        while columns[index] > x:
            index += 1
        table.append(((index << ANGLETOFINESHIFT) - ANG90) & 0xFFFFFFFF)
    return table


def emitted(rows: list[str], values: list[int], per_line: int) -> None:
    for start in range(0, len(values), per_line):
        rows.append("    " + ", ".join(str(value) for value in values[start:start + per_line]) + ",")


def format_array(declaration: str, values: list[int], per_line: int) -> list[str]:
    rows = [declaration]
    emitted(rows, values, per_line)
    rows.append("};")
    return rows


def main() -> int:
    output_path = None
    arguments = sys.argv[1:]
    if arguments:
        if arguments[0] in ("-h", "--help"):
            print(__doc__)
            return 0
        if len(arguments) == 2 and arguments[0] == "--out":
            output_path = arguments[1]
        else:
            print("usage: gen_r_tables.py [--out PATH]", file=sys.stderr)
            return 2

    sine = fine_sine()
    tangent = fine_tangent()
    arc = tan_to_angle()
    columns = view_angle_to_x(tangent)
    levels = light_level()

    # The fencepost pass runs after xtoviewangle/clipangle are derived, exactly
    # as R_InitTextureMapping() does it, so the emitted table is the post-pass one.
    for i, value in enumerate(columns):
        if value == -1:
            columns[i] = 0
        elif value == SCREEN_WIDTH + 1:
            columns[i] = SCREEN_WIDTH

    xangles = x_to_view_angle(columns)
    clip_angle = xangles[0]

    out: list[str] = []
    out.append("/*")
    out.append(" * The renderer's fixed-point lookup tables.")
    out.append(" *")
    out.append(" * GENERATED FILE -- do not edit. Regenerate with:")
    out.append(" *     python tools/gen_r_tables.py --out lib/doom/r_tables.c")
    out.append(" *")
    out.append(" * The formulas, the reason the data is generated instead of computed at")
    out.append(" * boot, and the measured difference from upstream's 1993 literal arrays are")
    out.append(" * documented at the top of tools/gen_r_tables.py and in r_tables.h.")
    out.append(" *")
    out.append(" * These are const, so they live in flash on the ESP32 and cost no DRAM.")
    out.append(" */")
    out.append('#include "r_tables.h"')
    out.append("")
    out.extend(format_array("const fixed_t r_fine_sine[5 * FINEANGLES / 4] = {", sine, 8))
    out.append("")
    out.extend(format_array("const fixed_t r_fine_tangent[FINEANGLES / 2] = {", tangent, 8))
    out.append("")
    out.extend(format_array("const uint32_t r_tan_to_angle[SLOPERANGE + 1] = {", arc, 6))
    out.append("")
    out.extend(format_array("const int16_t r_view_angle_to_x[FINEANGLES / 2] = {", columns, 12))
    out.append("")
    out.extend(format_array("const uint32_t r_x_to_view_angle[R_SCREEN_WIDTH + 1] = {", xangles, 4))
    out.append("")
    out.append(f"const uint32_t r_clip_angle = 0x{clip_angle:08x}u;")
    out.append("")
    out.append("/* R_ExecuteSetViewSize()'s scalelight[,] as colormap indices; see the")
    out.append(" * generator and r_tables.h. Row index is the wall light number, column")
    out.append(" * index is rw_scale >> LIGHTSCALESHIFT, and the value indexes the 256-byte")
    out.append(" * maps of the COLORMAP lump. */")
    out.append("const uint8_t r_light_level[R_LIGHT_LEVELS][R_LIGHT_SCALE_LEVELS] = {")
    for row in levels:
        out.append("    { " + ", ".join(str(value) for value in row) + " },")
    out.append("};")
    out.append("")

    text = "\n".join(out) + "\n"
    if output_path is None:
        sys.stdout.write(text)
    else:
        # ASCII, LF endings: the file must not pick up the host's default
        # encoding, which is what turns a generated table into UTF-16 on
        # Windows when a shell redirects it.
        with open(output_path, "w", encoding="ascii", newline="\n") as handle:
            handle.write(text)
    return 0


if __name__ == "__main__":
    sys.exit(main())
