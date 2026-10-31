#!/usr/bin/env python3
"""Generate lib/doom/info_tables.h from upstream DOOM's info tables (D4.1a).

WHY THIS IS GENERATED

``p_mobj.c`` needs the 138 ``mobjinfo`` records and the 1,143 ``states``
records of DOOM 1993 verbatim. That is about 3,300 individual field values, and
one wrong value is the kind of defect no hand-written test catches: a monster
with the wrong ``height`` spawns in the ceiling, a state with the wrong
``nextstate`` loops forever, a wrong ``flags`` word makes a corpse count as a
kill. Transcribing by hand also means the port can never be regenerated from a
different upstream revision without repeating the whole exercise.

So this tool parses the upstream sources and writes ``info_tables.h``. The
header is committed -- the firmware build never runs Python -- and this tool is
run by a human when the tables must change.

WHAT IT READS, AND WHY EACH FILE

    info.h     the three enums: ``spritenum_t``, ``statenum_t``,
               ``mobjtype_t``. They are the indices; the tables below are only
               correct if index ``i`` of ``states[]`` is ``S_`` name ``i``.
    info.c     ``sprnames[]``, ``states[]`` and ``mobjinfo[]`` themselves.
    sounds.h   ``sfxenum_t``: the ``sfx_*`` values a ``mobjinfo`` field names.
    p_mobj.h   the ``MF_*`` flag enum. The ``flags`` column of ``mobjinfo`` is
               written as ``MF_A|MF_B`` and this enum is what those names mean.

All four are fetched from the pinned upstream commit recorded in
``PINNED_COMMIT``; ``--sources DIR`` reads copies from a directory instead (for
an offline run), and the commit hash is still written into the output.

FAIL LOUDLY, NEVER SKIP

Every parse step either produces a value or raises. Specifically:

  * an unknown identifier in a value expression (a name in no enum, not
    ``FRACUNIT``) is an error, not a zero;
  * a ``states`` entry whose trailing ``// S_NAME`` comment disagrees with its
    position in ``statenum_t`` is an error -- that comment is the only
    independent witness that the records are in enum order;
  * a ``mobjinfo`` field whose trailing comment is not the field that position
    means is an error, for the same reason;
  * an unknown operator, an unterminated expression, a count that disagrees
    with the enum it indexes, or a ``nextstate`` outside ``statenum_t`` is an
    error.

Silently defaulting any of those would produce a table that compiles, runs, and
is wrong.

USAGE

    python tools/gen_info.py                       # fetch, write the header
    python tools/gen_info.py --sources <dir>       # use local copies
    python tools/gen_info.py --stdout              # print instead of writing
    python tools/gen_info.py --check               # fail if the file drifts

``--check`` regenerates in memory and compares with the committed file, so CI
(or a reviewer) can prove the committed header still matches its source.
"""

from __future__ import annotations

import argparse
import re
import sys
import tempfile
import urllib.request
from pathlib import Path

# The commit this port pins. `linuxdoom-1.10/info.c` has exactly one commit in
# id-Software/DOOM -- the 1997 release import -- so this is also the only
# revision the file has ever had.
PINNED_COMMIT = "4eb368a960647c8cc82d721d0183629ae10759d1"
UPSTREAM_DIR = "linuxdoom-1.10"
UPSTREAM_URL = ("https://raw.githubusercontent.com/id-Software/DOOM/"
                + PINNED_COMMIT + "/" + UPSTREAM_DIR + "/")
SOURCE_FILES = ("info.h", "info.c", "sounds.h", "p_mobj.h")

# The `mobjinfo_t` field order, which is also the order of the `// field`
# comments upstream writes after every value. A value whose comment names a
# different field means the parser lost its place.
MOBJINFO_FIELDS = (
    "doomednum", "spawnstate", "spawnhealth", "seestate", "seesound",
    "reactiontime", "attacksound", "painstate", "painchance", "painsound",
    "meleestate", "missilestate", "deathstate", "xdeathstate", "deathsound",
    "speed", "radius", "height", "mass", "damage", "activesound", "flags",
    "raisestate",
)

DEFAULT_OUTPUT = Path(__file__).resolve().parent.parent / "lib" / "doom" / "info_tables.h"


class GeneratorError(Exception):
    """A source file did not look like linuxdoom-1.10, or an input is unusable."""


# --- Source loading ---------------------------------------------------------

def load_sources(sources: Path | None) -> dict[str, str]:
    """Returns the four upstream files as text, from ``sources`` or the web."""
    texts: dict[str, str] = {}
    if sources is not None:
        for name in SOURCE_FILES:
            path = sources / name
            if not path.is_file():
                raise GeneratorError(f"{path} is not a file; --sources needs all of "
                                     + ", ".join(SOURCE_FILES))
            texts[name] = path.read_text(encoding="latin-1")
        return texts

    with tempfile.TemporaryDirectory(prefix="gen_info_") as directory:
        for name in SOURCE_FILES:
            target = Path(directory) / name
            try:
                with urllib.request.urlopen(UPSTREAM_URL + name, timeout=60) as response:
                    target.write_bytes(response.read())
            except OSError as error:
                raise GeneratorError(
                    f"cannot fetch {UPSTREAM_URL + name}: {error}; pass --sources "
                    "with copies of " + ", ".join(SOURCE_FILES)) from error
            texts[name] = target.read_text(encoding="latin-1")
    return texts


# --- Small parsers ----------------------------------------------------------

def strip_comments(text: str) -> str:
    """Removes // comments and /* */ comments from a C fragment."""
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.S)
    return re.sub(r"//[^\n]*", "", text)


def parse_enum(text: str, type_name: str, file_name: str) -> list[str]:
    """Returns the enumerator names of ``typedef enum {...} type_name;``."""
    pattern = re.compile(r"typedef\s+enum\s*\{([^{}]*)\}\s*" + re.escape(type_name)
                         + r"\s*;")
    match = pattern.search(text)
    if match is None:
        raise GeneratorError(f"{file_name}: no typedef enum ... {type_name};")

    body = strip_comments(match.group(1))
    names: list[str] = []
    for token in body.split(","):
        token = token.strip()
        if not token:
            continue
        if re.fullmatch(r"[A-Za-z_]\w*", token) is None:
            raise GeneratorError(f"{file_name}: {type_name} enumerator {token!r} is "
                                 "not a plain name; the parser cannot assign it a value")
        names.append(token)
    # Upstream's enums end with a count sentinel (NUMSPRITES, NUMSTATES,
    # NUMMOBJTYPES). It is not a value; the array length is the same number.
    if names and re.fullmatch(r"NUM[A-Z_]*", names[-1]):
        names.pop()
    if not names:
        raise GeneratorError(f"{file_name}: {type_name} is empty")
    return names


def parse_flag_enum(text: str) -> list[tuple[str, int]]:
    """Returns the ``MF_*`` names and values, in source order."""
    block = re.search(r"typedef\s+enum\s*\{(.*?)\}\s*mobjflag_t\s*;", text, re.S)
    if block is None:
        raise GeneratorError("p_mobj.h: no typedef enum ... mobjflag_t;")

    flags: list[tuple[str, int]] = []
    for line in strip_comments(block.group(1)).splitlines():
        line = line.strip().rstrip(",").strip()
        if not line:
            continue
        match = re.fullmatch(r"(MF_\w+)\s*=\s*(0x[0-9A-Fa-f]+|\d+)", line)
        if match is None:
            raise GeneratorError(f"p_mobj.h: cannot read flag line {line!r}; every "
                                 "MF_* entry must be NAME = VALUE")
        flags.append((match.group(1), int(match.group(2), 0)))
    if not flags:
        raise GeneratorError("p_mobj.h: mobjflag_t is empty")
    return flags


def parse_sprnames(text: str) -> list[str]:
    """Returns the strings of ``char *sprnames[NUMSPRITES] = {...};``."""
    match = re.search(r"sprnames\s*\[[^\]]*\]\s*=\s*\{(.*?)\};", text, re.S)
    if match is None:
        raise GeneratorError("info.c: no sprnames[] initialiser")
    names = re.findall(r'"([^"]*)"', match.group(1))
    if not names:
        raise GeneratorError("info.c: sprnames[] has no string")
    for name in names:
        if len(name) > 4 or not name.isascii() or not name.isupper():
            raise GeneratorError(f"info.c: sprnames entry {name!r} is not an "
                                 "upper-case name of at most four characters")
    return names


# --- Value expressions ------------------------------------------------------
#
# A table value is one of `-1`, `3004`, `16*FRACUNIT`, `MF_A|MF_B|MF_C`. That is
# a tiny C expression language, and it is parsed with C precedence rather than
# `eval()` so an unexpected token is reported by name.

class Expression:
    """C integer expression parser over a symbol table."""

    _TOKEN = re.compile(r"\s*(0x[0-9A-Fa-f]+|\d+|[A-Za-z_]\w*|[-+*|&<>()])")

    def __init__(self, symbols: dict[str, int]) -> None:
        self.symbols = symbols
        self.tokens: list[str] = []
        self.position = 0
        self.text = ""

    def evaluate(self, text: str) -> int:
        self.text = text
        self.tokens = self._tokenize(text)
        self.position = 0
        value = self._parse_or()
        if self.position != len(self.tokens):
            raise GeneratorError(f"expression {text!r}: trailing token "
                                 f"{self.tokens[self.position]!r}")
        return value

    def _tokenize(self, text: str) -> list[str]:
        tokens: list[str] = []
        rest = text
        while rest.strip():
            match = self._TOKEN.match(rest)
            if match is None:
                raise GeneratorError(f"expression {text!r}: unexpected text "
                                     f"{rest.strip()[:16]!r}")
            tokens.append(match.group(1))
            rest = rest[match.end():]
        return tokens

    def _peek(self) -> str | None:
        return self.tokens[self.position] if self.position < len(self.tokens) else None

    def _take(self) -> str:
        token = self._peek()
        if token is None:
            raise GeneratorError(f"expression {self.text!r}: ends early")
        self.position += 1
        return token

    def _expect(self, token: str) -> None:
        if self._peek() != token:
            raise GeneratorError(f"expression {self.text!r}: expected {token!r}, saw "
                                 f"{self._peek()!r}")

    def _parse_or(self) -> int:
        value = self._parse_and()
        while self._peek() == "|":
            self._take()
            value |= self._parse_and()
        return value

    def _parse_and(self) -> int:
        value = self._parse_shift()
        while self._peek() == "&":
            self._take()
            value &= self._parse_shift()
        return value

    def _parse_shift(self) -> int:
        value = self._parse_additive()
        while self._peek() in ("<<", ">>"):
            operator = self._take()
            right = self._parse_additive()
            value = value << right if operator == "<<" else value >> right
        return value

    def _parse_additive(self) -> int:
        value = self._parse_multiplicative()
        while self._peek() in ("+", "-"):
            operator = self._take()
            right = self._parse_multiplicative()
            value = value + right if operator == "+" else value - right
        return value

    def _parse_multiplicative(self) -> int:
        value = self._parse_unary()
        while self._peek() == "*":
            self._take()
            value *= self._parse_unary()
        return value

    def _parse_unary(self) -> int:
        if self._peek() == "-":
            self._take()
            return -self._parse_unary()
        return self._parse_primary()

    def _parse_primary(self) -> int:
        token = self._take()
        if token == "(":
            value = self._parse_or()
            self._expect(")")
            return value
        if re.fullmatch(r"0x[0-9A-Fa-f]+", token):
            return int(token, 16)
        if token.isdigit():
            return int(token)
        if token in self.symbols:
            return self.symbols[token]
        raise GeneratorError(f"expression {self.text!r}: unknown identifier "
                             f"{token!r} (it is not in any parsed enum and not "
                             "FRACUNIT)")


# --- info.c: states[] -------------------------------------------------------

def parse_states(text: str, state_names: list[str], sprite_names: list[str],
                 actions: list[str], evaluator: Expression) -> list[dict]:
    """Returns one record per ``states[]`` entry, in enum order."""
    match = re.search(r"state_t\s+states\s*\[\s*NUMSTATES\s*\]\s*=\s*\{(.*?)\n\};",
                      text, re.S)
    if match is None:
        raise GeneratorError("info.c: no states[NUMSTATES] initialiser")

    index_of = {name: number for number, name in enumerate(state_names)}
    sprite_index = {name: number for number, name in enumerate(sprite_names)}

    records: list[dict] = []
    for line_number, raw in enumerate(match.group(1).splitlines(), start=1):
        line = raw.strip()
        if not line:
            continue
        body, separator, comment = line.partition("//")
        body = body.strip().rstrip(",").strip()
        if not body:
            # A comment-only line is fine inside the array.
            continue
        if not (body.startswith("{") and body.endswith("}")):
            raise GeneratorError(f"info.c states line {line_number}: {raw.strip()!r} "
                                 "is not a braced state record")
        fields = [field.strip() for field in body[1:-1].split(",")]
        if len(fields) != 7:
            raise GeneratorError(f"info.c states line {line_number}: {len(fields)} "
                                 f"fields, expected 7 in {raw.strip()!r}")

        name = comment.strip()
        if not name.startswith("S_"):
            raise GeneratorError(f"info.c states line {line_number}: no trailing "
                                 f"// S_NAME comment in {raw.strip()!r}")
        expected = state_names[len(records)] if len(records) < len(state_names) else None
        if name != expected:
            raise GeneratorError(f"info.c states line {line_number}: record {len(records)} "
                                 f"is commented {name} but statenum_t[{len(records)}] is "
                                 f"{expected}; the table is not in enum order")

        sprite = fields[0]
        if sprite not in sprite_index:
            raise GeneratorError(f"info.c states line {line_number}: sprite {sprite!r} "
                                 "is not in spritenum_t")
        if not (fields[3].startswith("{") and fields[3].endswith("}")):
            raise GeneratorError(f"info.c states line {line_number}: action field "
                                 f"{fields[3]!r} is not braced")
        action = fields[3][1:-1].strip()
        if action == "NULL":
            action_name = "ACT_NULL"
        elif action in actions:
            action_name = action
        else:
            raise GeneratorError(f"info.c states line {line_number}: action {action!r} "
                                 "was not declared at the top of info.c")

        next_state = fields[4]
        if next_state not in index_of:
            raise GeneratorError(f"info.c states line {line_number}: nextstate "
                                 f"{next_state!r} is not in statenum_t")

        records.append({
            "name": name,
            "sprite": sprite_index[sprite],
            "frame": evaluator.evaluate(fields[1]),
            "tics": evaluator.evaluate(fields[2]),
            "action": action_name,
            "nextstate": index_of[next_state],
        })

    if len(records) != len(state_names):
        raise GeneratorError(f"info.c: {len(records)} state records for "
                             f"{len(state_names)} statenum_t names")
    return records


def parse_declared_actions(text: str) -> list[str]:
    """Returns the ``A_*`` functions info.c declares before its tables."""
    declared = re.findall(r"^void\s+(A_\w+)\s*\(\s*\)\s*;", text, re.M)
    if not declared:
        raise GeneratorError("info.c: no `void A_...();` action declarations")
    return declared


# --- info.c: mobjinfo[] -----------------------------------------------------

def parse_mobjinfo(text: str, mobj_names: list[str], state_names: list[str],
                   evaluator: Expression) -> list[dict]:
    """Returns one record per ``mobjinfo[]`` entry, in enum order."""
    match = re.search(r"mobjinfo_t\s+mobjinfo\s*\[\s*NUMMOBJTYPES\s*\]\s*=\s*\{(.*)\n\};",
                      text, re.S)
    if match is None:
        raise GeneratorError("info.c: no mobjinfo[NUMMOBJTYPES] initialiser")

    state_index = {name: number for number, name in enumerate(state_names)}
    parts = re.split(r"\{\s*//\s*(MT_\w+)\s*", match.group(1))
    # parts = [leading whitespace, name1, body1, name2, body2, ...]
    if len(parts) < 3:
        raise GeneratorError("info.c: mobjinfo[] has no `{  // MT_NAME` records")

    records: list[dict] = []
    for pair in range(1, len(parts), 2):
        name = parts[pair]
        body = parts[pair + 1]
        expected = mobj_names[len(records)] if len(records) < len(mobj_names) else None
        if name != expected:
            raise GeneratorError(f"info.c: mobjinfo record {len(records)} is "
                                 f"commented {name} but mobjtype_t[{len(records)}] is "
                                 f"{expected}; the table is not in enum order")

        values: list[int] = []
        for raw in body.splitlines():
            line = raw.strip()
            if not line:
                continue
            value_text, separator, comment = line.partition("//")
            value_text = value_text.strip().rstrip(",").strip()
            if not value_text:
                continue
            if not separator:
                if set(value_text) <= set("},"):
                    continue
                raise GeneratorError(f"info.c {name}: value line {raw.strip()!r} has "
                                     "no `// fieldname` comment")
            field = comment.strip()
            if len(values) >= len(MOBJINFO_FIELDS):
                raise GeneratorError(f"info.c {name}: more values than the "
                                     f"{len(MOBJINFO_FIELDS)} documented fields")
            if field != MOBJINFO_FIELDS[len(values)]:
                raise GeneratorError(f"info.c {name}: value {len(values)} is commented "
                                     f"{field} but that position is "
                                     f"{MOBJINFO_FIELDS[len(values)]}")
            values.append(evaluator.evaluate(value_text))

        if len(values) != len(MOBJINFO_FIELDS):
            raise GeneratorError(f"info.c {name}: {len(values)} values, expected "
                                 f"{len(MOBJINFO_FIELDS)}")

        record = dict(zip(MOBJINFO_FIELDS, values))
        for field in ("spawnstate", "seestate", "painstate", "meleestate",
                      "missilestate", "deathstate", "xdeathstate", "raisestate"):
            if not 0 <= record[field] < len(state_names):
                raise GeneratorError(f"info.c {name}: {field} {record[field]} is "
                                     "outside statenum_t")
        records.append(record)

    if len(records) != len(mobj_names):
        raise GeneratorError(f"info.c: {len(records)} mobjinfo records for "
                             f"{len(mobj_names)} mobjtype_t names")
    return records


# --- Emission ---------------------------------------------------------------

def fixed_literal(value: int) -> str:
    """A 16.16 value as a readable expression when it is a whole map unit."""
    if value != 0 and value % 65536 == 0:
        units = value // 65536
        return f"{units} * FRACUNIT" if units != 1 else "FRACUNIT"
    return str(value)


def emit(sprite_names, state_names, mobj_names, flag_names, sprnames, states,
         mobjinfo, action_names) -> str:
    lines: list[str] = []
    add = lines.append

    add("/*")
    add(" * DOOM's state and thing tables, generated from upstream linuxdoom-1.10.")
    add(" *")
    add(" * GENERATED FILE -- DO NOT EDIT. Every byte below is parsed output.")
    add(" *")
    add(" * Regenerate from the pinned upstream commit:")
    add(" *   python tools/gen_info.py")
    add(" * or, offline, from copies of info.h/info.c/sounds.h/p_mobj.h:")
    add(" *   python tools/gen_info.py --sources <dir>")
    add(" * `python tools/gen_info.py --check` fails if this file has drifted from")
    add(" * its source; that is the check a reviewer should run.")
    add(" *")
    add(f" * Upstream: id-Software/DOOM {UPSTREAM_DIR} at commit")
    add(f" *   {PINNED_COMMIT}")
    add(" * (\"The DOOM sources as originally released on December 23, 1997\").")
    add(" *")
    add(" * WHAT IT HOLDS, AND WHY THE SHAPES ARE THE PORT'S OWN")
    add(" *")
    add(f" *   mobjinfo[]   {len(mobjinfo)} records, one per mobjtype_t, in enum")
    add(" *                order. The")
    add(" *                fields are upstream's 23 in upstream's order; the brief")
    add(" *                for D4.1a named a subset, and the rest are what the")
    add(" *                action dispatch of D4.2 reads (painstate, deathstate,")
    add(" *                xdeathstate, reactiontime, the sound numbers), emitted")
    add(" *                now so this file is generated once and not per phase.")
    add(f" *   states[]     {len(states)} records, one per statenum_t, in enum order.")
    add(" *                `action` is an action_t -- an enum, not the function")
    add(" *                pointer upstream stores -- so the table is pure data in")
    add(" *                flash and p_action.c owns the dispatch. `frame` is int32")
    add(" *                because upstream's flash states carry 32768+ (the")
    add(" *                FF_FULLBRIGHT bit), which does not fit a signed 16-bit.")
    add(" *   sprnames[]   the four-character sprite names sprnames[] carries.")
    add(" *   MF_*         the mobj flag values the `flags` column is expressed with.")
    add(" *")
    add(" * ONE TRANSLATION UNIT DEFINES THE TABLES")
    add(" *")
    add(" * The arrays are declared `extern` here and defined only in the file that")
    add(" * defines DOOM_INFO_TABLES_DEFINE before including this header (p_mobj.c)")
    add(" * so a second includer cannot create a second copy in flash.")
    add(" *")
    add(" * This is C99 only: <stdint.h>, no allocation, no globals, no floating")
    add(" * point. The tables are `const` and therefore live in .rodata (flash).")
    add(" */")
    add("#ifndef DOOM_INFO_TABLES_H")
    add("#define DOOM_INFO_TABLES_H")
    add("")
    add("#include <stdint.h>")
    add("")
    add('/* FRACUNIT: the radius and height columns are 16.16. */')
    add('#include "m_fixed.h"')
    add("")
    add("#ifdef __cplusplus")
    add('extern "C" {')
    add("#endif")
    add("")
    add("/* The upstream commit these tables were parsed from. */")
    add(f'#define DOOM_INFO_UPSTREAM_COMMIT "{PINNED_COMMIT}"')
    add("")
    add(f"#define NUM_SPRITES {len(sprite_names)}")
    add(f"#define NUM_STATES {len(state_names)}")
    add(f"#define NUM_MOBJ_TYPES {len(mobj_names)}")
    add("")
    add("/* --- Sprites (upstream spritenum_t) ------------------------------------- */")
    add("")
    add("typedef enum spritenum {")
    for number, name in enumerate(sprite_names):
        add(f"    {name} = {number},")
    add("} spritenum_t;")
    add("")
    add("/* --- States (upstream statenum_t) --------------------------------------- */")
    add("")
    add(f"/* {len(state_names)} names, and the table below is only correct if")
    add(" * index i is name i. */")
    add("typedef enum statenum {")
    for number, name in enumerate(state_names):
        add(f"    {name} = {number},")
    add("} statenum_t;")
    add("")
    add("/* --- Thing types (upstream mobjtype_t) ---------------------------------- */")
    add("")
    add("typedef enum mobjtype {")
    for number, name in enumerate(mobj_names):
        add(f"    {name} = {number},")
    add("} mobjtype_t;")
    add("")
    add("/* --- Actions ------------------------------------------------------------ */")
    add("")
    add("/*")
    add(" * The state action, as an enum instead of upstream's `actionf_t` function")
    add(" * pointer. p_action.c maps each value to its implementation and reports the")
    add(" * ones it does not implement yet through a counter; a function pointer in")
    add(" * the table would make `states[]` a relocated pointer table and would leave")
    add(" * no single place to count what is missing.")
    add(" *")
    add(" * ACT_NULL is upstream's `{NULL}`: no action is called at all, so it is not")
    add(" * an unimplemented action and is never counted as one.")
    add(" */")
    add("typedef enum action {")
    add("    ACT_NULL = 0,")
    for number, name in enumerate(sorted(action_names), start=1):
        add(f"    {name} = {number},")
    add("} action_t;")
    add(f"#define NUM_ACTIONS {len(action_names) + 1}")
    add("")
    add("/* --- Mobj flags (upstream p_mobj.h's mobjflag_t) ------------------------ */")
    add("")
    for name, value in flag_names:
        add(f"#define {name} 0x{value:08X}u")
    add("")
    add("/* --- The tables --------------------------------------------------------- */")
    add("")
    add("/* One mobjinfo record: upstream's 23 fields, in upstream's order. radius,")
    add(" * height and the fixed-point speeds are 16.16; doomednum is the THINGS type")
    add(" * that spawns this mobj. */")
    add("typedef struct doom_mobjinfo {")
    for field in MOBJINFO_FIELDS:
        kind = "uint32_t" if field == "flags" else "int32_t"
        add(f"    {kind} {field};")
    add("} doom_mobjinfo_t;")
    add("")
    add("/* One state record. `action` indexes action_t; `nextstate` and `sprite`")
    add(" * index statenum_t and spritenum_t. `frame` is first so the record packs to")
    add(" * 12 bytes on a 32-bit target instead of 16. */")
    add("typedef struct doom_state {")
    add("    int32_t frame;     /* may carry FF_FULLBRIGHT (0x8000) */")
    add("    int16_t sprite;    /* spritenum_t */")
    add("    int16_t tics;      /* -1 is a permanent state */")
    add("    int16_t nextstate; /* statenum_t */")
    add("    uint8_t action;    /* action_t */")
    add("} doom_state_t;")
    add("")
    add("#ifdef DOOM_INFO_TABLES_DEFINE")
    add("")
    add("const doom_mobjinfo_t mobjinfo[NUM_MOBJ_TYPES] = {")
    for record in mobjinfo:
        values = []
        for field in MOBJINFO_FIELDS:
            value = record[field]
            if field == "flags":
                values.append(f"0x{value:08X}u")
            elif field in ("radius", "height"):
                values.append(fixed_literal(value))
            else:
                values.append(str(value))
        add("    {" + ", ".join(values) + "},")
    add("};")
    add("")
    add("const doom_state_t states[NUM_STATES] = {")
    for record in states:
        add(f"    {{{record['frame']}, {record['sprite']}, {record['tics']}, "
            f"{record['nextstate']}, {record['action']}}},  /* {record['name']} */")
    add("};")
    add("")
    add("const char* const sprnames[NUM_SPRITES] = {")
    for start in range(0, len(sprnames), 8):
        chunk = sprnames[start:start + 8]
        add("    " + ", ".join(f'"{name}"' for name in chunk) + ",")
    add("};")
    add("")
    add("#else  /* DOOM_INFO_TABLES_DEFINE */")
    add("")
    add("extern const doom_mobjinfo_t mobjinfo[NUM_MOBJ_TYPES];")
    add("extern const doom_state_t states[NUM_STATES];")
    add("extern const char* const sprnames[NUM_SPRITES];")
    add("")
    add("#endif /* DOOM_INFO_TABLES_DEFINE */")
    add("")
    add("#ifdef __cplusplus")
    add('} /* extern "C" */')
    add("#endif")
    add("")
    add("#endif /* DOOM_INFO_TABLES_H */")
    return "\n".join(lines) + "\n"


# --- Driver -----------------------------------------------------------------

def generate(sources: Path | None) -> str:
    texts = load_sources(sources)

    sprite_names = parse_enum(texts["info.h"], "spritenum_t", "info.h")
    state_names = parse_enum(texts["info.h"], "statenum_t", "info.h")
    mobj_names = parse_enum(texts["info.h"], "mobjtype_t", "info.h")
    sound_names = parse_enum(texts["sounds.h"], "sfxenum_t", "sounds.h")
    flags = parse_flag_enum(texts["p_mobj.h"])

    symbols = {"FRACUNIT": 65536}
    symbols.update({name: number for number, name in enumerate(sound_names)})
    symbols.update({name: number for number, name in enumerate(state_names)})
    symbols.update({name: value for name, value in flags})
    evaluator = Expression(symbols)

    sprnames = parse_sprnames(texts["info.c"])
    if len(sprnames) != len(sprite_names):
        raise GeneratorError(f"info.c: {len(sprnames)} sprnames for "
                             f"{len(sprite_names)} spritenum_t names")

    declared_actions = parse_declared_actions(texts["info.c"])
    states = parse_states(texts["info.c"], state_names, sprite_names,
                          declared_actions, evaluator)
    mobjinfo = parse_mobjinfo(texts["info.c"], mobj_names, state_names, evaluator)

    used_actions = {record["action"] for record in states} - {"ACT_NULL"}
    unused = used_actions - set(declared_actions)
    if unused:
        raise GeneratorError("info.c: states use actions that were not declared: "
                             + ", ".join(sorted(unused)))
    unreferenced = set(declared_actions) - used_actions
    if unreferenced:
        # Not an error for the table, but it means the parser lost an entry.
        raise GeneratorError("info.c: actions declared but never used by a state "
                             "(the states parser probably lost records): "
                             + ", ".join(sorted(unreferenced)))

    return emit(sprite_names, state_names, mobj_names, flags, sprnames, states,
                mobjinfo, used_actions)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        prog="gen_info.py",
        description="Generate lib/doom/info_tables.h from linuxdoom-1.10's "
                    "info.h/info.c/sounds.h/p_mobj.h at the pinned commit.",
        epilog="The output is a committed generated file: the firmware never runs "
               "this tool, and --check is how drift is caught.\n",
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    parser.add_argument("--sources", type=Path, metavar="DIR",
                        help="directory holding local copies of "
                             + ", ".join(SOURCE_FILES))
    parser.add_argument("--output", type=Path, metavar="PATH", default=DEFAULT_OUTPUT,
                        help=f"where to write (default: {DEFAULT_OUTPUT})")
    parser.add_argument("--stdout", action="store_true",
                        help="print the header instead of writing it")
    parser.add_argument("--check", action="store_true",
                        help="compare the generated header with the committed file "
                             "(exit 1 on drift)")
    arguments = parser.parse_args(argv)

    try:
        text = generate(arguments.sources)
    except GeneratorError as error:
        print(f"gen_info: {error}", file=sys.stderr)
        return 2

    if arguments.stdout:
        sys.stdout.write(text)
        return 0

    output = arguments.output
    if arguments.check:
        try:
            current = output.read_text(encoding="utf-8")
        except OSError as error:
            print(f"gen_info: cannot read {output}: {error}", file=sys.stderr)
            return 2
        if current != text:
            print(f"gen_info: {output} has drifted from upstream; regenerate it",
                  file=sys.stderr)
            return 1
        print(f"gen_info: {output} matches upstream {PINNED_COMMIT}")
        return 0

    output.write_text(text, encoding="utf-8", newline="\n")
    print(f"gen_info: wrote {output} ({len(text)} bytes)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
