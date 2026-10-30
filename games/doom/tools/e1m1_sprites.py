#!/usr/bin/env python3
"""The E1M1 sprite fixture: thing type -> sprite lump, and nothing else.

WHY THIS FILE EXISTS

D3.4 renders a *caller-supplied* sprite list. The renderer must not know about
mobjs, states or info.c, so the fixture that produces that list lives here, in
tools/, and never enters lib/doom/. Two consumers share it:

  * tools/gen_e1m1_sprites.py writes tools/e1m1_sprites.inc, the C table the
    real-WAD harness (tools/rsegs_check.c) compiles in;
  * tools/doom_ref.py imports this module and builds the same list from the WAD
    it has already read, independently of the .inc.

WHAT IS TRANSCRIBED, AND FROM WHERE

``THING_SPRITES`` is upstream's ``mobjinfo[]`` (linuxdoom-1.10/info.c) reduced
to three things per DoomEd number: the spawnstate, the state's sprite, and the
state's frame. The state is the *spawn* state, because there is no state machine
here: the first tic of every thing is its spawnstate, and a fixture that showed
any other frame would not be the thing the map placed. For a monster or an item
that is frame 0; for a corpse (DoomEd 10, 12, 15) it is the death frame the map
author chose, which is exactly what the map shows at rest. No animation, no
tics, no AI, no pickups: all of that is Phase 4.

The sprite and frame come from the state table, not from the doomednum:

    frame = states[mobjinfo[type].spawnstate].frame
    sprite = sprnames[states[...].sprite]
    fullbright = (frame & FF_FULLBRIGHT) != 0
    frame_index = frame & FF_FRAMEMASK   (the letter index, 0..28)

DoomEd numbers 1..4 and 11 are player/deathmatch starts. They are not mobjs --
P_SpawnMapThing() skips them -- so they are not in the table and the fixture
does not render them.

ROTATION, EXACTLY R_ProjectSprite()'s

    ang = R_PointToAngle(thing->x, thing->y)          # from the viewer
    rot = (ang - thing->angle + (ANG45/2)*9) >> 29    # 0..7

and the lump is ``sprframe->lump[rot]`` with ``sprframe->flip[rot]``. The sprite
table that resolves those is built by R_InitSpriteDefs()'s rule: scan the lumps
between S_START and S_END, a six-character name ``XXXXF R`` installs one
rotation, and an eight-character name ``XXXXF R F2 R2`` installs its own frame
and also the frame/rotation in bytes 6..7 *of the same lump*, mirrored. A
frame with a ``...F0`` lump has one image for every view (rotate = false).

The rotation character is 1..8 on disk, 0..7 in the table. The sprite table is
per sprite name and frame; ``build_sprite_tables`` reproduces the install rules
and validates that every rotation a selected frame needs exists.

WHAT THIS MODULE NEVER DOES

It never reads lib/doom/, never writes a map coordinate into the .inc, and
never carries texture, flat or sprite pixels. The .inc holds DoomEd numbers,
sprite names, frame indices and rotation lump *names* -- fixture data, not game
data.

Standard library only.
"""

from __future__ import annotations

from dataclasses import dataclass
from typing import Iterable, Sequence

# --- The transcribed table ----------------------------------------------------

#: ANG45 in binary angle measure; the THINGS angle field is in degrees and
#: P_SpawnMapThing() converts with `ANG45 * (angle / 45)`.
ANG45 = 0x20000000
ANG90 = 0x40000000
ANG180 = 0x80000000
ANG270 = 0xC0000000
ANGLETOFINESHIFT = 19
FRACBITS = 16

#: FF_FULLBRIGHT / FF_FRAMEMASK from doomdef.h.
FF_FULLBRIGHT = 0x8000
FF_FRAMEMASK = 0x7FFF

#: sprnames[] order from info.c (SPR_TROO = 0).
SPRITE_NAMES: tuple[str, ...] = (
    "TROO", "SHTG", "PUNG", "PISG", "PISF", "SHTF", "SHT2", "CHGG", "CHGF",
    "MISG", "MISF", "SAWG", "PLSG", "PLSF", "BFGG", "BFGF", "BLUD", "PUFF",
    "BAL1", "BAL2", "PLSS", "PLSE", "MISL", "BFS1", "BFE1", "BFE2", "TFOG",
    "IFOG", "PLAY", "POSS", "SPOS", "VILE", "FIRE", "FATB", "FBXP", "SKEL",
    "MANF", "FATT", "CPOS", "SARG", "HEAD", "BAL7", "BOSS", "BOS2", "SKUL",
    "SPID", "BSPI", "APLS", "APBX", "CYBR", "PAIN", "SSWV", "KEEN", "BBRN",
    "BOSF", "ARM1", "ARM2", "BAR1", "BEXP", "FCAN", "BON1", "BON2", "BKEY",
    "RKEY", "YKEY", "BSKU", "RSKU", "YSKU", "STIM", "MEDI", "SOUL", "PINV",
    "PSTR", "PINS", "MEGA", "SUIT", "PMAP", "PVIS", "CLIP", "AMMO", "ROCK",
    "BROK", "CELL", "CELP", "SHEL", "SBOX", "BPAK", "BFUG", "MGUN", "CSAW",
    "LAUN", "PLAS", "SHOT", "SGN2", "COLU", "SMT2", "GOR1", "POL2", "POL5",
    "POL4", "POL3", "POL1", "POL6", "GOR2", "GOR3", "GOR4", "GOR5", "SMIT",
    "COL1", "COL2", "COL3", "COL4", "CAND", "CBRA", "COL6", "TRE1", "TRE2",
    "ELEC", "CEYE", "FSKU", "COL5", "TBLU", "TGRN", "TRED", "SMBT", "SMGT",
    "SMRT", "HDB1", "HDB2", "HDB3", "HDB4", "HDB5", "HDB6", "POB1", "POB2",
    "BRS1", "TLMP", "TLP2",
)


@dataclass(frozen=True)
class ThingSprite:
    """One row of the transcription.

    ``frame`` is the spawnstate's letter index (0..28, FF_FULLBRIGHT already
    removed); ``fullbright`` is that flag. ``mobjtype`` and ``state`` are kept
    so the .inc and the report can name where the row came from.
    """

    doomednum: int
    mobjtype: str
    state: str
    sprite: str
    frame: int
    fullbright: bool


#: The mobjinfo[] rows for every type E1M1 actually contains, in DoomEd order.
#: Source: linuxdoom-1.10/info.c, mobjinfo[] (doomednum, spawnstate) and the
#: states[] entry for that spawnstate (sprite, frame).
THING_SPRITES: tuple[ThingSprite, ...] = (
    ThingSprite(9, "MT_SHOTGUY", "S_SPOS_STND", "SPOS", 0, False),
    ThingSprite(10, "MT_MISC68", "S_PLAY_XDIE9", "PLAY", 22, False),
    ThingSprite(12, "MT_MISC69", "S_PLAY_XDIE9", "PLAY", 22, False),
    ThingSprite(15, "MT_MISC62", "S_PLAY_DIE7", "PLAY", 13, False),
    ThingSprite(24, "MT_MISC71", "S_GIBS", "POL5", 0, False),
    ThingSprite(35, "MT_MISC50", "S_CANDELABRA", "CBRA", 0, True),
    ThingSprite(48, "MT_MISC48", "S_TECHPILLAR", "ELEC", 0, False),
    ThingSprite(2001, "MT_SHOTGUN", "S_SHOT", "SHOT", 0, False),
    ThingSprite(2002, "MT_CHAINGUN", "S_MGUN", "MGUN", 0, False),
    ThingSprite(2003, "MT_MISC27", "S_LAUN", "LAUN", 0, False),
    ThingSprite(2007, "MT_CLIP", "S_CLIP", "CLIP", 0, False),
    ThingSprite(2008, "MT_MISC22", "S_SHEL", "SHEL", 0, False),
    ThingSprite(2011, "MT_MISC10", "S_STIM", "STIM", 0, False),
    ThingSprite(2012, "MT_MISC11", "S_MEDI", "MEDI", 0, False),
    ThingSprite(2014, "MT_MISC2", "S_BON1", "BON1", 0, False),
    ThingSprite(2015, "MT_MISC3", "S_BON2", "BON2", 0, False),
    ThingSprite(2018, "MT_MISC0", "S_ARM1", "ARM1", 0, False),
    ThingSprite(2019, "MT_MISC1", "S_ARM2", "ARM2", 0, False),
    ThingSprite(2028, "MT_MISC31", "S_COLU", "COLU", 0, True),
    ThingSprite(2035, "MT_BARREL", "S_BAR1", "BAR1", 0, False),
    ThingSprite(2046, "MT_MISC19", "S_BROK", "BROK", 0, False),
    ThingSprite(2048, "MT_MISC17", "S_AMMO", "AMMO", 0, False),
    ThingSprite(2049, "MT_MISC23", "S_SBOX", "SBOX", 0, False),
    ThingSprite(3001, "MT_TROOP", "S_TROO_STND", "TROO", 0, False),
    ThingSprite(3004, "MT_POSSESSED", "S_POSS_STND", "POSS", 0, False),
)

#: DoomEd number -> row.
RULE_BY_TYPE: dict[int, ThingSprite] = {row.doomednum: row for row in THING_SPRITES}


def rule_for(doomednum: int) -> ThingSprite | None:
    """The fixture row for a THINGS type, or None when the type is not a mobj."""
    return RULE_BY_TYPE.get(doomednum)


# --- R_InitSpriteDefs()'s table ---------------------------------------------


@dataclass
class FrameTable:
    """One sprite frame: either one image for all views or eight rotations."""

    rotate: bool
    lumps: list[str]
    flips: list[bool]


def _install_frame(frame: FrameTable, rotation: int, lump: str, flip: bool,
                   sprite: str, letter: int) -> None:
    """Installs one rotation, allowing the two legal rot=0/rotated shapes.

    Upstream's R_InstallSpriteLump() requires a frame to be *either* one rot=0
    lump (used for all eight views) *or* eight rotated lumps. A frame that has a
    rot=0 lump and then rotated ones is the error `R_InstallSpriteLump` reports.
    """
    if rotation == 0:
        if any(frame.lumps):
            raise ValueError(f"{sprite} frame {chr(65 + letter)}: "
                             "multip rot=0 lump")
        for index in range(8):
            frame.lumps[index] = lump
            frame.flips[index] = flip
        frame.rotate = False
        return

    if not frame.rotate and any(frame.lumps):
        raise ValueError(f"{sprite} frame {chr(65 + letter)}: "
                         "rotations and a rot=0 lump")
    frame.rotate = True
    index = rotation - 1
    if frame.lumps[index]:
        raise ValueError(f"{sprite} frame {chr(65 + letter)}: "
                         f"rotation {rotation} has two lumps")
    frame.lumps[index] = lump
    frame.flips[index] = flip


def build_sprite_tables(names: Iterable[str]) -> dict[str, dict[int, FrameTable]]:
    """R_InitSpriteDefs() over one WAD's lump names.

    ``names`` is every 8-byte lump name, in directory order. The scan is the
    S_START..S_END range only, exactly as upstream's firstspritelump /
    lastspritelump bracket it. The result is ``sprite -> frame -> FrameTable``;
    frames are letter indices (0..28).
    """
    ordered = list(names)
    try:
        start = ordered.index("S_START")
        end = ordered.index("S_END", start + 1)
    except ValueError as error:
        raise ValueError("this WAD has no S_START/S_END block") from error
    if end <= start:
        raise ValueError("S_END precedes S_START")

    tables: dict[str, dict[int, FrameTable]] = {}

    for name in ordered[start + 1:end]:
        field = name.ljust(8, "\0")
        if len(field) < 6 or field[4] < "A" or field[4] > "Z":
            continue
        sprite = field[:4]
        frame_index = ord(field[4]) - ord("A")
        if frame_index < 0 or frame_index > 28:
            continue
        if not field[5].isdigit():
            continue
        rotation = int(field[5])

        frames = tables.setdefault(sprite, {})
        frame = frames.get(frame_index)
        if frame is None:
            frame = FrameTable(False, [""] * 8, [False] * 8)
            frames[frame_index] = frame
        _install_frame(frame, rotation, name, False, sprite, frame_index)

        if len(field) > 6 and field[6] != "\0":
            if field[6] < "A" or field[6] > "Z" or not field[7].isdigit():
                continue
            flipped_frame = ord(field[6]) - ord("A")
            flipped_rotation = int(field[7])
            flipped_frames = tables.setdefault(sprite, {})
            entry = flipped_frames.get(flipped_frame)
            if entry is None:
                entry = FrameTable(False, [""] * 8, [False] * 8)
                flipped_frames[flipped_frame] = entry
            _install_frame(entry, flipped_rotation, name, True, sprite,
                           flipped_frame)

    return tables


def select_sprite(tables: dict[str, dict[int, FrameTable]], sprite: str,
                  frame: int, rotation: int) -> tuple[str, bool]:
    """The lump and flip for one view, or ValueError when the frame is short.

    ``rotation`` is 0..7 (the value R_ProjectSprite's shift produces).
    """
    frames = tables.get(sprite)
    if frames is None:
        raise ValueError(f"sprite {sprite!r} has no lumps")
    entry = frames.get(frame)
    if entry is None:
        raise ValueError(f"sprite {sprite!r} frame {chr(65 + frame)} has no lump")
    index = rotation if entry.rotate else 0
    lump = entry.lumps[index]
    if not lump:
        raise ValueError(f"sprite {sprite!r} frame {chr(65 + frame)} "
                         f"rotation {index} is missing")
    return lump, entry.flips[index]


# --- R_PointToAngle(), for the rotation -------------------------------------


def _tan_to_angle_table() -> tuple[int, ...]:
    import math
    import struct

    def single(value: float) -> float:
        return struct.unpack("f", struct.pack("f", value))[0]

    return tuple(
        int(0x100000000 * (math.atan(single(i / 2048)) / (3.141592657 * 2)))
        & 0xFFFFFFFF
        for i in range(2049)
    )


TAN_TO_ANGLE = _tan_to_angle_table()


def _slope_div(num: int, den: int) -> int:
    if den < 512:
        return 2048
    answer = (num << 3) // (den >> 8)
    return answer if answer <= 2048 else 2048


def point_to_angle(viewx: int, viewy: int, x: int, y: int) -> int:
    """R_PointToAngle() (r_main.c), the octant rule, in Python integers."""
    x -= viewx
    y -= viewy
    if x == 0 and y == 0:
        return 0
    if x >= 0:
        if y >= 0:
            if x > y:
                return TAN_TO_ANGLE[_slope_div(y, x)]
            return (ANG90 - 1 - TAN_TO_ANGLE[_slope_div(x, y)]) & 0xFFFFFFFF
        y = -y
        if x > y:
            return (-TAN_TO_ANGLE[_slope_div(y, x)]) & 0xFFFFFFFF
        return (ANG270 + TAN_TO_ANGLE[_slope_div(x, y)]) & 0xFFFFFFFF
    x = -x
    if y >= 0:
        if x > y:
            return (ANG180 - 1 - TAN_TO_ANGLE[_slope_div(y, x)]) & 0xFFFFFFFF
        return (ANG90 + TAN_TO_ANGLE[_slope_div(x, y)]) & 0xFFFFFFFF
    y = -y
    if x > y:
        return (ANG180 + TAN_TO_ANGLE[_slope_div(y, x)]) & 0xFFFFFFFF
    return (ANG270 - 1 - TAN_TO_ANGLE[_slope_div(x, y)]) & 0xFFFFFFFF


def rotation_for(viewx: int, viewy: int, thingx: int, thingy: int,
                 thing_angle: int) -> int:
    """R_ProjectSprite's `(ang - thing->angle + (ANG45/2)*9) >> 29`."""
    ang = point_to_angle(viewx, viewy, thingx, thingy)
    return (((ang - thing_angle + (ANG45 // 2) * 9) & 0xFFFFFFFF) >> 29)


def thing_angle(degrees: int) -> int:
    """P_SpawnMapThing's `mobj->angle = ANG45 * (mthing->angle/45)`."""
    return (ANG45 * (degrees // 45)) & 0xFFFFFFFF


# --- The C header -----------------------------------------------------------


def emit_inc(tables: dict[str, dict[int, FrameTable]],
             rows: Sequence[ThingSprite]) -> str:
    """The tools/e1m1_sprites.inc text for @p rows, as a C fragment.

    One rule per DoomEd number: the sprite name, the frame letter index, the
    fullbright flag and the eight (or one) rotation lumps with their flip flags.
    A non-rotating frame stores only entry 0 and sets ``rotations = 0``.
    """
    lines: list[str] = []
    lines.append("/*")
    lines.append(" * The E1M1 sprite fixture: DoomEd number -> sprite lump.")
    lines.append(" *")
    lines.append(" * GENERATED FILE -- do not edit. Regenerate with:")
    lines.append(" *     python tools/gen_e1m1_sprites.py --wad <path-to-DOOM1.WAD>")
    lines.append(" *")
    lines.append(" * Fixture data, not renderer code: it is compiled into the host")
    lines.append(" * harness (tools/rsegs_check.c) and is never included by lib/doom/.")
    lines.append(" * The transcription, the spawnstate rule and the rotation rule are")
    lines.append(" * documented in tools/e1m1_sprites.py.")
    lines.append(" */")
    lines.append("#ifndef DOOM_E1M1_SPRITES_INC")
    lines.append("#define DOOM_E1M1_SPRITES_INC")
    lines.append("")
    lines.append("typedef struct e1m1_sprite_lump {")
    lines.append("    char name[9];")
    lines.append("    unsigned char flip;")
    lines.append("} e1m1_sprite_lump_t;")
    lines.append("")
    lines.append("typedef struct e1m1_sprite_rule {")
    lines.append("    short type;")
    lines.append("    signed char frame;")
    lines.append("    unsigned char fullbright;")
    lines.append("    unsigned char rotations;")
    lines.append("    char sprite[5];")
    lines.append("    e1m1_sprite_lump_t lumps[8];")
    lines.append("} e1m1_sprite_rule_t;")
    lines.append("")
    lines.append("static const e1m1_sprite_rule_t kE1M1Sprites[] = {")
    for row in sorted(rows, key=lambda item: item.doomednum):
        entry = tables[row.sprite][row.frame]
        lines.append(f"    {{ {row.doomednum}, {row.frame}, "
                     f"{1 if row.fullbright else 0}, "
                     f"{1 if entry.rotate else 0}, \"{row.sprite}\", {{")
        if entry.rotate:
            for lump, flip in zip(entry.lumps, entry.flips):
                lines.append(f"        {{ \"{lump}\", {1 if flip else 0} }},")
        else:
            lines.append(f"        {{ \"{entry.lumps[0]}\", "
                         f"{1 if entry.flips[0] else 0} }},")
        lines.append("    } },")
    lines.append("};")
    lines.append("")
    lines.append("#define E1M1_SPRITE_RULE_COUNT \\")
    lines.append("    ((int)(sizeof kE1M1Sprites / sizeof kE1M1Sprites[0]))")
    lines.append("")
    lines.append("#endif /* DOOM_E1M1_SPRITES_INC */")
    return "\n".join(lines) + "\n"
