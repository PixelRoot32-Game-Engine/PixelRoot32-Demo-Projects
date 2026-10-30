#!/usr/bin/env python3
"""Bake the graphics the ESP32 port reads per pixel, and prove the bake.

WHY THIS FILE EXISTS

D3.5 measured one E1M1 frame at 166,261 us. 114,845 us of it (69.1 %) was
inflate/copy, 13,044 us (7.8 %) was texture composition and 9,816 us (5.9 %)
was container name scans. Composition writes roughly 0.6 MB per frame at DRAM
write speed, so a cache cannot make it free; the only way to stop paying it is
to stop doing it. This tool turns the renderer's per-pixel inputs into raw
payloads a reader can map:

  * wall textures and the sky: width * 128 bytes per texture, column major,
    exactly the layout R_DrawColumn() reads, plus an optional width * 128 mask;
  * flats: the WAD's own 64 x 64 bytes, marked raw (no transform at all);
  * sprite patches: the WAD's own patch lump, marked raw (no composition; the
    post walk is already cheap).

The 128 rows are not a choice. DOOM's R_DrawColumn() wraps a texture row with a
hard-coded `& 127`, so a composite stored for the column drawer is always 128
rows, with the rows above a shorter texture's declared height zeroed.

WHAT "CORRECT" MEANS

The acceptance test is not "it looks right": the C decoder (lib/doom/r_data.c)
must produce exactly these bytes from the SOURCE WAD's own patches. That is why
--check-container compiles test/unit/test_baked/tool/bake_check.c against
lib/doom/r_data.c and lib/doom/whd.c and compares every baked texture byte for
byte, mask included. The container no longer carries the wall patches -- D3.11
drops them after the bake -- so the referee takes the WAD as a second path and
composes from the original data; comparing against the container's own sources
would be the container agreeing with itself. This module contains its own
composer -- an independent implementation, like tools/doom_ref.py -- so a bug
shared with the encoder is still caught by the C check.

THE MANIFEST

The port never searches the container by name at runtime. One raw lump,
``~PWBAKE~``, holds a 16-byte header and 20-byte records:

    header
      0   4   magic "PWBK"
      4   2   version 1
      6   2   record size 20
      8   4   record count
      12  4   reserved, zero

    record
      0   8   name[8], NUL padded
      8   2   width
      10  2   rows (declared height for a wall/sky, 64 for a flat, 0 for a sprite)
      12  1   flags: bit 0 = a mask plane follows the pixels
      13  1   kind: 1 wall, 2 sky, 3 flat, 4 sprite
      14  2   reserved, zero
      16  4   lump index inside this container

The lump name is eight characters and contains ``~``, which no IWAD lump name
uses, so it cannot collide with level, patch, flat or sprite data.

MASK PADDING

A mask plane is baked only when the composite leaves at least one texel
unwritten. The expanded mask sets 1 in rows ``height .. 127``: those rows are
the zero fill the old compose expansion produced and the column drawer treated
as opaque, so a mask of 1 there is what keeps the finished frame byte-identical.

USAGE

    python tools/bake_graphics.py --check-container tools/DOOM1.whd \
        --wad tools/DOOM1.WAD

The encoder calls bake_level() and build_manifest() while packing; this file's
command line is only the independent verifier.

Standard library only.
"""

from __future__ import annotations

import argparse
import re
import shutil
import struct
import subprocess
import sys
import tempfile
from dataclasses import dataclass, field
from pathlib import Path
from typing import Iterable, Sequence

# --- Manifest constants (keep in step with src/doomc/BakedGraphics.h) ------

MANIFEST_NAME = "~PWBAKE~"
MANIFEST_MAGIC = b"PWBK"
MANIFEST_VERSION = 1
MANIFEST_HEADER_SIZE = 16
MANIFEST_RECORD_SIZE = 20

KIND_WALL = 1
KIND_SKY = 2
KIND_FLAT = 3
KIND_SPRITE = 4

FLAG_MASK = 0x01

STORED_ROWS = 128
FLAT_BYTES = 64 * 64

# --- WAD formats (independent of tools/doom_ref.py) -------------------------

PATCH_HEADER_SIZE = 8
PATCH_COLUMN_ENTRY_SIZE = 4
PATCH_POST_HEADER_SIZE = 3
PATCH_POST_TRAILER_SIZE = 1
PATCH_POST_END = 0xFF

TEXTURE_NAME_SIZE = 8
TEXTURE_COUNT_SIZE = 4
TEXTURE_DIRECTORY_ENTRY_SIZE = 4
TEXTURE_HEADER_SIZE = 22
TEXTURE_PATCH_SIZE = 10
PNAMES_HEADER_SIZE = 4

TRANSPARENT = 0
OPAQUE = 1

EXIT_OK = 0
EXIT_MISMATCH = 1
EXIT_USAGE = 2


class BakeError(Exception):
    """The WAD cannot be baked: a lump, patch or texture is missing or malformed."""


@dataclass(frozen=True)
class BakedItem:
    """One manifest record before its container index is assigned.

    ``payload`` is None for a flat or sprite: those are the WAD's own bytes and
    are only marked raw, so the record points at the existing lump. A wall or
    sky carries the dense composite this module produced.
    """

    name: str
    kind: int
    width: int
    rows: int
    flags: int
    payload: bytes | None


@dataclass(frozen=True)
class LevelBake:
    """One level's contribution to a union bake.

    The counts are the level's own distinct names; ``payload_bytes`` is what the
    level would cost in a container baked for it alone: the dense wall/sky
    payloads plus one 4,096-byte flat per distinct flat. A container also pays
    the manifest, which is ``MANIFEST_HEADER_SIZE + MANIFEST_RECORD_SIZE`` per
    item; --stats adds it when it answers "does this level fit alone".
    """

    level: str
    walls: int
    skies: int
    flats: int
    sprites: int
    payload_bytes: int

    def container_bytes(self) -> int:
        """The level's bake as a whole container: payloads plus its manifest."""
        entries = self.walls + self.skies + self.flats + self.sprites
        return self.payload_bytes + MANIFEST_HEADER_SIZE + MANIFEST_RECORD_SIZE * entries


@dataclass
class BakedSet:
    """What one level needs, plus the counts a --stats line reports.

    ``per_level`` is empty for a single-level bake and carries one row per level
    for the union bake (D3.10): it is what lets --stats say whether each level
    would still fit if it were baked alone.
    """

    items: list[BakedItem]
    flat_names: list[str]
    sprite_names: list[str]
    per_level: list["LevelBake"] = field(default_factory=list)

    def count(self, kind: int) -> int:
        return sum(1 for item in self.items if item.kind == kind)

    def payload_bytes(self) -> int:
        return sum(len(item.payload) for item in self.items if item.payload is not None)

    def bytes_by_kind(self, kind: int) -> int:
        return sum(len(item.payload) for item in self.items
                   if item.kind == kind and item.payload is not None)


@dataclass(frozen=True)
class Patch:
    name: str
    width: int
    height: int
    columns: tuple[bytes, ...]


@dataclass(frozen=True)
class Texture:
    name: str
    masked: int
    width: int
    height: int
    patches: tuple[tuple[int, int, int], ...]


# --- Little-endian readers --------------------------------------------------


def _le16(data: bytes, offset: int) -> int:
    return int.from_bytes(data[offset:offset + 2], "little", signed=False)


def _le16s(data: bytes, offset: int) -> int:
    return int.from_bytes(data[offset:offset + 2], "little", signed=True)


def _le32(data: bytes, offset: int) -> int:
    return int.from_bytes(data[offset:offset + 4], "little", signed=True)


def _name(data: bytes, offset: int) -> str:
    return data[offset:offset + 8].split(b"\0", 1)[0].decode("latin-1")


# --- Patch and texture parsing ---------------------------------------------


def parse_patch(name: str, data: bytes) -> Patch:
    """Validates a patch the way r_patch_validate() does and normalises it.

    Every rule the C decoder enforces is restated here: the header, the column
    table, each offset, each post's bytes, the height rule and the terminator.
    """
    if len(data) < PATCH_HEADER_SIZE:
        raise BakeError(f"{name}: {len(data)} bytes is shorter than a patch header")
    width = _le16s(data, 0)
    height = _le16s(data, 2)
    if width < 1:
        raise BakeError(f"{name}: width {width}")
    if height < 1:
        raise BakeError(f"{name}: height {height}")
    table_end = PATCH_HEADER_SIZE + width * PATCH_COLUMN_ENTRY_SIZE
    if table_end > len(data):
        raise BakeError(f"{name}: column table needs {table_end} B of {len(data)}")

    columns: list[bytes] = []
    for column in range(width):
        offset = _le32(data, PATCH_HEADER_SIZE + column * PATCH_COLUMN_ENTRY_SIZE)
        if offset < table_end or offset >= len(data):
            raise BakeError(f"{name}: column {column} offset {offset} is outside "
                            f"{table_end}..{len(data)}")
        columns.append(decode_column(name, column, data, offset, height))
    return Patch(name, width, height, tuple(columns))


def decode_column(name: str, column: int, data: bytes, start: int, height: int) -> bytes:
    """Normalises one post chain into [topdelta][length][0][pixels][0]... 0xFF."""
    out = bytearray()
    at = start
    while True:
        if at >= len(data):
            raise BakeError(f"{name}: column {column} has no terminator")
        top_delta = data[at]
        if top_delta == PATCH_POST_END:
            break
        if at + PATCH_POST_HEADER_SIZE > len(data):
            raise BakeError(f"{name}: column {column} post header past the lump")
        length = data[at + 1]
        if at + PATCH_POST_HEADER_SIZE + length > len(data):
            raise BakeError(f"{name}: column {column} post pixels past the lump")
        if top_delta + length > height:
            raise BakeError(f"{name}: column {column} post reaches row "
                            f"{top_delta + length} of {height}")
        if at + length + PATCH_POST_HEADER_SIZE + PATCH_POST_TRAILER_SIZE >= len(data):
            raise BakeError(f"{name}: column {column} post has no following byte")
        out += bytes((top_delta, length, 0))
        out += data[at + PATCH_POST_HEADER_SIZE:at + PATCH_POST_HEADER_SIZE + length]
        out.append(0)
        at += length + PATCH_POST_HEADER_SIZE + PATCH_POST_TRAILER_SIZE
    out.append(PATCH_POST_END)
    return bytes(out)


def parse_pnames(data: bytes) -> list[str]:
    """PNAMES: int32 count then count 8-byte names, kept in file case."""
    if len(data) < PNAMES_HEADER_SIZE:
        raise BakeError(f"PNAMES: {len(data)} bytes is shorter than its count")
    count = _le32(data, 0)
    if count < 0:
        raise BakeError(f"PNAMES: count {count}")
    if PNAMES_HEADER_SIZE + count * TEXTURE_NAME_SIZE > len(data):
        raise BakeError(f"PNAMES: {count} names do not fit {len(data)} bytes")
    return [data[PNAMES_HEADER_SIZE + index * TEXTURE_NAME_SIZE:
                 PNAMES_HEADER_SIZE + (index + 1) * TEXTURE_NAME_SIZE]
            .rstrip(b"\0").decode("latin-1")
            for index in range(count)]


def parse_textures(lump_name: str, data: bytes) -> list[Texture]:
    """TEXTURE1/TEXTURE2: count, offsets, definitions. Order preserved."""
    if len(data) < TEXTURE_COUNT_SIZE:
        raise BakeError(f"{lump_name}: {len(data)} bytes is shorter than its count")
    count = _le32(data, 0)
    if count < 0:
        raise BakeError(f"{lump_name}: negative texture count {count}")
    directory_end = TEXTURE_COUNT_SIZE + count * TEXTURE_DIRECTORY_ENTRY_SIZE
    if directory_end > len(data):
        raise BakeError(f"{lump_name}: directory needs {directory_end} B of {len(data)}")

    textures: list[Texture] = []
    for index in range(count):
        offset = _le32(data, TEXTURE_COUNT_SIZE + index * 4)
        if offset < directory_end or offset + TEXTURE_HEADER_SIZE > len(data):
            raise BakeError(f"{lump_name}: texture {index} offset {offset} is outside the lump")
        name = _name(data, offset)
        masked = _le32(data, offset + 8)
        width = _le16s(data, offset + 12)
        height = _le16s(data, offset + 14)
        patch_count = _le16s(data, offset + 20)
        if width < 1 or height < 1:
            raise BakeError(f"{lump_name}: texture {name!r} is {width}x{height}")
        if patch_count < 0:
            raise BakeError(f"{lump_name}: texture {name!r} has {patch_count} patches")
        if offset + TEXTURE_HEADER_SIZE + patch_count * TEXTURE_PATCH_SIZE > len(data):
            raise BakeError(f"{lump_name}: texture {name!r} patch list past the lump")
        patches: list[tuple[int, int, int]] = []
        for patch_index in range(patch_count):
            at = offset + TEXTURE_HEADER_SIZE + patch_index * TEXTURE_PATCH_SIZE
            origin_x = _le16s(data, at)
            origin_y = _le16s(data, at + 2)
            index_in_pnames = _le16s(data, at + 4)
            patches.append((origin_x, origin_y, index_in_pnames))
        textures.append(Texture(name, masked, width, height, tuple(patches)))
    return textures


# --- Composition ------------------------------------------------------------


def compose_texture(texture: Texture, resolved: dict[int, Patch]) -> tuple[bytes, bytes]:
    """Blits every patch into one column-major surface plus mask.

    The fill is TRANSPARENT (0) and OPAQUE (1) where a post wrote; later patches
    overwrite earlier ones. The two documented divergences from vanilla are the
    C decoder's and this implementation's: the origin is applied to every
    column, and a post starting above the surface advances its source pointer.
    """
    width, height = texture.width, texture.height
    pixels = bytearray([TRANSPARENT]) * (width * height)
    mask = bytearray([0]) * (width * height)

    for origin_x, origin_y, patch_index in texture.patches:
        patch = resolved[patch_index]
        x_first = max(0, origin_x)
        x_last = min(width, origin_x + patch.width)
        for x in range(x_first, x_last):
            stream = patch.columns[x - origin_x]
            at = 0
            while stream[at] != PATCH_POST_END:
                top_delta = stream[at]
                length = stream[at + 1]
                source = at + PATCH_POST_HEADER_SIZE
                position = origin_y + top_delta
                count = length
                if position < 0:
                    source += -position
                    count += position
                    position = 0
                if position + count > height:
                    count = height - position
                if count > 0:
                    destination = x * height + position
                    pixels[destination:destination + count] = stream[source:source + count]
                    mask[destination:destination + count] = bytes([OPAQUE]) * count
                at += length + PATCH_POST_HEADER_SIZE + PATCH_POST_TRAILER_SIZE

    return bytes(pixels), bytes(mask)


def expand_to_stored(pixels: bytes, mask: bytes, width: int, height: int,
                     has_mask: bool) -> bytes:
    """Expands a width x height composite to the 128-row stored column form.

    Rows above the declared height stay zero, which is what the old in-place
    expansion produced. The mask's rows above the height are set to 1: those
    rows are the opaque zero fill, not a hole, and setting 0 there would change
    a masked mid texture's pixels.
    """
    stored = bytearray(width * STORED_ROWS)
    if has_mask:
        stored_mask = bytearray(width * STORED_ROWS)
    for x in range(width):
        source = x * height
        destination = x * STORED_ROWS
        stored[destination:destination + height] = pixels[source:source + height]
        if has_mask:
            stored_mask[destination:destination + height] = mask[source:source + height]
            stored_mask[destination + height:destination + STORED_ROWS] = \
                bytes([OPAQUE]) * (STORED_ROWS - height)
    return bytes(stored) + (bytes(stored_mask) if has_mask else b"")


def bake_texture(texture: Texture, patches: dict[int, Patch]) -> tuple[bytes, int]:
    """Returns (payload, flags) for one wall or sky texture."""
    pixels, mask = compose_texture(texture, patches)
    has_mask = any(value == 0 for value in mask)
    payload = expand_to_stored(pixels, mask, texture.width, texture.height, has_mask)
    return payload, (FLAG_MASK if has_mask else 0)


# --- Level selection --------------------------------------------------------


def first_index(lumps: Sequence, name: str) -> int:
    upper = name.upper()
    for index, lump in enumerate(lumps):
        if lump.name.upper() == upper:
            return index
    return -1


def level_lumps(lumps: Sequence, level: str) -> list:
    """The ten data lumps after a level marker, in WAD order."""
    marker = first_index(lumps, level)
    if marker < 0:
        raise BakeError(f"--bake: the WAD has no level marker {level!r}")
    names = ("THINGS", "LINEDEFS", "SIDEDEFS", "VERTEXES", "SEGS",
             "SSECTORS", "NODES", "SECTORS", "REJECT", "BLOCKMAP")
    following = [lump.name.upper() for lump in lumps[marker + 1:marker + 1 + len(names)]]
    if marker + len(names) >= len(lumps) or following != list(names):
        raise BakeError(f"--bake: {level!r} is not followed by {', '.join(names)}")
    return list(lumps[marker + 1:marker + 1 + len(names)])


def level_wall_names(sidedefs: bytes) -> list[str]:
    """The three texture fields of every sidedef, in first-seen order."""
    names: list[str] = []
    seen: set[str] = set()
    for offset in range(0, len(sidedefs) - 29, 30):
        for field in (4, 12, 20):
            name = _name(sidedefs, offset + field)
            if not name or name == "-":
                continue
            key = name.upper()
            if key not in seen:
                seen.add(key)
                names.append(key)
    return names


def level_flat_names(sectors: bytes) -> list[str]:
    """The floor and ceiling pictures of every sector, in first-seen order."""
    names: list[str] = []
    seen: set[str] = set()
    for offset in range(0, len(sectors) - 25, 26):
        for field in (4, 12):
            name = _name(sectors, offset + field)
            if not name or name == "-" or name.upper() == "F_SKY1":
                continue
            key = name.upper()
            if key not in seen:
                seen.add(key)
                names.append(key)
    return names


def read_sprite_list(path: Path) -> list[str]:
    """Reads a list of sprite lump names from a fixture.

    Accepts the generated ``.inc`` form (``{ "SPOSA1", 0 }``) or one name per
    line, so the encoder stays independent of how the port's fixture is stored.
    """
    try:
        text = path.read_text(encoding="utf-8")
    except OSError as error:
        raise BakeError(f"--sprite-list: {path}: {error.strerror}") from error
    names: list[str] = []
    seen: set[str] = set()
    for token in re.findall(r'\{\s*"([A-Za-z0-9_\\\-\[\]]{4,8})"\s*,\s*[01]\s*\}', text):
        key = token.upper()
        if key not in seen:
            seen.add(key)
            names.append(key)
    if names:
        return names
    for line in text.splitlines():
        key = line.strip().upper()
        if 4 <= len(key) <= 8 and all(character.isalnum() or character in "_\\-[]" for character in key):
            if key not in seen:
                seen.add(key)
                names.append(key)
    return names


def _texture_definitions(lumps: Sequence) -> dict[str, Texture]:
    definitions: dict[str, Texture] = {}
    for lump_name in ("TEXTURE1", "TEXTURE2"):
        index = first_index(lumps, lump_name)
        if index < 0:
            continue
        for texture in parse_textures(lump_name, lumps[index].data):
            definitions.setdefault(texture.name.upper(), texture)
    return definitions


def _pnames(lumps: Sequence) -> list[str]:
    index = first_index(lumps, "PNAMES")
    if index < 0:
        raise BakeError("--bake: the WAD has no PNAMES lump")
    return parse_pnames(lumps[index].data)


def bake_level(lumps: Sequence, level: str, sky: str = "SKY1",
               sprite_names: Iterable[str] = ()) -> BakedSet:
    """Bakes one level's wall textures, sky, flats and sprite patch list.

    The set is the graphics the lever's own records name: every sidedef texture,
    every sector flat, the sky texture and the sprite lumps the caller lists.
    A name with no definition is an error, not a skipped item: the container is
    built for this port or it is not.
    """
    data = level_lumps(lumps, level)
    sidedefs = data[2].data
    sectors = data[7].data

    definitions = _texture_definitions(lumps)
    pnames = _pnames(lumps)

    wall_names = level_wall_names(sidedefs)
    flat_names = level_flat_names(sectors)

    patch_cache: dict[str, Patch] = {}

    def patch_for(index: int) -> Patch:
        if index < 0 or index >= len(pnames):
            raise BakeError(f"--bake: PNAMES index {index} is outside {len(pnames)}")
        name = pnames[index].upper()
        if name in patch_cache:
            return patch_cache[name]
        lump = first_index(lumps, name)
        if lump < 0:
            raise BakeError(f"--bake: patch {name!r} is not a lump in this WAD")
        patch = parse_patch(name, lumps[lump].data)
        patch_cache[name] = patch
        return patch

    def bake_one(name: str, kind: int) -> BakedItem:
        texture = definitions.get(name)
        if texture is None:
            raise BakeError(f"--bake: texture {name!r} is not in TEXTURE1/TEXTURE2")
        patches = {index: patch_for(index) for _, _, index in texture.patches}
        payload, flags = bake_texture(texture, patches)
        return BakedItem(name, kind, texture.width, texture.height, flags, payload)

    items: list[BakedItem] = []
    seen_wall: set[str] = set()
    for name in wall_names:
        items.append(bake_one(name, KIND_WALL))
        seen_wall.add(name)

    sky_name = sky.upper()
    if sky_name in seen_wall:
        # A texture that is both a wall and the sky needs one payload; the
        # resolver serves both kinds, so the sky kind wins.
        for index, item in enumerate(items):
            if item.name == sky_name:
                items[index] = BakedItem(item.name, KIND_SKY, item.width, item.rows,
                                         item.flags, item.payload)
                break
    else:
        items.append(bake_one(sky_name, KIND_SKY))

    for name in flat_names:
        lump = first_index(lumps, name)
        if lump < 0:
            raise BakeError(f"--bake: flat {name!r} is not a lump in this WAD")
        if len(lumps[lump].data) != FLAT_BYTES:
            raise BakeError(f"--bake: flat {name!r} is {len(lumps[lump].data)} bytes, "
                            f"not {FLAT_BYTES}")
        items.append(BakedItem(name, KIND_FLAT, 64, 64, 0, None))

    kept_sprites: list[str] = []
    seen_sprite: set[str] = set()
    for raw_name in sprite_names:
        name = raw_name.upper()
        if name in seen_sprite:
            continue
        seen_sprite.add(name)
        lump = first_index(lumps, name)
        if lump < 0:
            raise BakeError(f"--bake: sprite lump {name!r} is not in this WAD")
        items.append(BakedItem(name, KIND_SPRITE, 0, 0, 0, None))
        kept_sprites.append(name)

    return BakedSet(items, flat_names, kept_sprites)


def bake_union(lumps: Sequence, levels: Sequence[str], sky: str = "SKY1",
               sprite_names: Iterable[str] = ()) -> BakedSet:
    """Bakes the union of several levels: one payload per distinct name.

    The nine shareware levels share most of their graphics -- 100 distinct wall
    textures for the whole episode against 32 for E1M1 -- so baking each level
    separately would store the shared textures eight more times. This returns
    one item per distinct (kind, name), in first-seen order across @p levels, so
    the result is deterministic and the container holds each texture and flat
    exactly once.

    A texture that is a wall in one level and the sky in another is one item with
    kind SKY: the renderer resolves both through the same wall view, and a
    duplicate name inside that view would make the lookup ambiguous. Every level
    still gets the sky in its own set, so the union always holds it.

    ``per_level`` carries each level's own counts and payload bytes for --stats.
    """
    if not levels:
        raise BakeError("--bake: no level was named")

    items: list[BakedItem] = []
    index_by_key: dict[tuple[str, str], int] = {}
    per_level: list[LevelBake] = []
    sprite_order: list[str] = []
    seen_sprite: set[str] = set()

    def key_of(item: BakedItem) -> tuple[str, str]:
        if item.kind in (KIND_WALL, KIND_SKY):
            return ("texture", item.name.upper())
        if item.kind == KIND_FLAT:
            return ("flat", item.name.upper())
        return ("sprite", item.name.upper())

    for level in levels:
        one = bake_level(lumps, level, sky=sky, sprite_names=sprite_names)
        payload_bytes = sum(len(item.payload) for item in one.items
                            if item.payload is not None)
        payload_bytes += FLAT_BYTES * sum(1 for item in one.items
                                          if item.kind == KIND_FLAT)
        per_level.append(LevelBake(
            level=level.strip().upper(), walls=one.count(KIND_WALL),
            skies=one.count(KIND_SKY), flats=one.count(KIND_FLAT),
            sprites=one.count(KIND_SPRITE), payload_bytes=payload_bytes))

        for item in one.items:
            key = key_of(item)
            at = index_by_key.get(key)
            if at is None:
                index_by_key[key] = len(items)
                items.append(item)
            elif item.kind == KIND_SKY and items[at].kind != KIND_SKY:
                # The same texture as a wall elsewhere: one payload, sky kind.
                items[at] = BakedItem(item.name, KIND_SKY, items[at].width,
                                      items[at].rows, items[at].flags,
                                      items[at].payload)
            if item.kind == KIND_SPRITE:
                sprite = item.name.upper()
                if sprite not in seen_sprite:
                    seen_sprite.add(sprite)
                    sprite_order.append(sprite)

    flat_order = [item.name for item in items if item.kind == KIND_FLAT]
    return BakedSet(items, flat_order, sprite_order, per_level)


# --- Manifest ---------------------------------------------------------------


def build_manifest(records: Sequence[tuple[str, int, int, int, int, int]]) -> bytes:
    """Builds the manifest lump.

    Each tuple is (name, width, rows, flags, kind, lumpIndex). Records are
    written in the order given; the reader sorts its own copy.
    """
    out = bytearray(MANIFEST_MAGIC)
    out += struct.pack("<HHII", MANIFEST_VERSION, MANIFEST_RECORD_SIZE,
                       len(records), 0)
    assert len(out) == MANIFEST_HEADER_SIZE
    for name, width, rows, flags, kind, lump_index in records:
        field = name.encode("latin-1")[:8].ljust(8, b"\0")
        out += field
        out += struct.pack("<HHBBHI", width, rows, flags, kind, 0, lump_index)
    assert (len(out) - MANIFEST_HEADER_SIZE) % MANIFEST_RECORD_SIZE == 0
    return bytes(out)


@dataclass(frozen=True)
class ManifestEntry:
    name: str
    width: int
    rows: int
    flags: int
    kind: int
    lump_index: int


def parse_manifest(payload: bytes) -> list[ManifestEntry]:
    """Parses a manifest the way the C reader does, for the --stats line."""
    if len(payload) < MANIFEST_HEADER_SIZE:
        raise BakeError(f"manifest is {len(payload)} bytes, shorter than its header")
    if payload[0:4] != MANIFEST_MAGIC:
        raise BakeError(f"manifest magic is {payload[0:4]!r}, expected {MANIFEST_MAGIC!r}")
    version, record_size, count, reserved = struct.unpack_from("<HHII", payload, 4)
    if version != MANIFEST_VERSION:
        raise BakeError(f"manifest version {version}, expected {MANIFEST_VERSION}")
    if record_size != MANIFEST_RECORD_SIZE:
        raise BakeError(f"manifest record size {record_size}, expected {MANIFEST_RECORD_SIZE}")
    if reserved != 0:
        raise BakeError("manifest header reserved word is not zero")
    if MANIFEST_HEADER_SIZE + count * record_size > len(payload):
        raise BakeError(f"manifest declares {count} records but holds "
                        f"{len(payload) - MANIFEST_HEADER_SIZE} bytes")
    entries: list[ManifestEntry] = []
    for index in range(count):
        at = MANIFEST_HEADER_SIZE + index * record_size
        name = payload[at:at + 8].split(b"\0", 1)[0].decode("latin-1")
        width, rows, flags, kind, entry_reserved, lump_index = \
            struct.unpack_from("<HHBBHI", payload, at + 8)
        if entry_reserved != 0:
            raise BakeError(f"manifest record {index}: reserved word is not zero")
        entries.append(ManifestEntry(name, width, rows, flags, kind, lump_index))
    return entries


def manifest_line(payload: bytes) -> str:
    """One `baked` line for a --stats block."""
    try:
        entries = parse_manifest(payload)
    except BakeError as error:
        return f"  baked            malformed manifest ({error})"
    walls = sum(1 for entry in entries if entry.kind == KIND_WALL)
    skies = sum(1 for entry in entries if entry.kind == KIND_SKY)
    flats = sum(1 for entry in entries if entry.kind == KIND_FLAT)
    sprites = sum(1 for entry in entries if entry.kind == KIND_SPRITE)
    return (f"  baked            {len(entries):,} entries "
            f"({walls:,} walls, {skies:,} sky, {flats:,} flats, {sprites:,} sprites), "
            f"{len(payload):,} B manifest")


# --- The C verifier ---------------------------------------------------------


def project_root() -> Path:
    """games/doom, the directory that holds lib/, src/ and tools/."""
    return Path(__file__).resolve().parent.parent


def compile_harness(workdir: Path, compiler: str) -> Path:
    """Builds the C decoder harness against lib/doom/r_data.c and whd.c."""
    root = project_root()
    executable = workdir / ("bake_check.exe" if sys.platform == "win32" else "bake_check")
    command = [
        compiler,
        "-std=gnu99", "-O2", "-Wall", "-Wextra",
        "-I", str(root / "lib" / "doom"),
        "-o", str(executable),
        "-x", "c",
        str(root / "test" / "unit" / "test_baked" / "tool" / "bake_check.c.inc"),
        str(root / "lib" / "doom" / "r_data.c"),
        str(root / "lib" / "doom" / "whd.c"),
    ]
    result = subprocess.run(command, capture_output=True, text=True)
    if result.returncode != 0:
        raise RuntimeError("the harness did not compile with the C compiler:\n"
                           + (result.stderr or result.stdout))
    if result.stderr.strip():
        raise RuntimeError("the harness compiled with warnings, which this project does not "
                           "accept:\n" + result.stderr)
    return executable


def check_container(container: Path, wad: Path, compiler: str = "gcc") -> int:
    """Compiles the C verifier, runs it on @p container and @p wad, and reports."""
    workdir = Path(tempfile.mkdtemp(prefix="bake_check_"))
    try:
        executable = compile_harness(workdir, compiler)
        result = subprocess.run([str(executable), str(container), str(wad)],
                                capture_output=True, text=True)
    except RuntimeError as error:
        print(f"bake_graphics: {error}", file=sys.stderr)
        return EXIT_USAGE
    finally:
        shutil.rmtree(workdir, ignore_errors=True)

    if result.returncode not in (EXIT_OK, EXIT_MISMATCH):
        print(result.stderr or result.stdout, file=sys.stderr)
        print("bake_graphics: the C verifier did not run cleanly", file=sys.stderr)
        return EXIT_USAGE

    print(f"bake_graphics: {container}")
    for line in result.stdout.splitlines():
        if line.startswith("BAKECHECK") or line.startswith("MISMATCH"):
            print(f"  {line}")
    if not any(line.startswith("BAKECHECK") for line in result.stdout.splitlines()):
        print("bake_graphics: the C verifier printed no summary", file=sys.stderr)
        return EXIT_USAGE
    if result.returncode != EXIT_OK:
        return EXIT_MISMATCH
    return EXIT_OK


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        prog="bake_graphics.py",
        description="Verify that a PWHD container's baked wall/sky payloads equal what "
                    "lib/doom/r_data.c composes from the SOURCE WAD's own patches.",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="The harness is compiled in a temporary directory and nothing is written "
               "into the worktree. The container no longer carries the wall patches: "
               "D3.11 drops them after the bake, so --wad is the source of truth.\n",
    )
    parser.add_argument("--check-container", type=Path, metavar="PATH",
                        help="the PWHD container to check")
    parser.add_argument("--wad", type=Path, metavar="PATH",
                        help="the source WAD whose TEXTURE1, PNAMES and patches the "
                             "referee composes from; required with --check-container")
    parser.add_argument("--cc", default="gcc", metavar="NAME",
                        help="C compiler to build the harness with (default: gcc)")
    arguments = parser.parse_args(argv)

    if arguments.check_container is None:
        parser.error("--check-container is required")
    if arguments.wad is None:
        parser.error("--wad is required: a baked container no longer carries its patches")

    if not arguments.check_container.is_file():
        print(f"bake_graphics: {arguments.check_container} is not a file", file=sys.stderr)
        return EXIT_USAGE
    if not arguments.wad.is_file():
        print(f"bake_graphics: {arguments.wad} is not a file", file=sys.stderr)
        return EXIT_USAGE
    return check_container(arguments.check_container, arguments.wad, arguments.cc)


if __name__ == "__main__":
    sys.exit(main())
