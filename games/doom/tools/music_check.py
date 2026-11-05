#!/usr/bin/env python3
"""Gate DOOM's MUS -> APU four-track conversion.

WHAT THIS PROVES

`tools/mus2tracks.py` is the generator; this file is the independent check of
its contract, and it needs no compiler. It converts the nine level themes
`D_E1M1`..`D_E1M9` (and `D_INTRO`/`D_INTER`, which no build ships) **in
memory** from `tools/DOOM1.WAD` and then, for every emitted track:

  * every press event on a selected melodic channel appears exactly once, with
    the pitch the MUS names (after the documented octave 0..8 clamp) and a
    start within 25 ms (half an APU tick at 300 BPM) of `musTicks / 140`; the
    converter places it on the nearest 7-MUS-tick grid start;
  * every press event on a channel that did not make the busiest three is named
    in the printed drop list, and no other press is missing;
  * every percussion tick appears exactly once, carrying the preset of the
    highest-priority class on that tick (kick > snare > tom > open hi-hat >
    closed hi-hat > crash > ride > other), and every losing source hit is in
    the documented fold list;
  * every track's stored durations sum to `round(scoreEnd / 7) / 4` beats and
    project to the same `round(scoreEnd / 7)` APU ticks, so the four loops
    close together exactly;
  * every committed include -- `src/assets/music/MusE1M1.inc` ..
    `MusE1M9.inc` -- is byte-identical to a fresh conversion and its parsed
    arrays hold the same notes (independent of the renderer: each file is
    parsed back and compared with the MUS). A lump whose score carries only two
    melodic channels (E1M3) must ship no second-voice array and a `nullptr`
    `secondVoice`.

The expected values are re-derived here from the parsed MUS and the documented
rules, not read back from the converter's own structures. The only shared code
is the MUS/WAD readers -- the byte format, not the musical decisions.

NEGATIVE CONTROLS

A gate that can only pass proves nothing. `--perturb` shifts every melodic
pitch by one semitone and `--stub` empties every track; both must make the run
FAIL. The gate reports `OK ... the gate failed as it must` only when every one
of the checked lumps is caught.

THE FLASH ARITHMETIC

The gate prints, and writes to `tools/music_check.txt`, the number of press
events, emitted `MusicNote`s and bytes for each lump, and whether that would
fit the shipped image's headroom. `D_E1M1` is the one asset every build ships;
`D_E1M2`..`D_E1M9` are committed for the native profile
(`DOOM_FULL_PROFILE`) and are not in any 4 MB image; `D_INTRO` and `D_INTER`
are converted and checked but not written to `src/assets/`.

    python tools/music_check.py --wad tools/DOOM1.WAD
    python tools/music_check.py --wad tools/DOOM1.WAD --perturb   # must FAIL
    python tools/music_check.py --wad tools/DOOM1.WAD --stub      # must FAIL

Exit 0 the gate is green, 1 a check failed, 2 unusable input.
"""
from __future__ import annotations

import argparse
import copy
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))

import mus2tracks  # noqa: E402

LEVEL_LUMPS = [f"D_E1M{level}" for level in range(1, 10)]
OTHER_LUMPS = ["D_INTRO", "D_INTER"]
# Checked in this order: the nine committed level themes first, then the two
# that are converted but never written.
LUMP_IDS = {lump: lump[2:].lower() for lump in LEVEL_LUMPS + OTHER_LUMPS}
COMMITTED_LUMPS = tuple(LEVEL_LUMPS)
ASSET_DIR = ROOT / "src" / "assets" / "music"
RECORD = ROOT / "tools" / "music_check.txt"


def asset_path(lump: str) -> Path:
    """The committed include for a lump: `src/assets/music/Mus<Lump>.inc`."""
    return ASSET_DIR / mus2tracks.file_name(lump)


# The array suffix and the `MusicTrack` shape each role is emitted with.
ROLE_SUFFIX = {
    mus2tracks.ROLE_MELODY: "Notes",
    mus2tracks.ROLE_THIRD: "SecondNotes",
    mus2tracks.ROLE_BASS: "ThirdNotes",
    mus2tracks.ROLE_PERCUSSION: "PercussionNotes",
}
SUFFIX_ROLE = {suffix: role for role, suffix in ROLE_SUFFIX.items()}
ROLE_STRUCT = {
    mus2tracks.ROLE_MELODY: ("", "PULSE", 0.5),
    mus2tracks.ROLE_THIRD: ("SecondTrack", "PULSE", 0.125),
    mus2tracks.ROLE_BASS: ("ThirdTrack", "TRIANGLE", 0.5),
    mus2tracks.ROLE_PERCUSSION: ("PercussionTrack", "NOISE", 0.0),
}
# `MusicTrack::{secondVoice, thirdVoice, percussion}`, in declaration order.
SUB_ROLES = [mus2tracks.ROLE_THIRD, mus2tracks.ROLE_BASS, mus2tracks.ROLE_PERCUSSION]
NOTE_NAMES = mus2tracks.NOTE_NAMES
NOTE_INDEX = {name: index for index, name in enumerate(NOTE_NAMES)}


class Failures(list):
    def add(self, text: str) -> None:
        self.append(text)


def _mean(values: list[int]) -> float:
    return sum(values) / len(values) if values else 0.0


def _midi_to_note(midi: int) -> tuple[int, int]:
    index, octave, _clamped = mus2tracks._midi_to_note(midi)
    return index, octave


def _channel_end(score: mus2tracks.MusScore, channel: int, last_tick: int) -> int:
    return mus2tracks._channel_end_tick(score, channel, last_tick)


def selected_roles(score: mus2tracks.MusScore) -> tuple[dict[str, int], list[int]]:
    """The documented rule, re-derived: three busiest melodic channels, and
    register (mean pitch) decides melody/third/bass among them."""
    melodic = [ch for ch in score.presses if ch != 15 and score.presses[ch]]
    ranked = sorted(melodic, key=lambda ch: (-len(score.presses[ch]), ch))
    selected = ranked[:3]
    by_pitch = sorted(selected, key=lambda ch: (_mean([p for _, p in score.presses[ch]]), ch))
    role_of: dict[str, int] = {}
    if len(by_pitch) == 1:
        role_of["melody"] = by_pitch[0]
    elif len(by_pitch) == 2:
        role_of["bass"] = by_pitch[0]
        role_of["melody"] = by_pitch[1]
    elif by_pitch:
        role_of["bass"] = by_pitch[0]
        role_of["melody"] = by_pitch[-1]
        role_of["third"] = by_pitch[1]
    return role_of, ranked[3:]


def expected_melodic(score: mus2tracks.MusScore, channel: int) -> list[dict]:
    """The sequence the contract says a channel becomes.

    Starts are the rounded cumulative MUS ticks on the APU's 7-tick grid at
    300 BPM; a duration is the difference of consecutive grid starts / 4, so
    it is an exact multiple of 0.25 beats.
    """
    presses = score.presses[channel]
    starts = [mus2tracks._apu_tick(tick) for tick, _pitch in presses]
    rows: list[dict] = []
    if starts[0] > 0:
        rows.append({"kind": "rest", "duration": starts[0] / 4.0,
                     "tick": None, "midi": None})
    for index, (tick, midi) in enumerate(presses):
        if index + 1 < len(presses):
            end_apu = starts[index + 1]
        else:
            end_apu = max(starts[index],
                          mus2tracks._apu_tick(_channel_end(score, channel, tick)))
        note_index, octave = _midi_to_note(midi)
        rows.append({"kind": "note", "duration": (end_apu - starts[index]) / 4.0,
                     "tick": tick, "midi": midi, "index": note_index,
                     "octave": octave})
    end_apu = max(starts[-1],
                  mus2tracks._apu_tick(_channel_end(score, channel, presses[-1][0])))
    score_apu = mus2tracks._apu_tick(score.score_end_ticks)
    if score_apu > end_apu:
        rows.append({"kind": "rest", "duration": (score_apu - end_apu) / 4.0,
                     "tick": None, "midi": None})
    exact = 0.0
    for row in rows:
        row["start"] = exact
        exact += row["duration"]
    return rows


def expected_percussion(score: mus2tracks.MusScore) -> list[dict]:
    presses = score.presses.get(15, [])
    if not presses:
        return []
    by_tick: dict[int, list[int]] = {}
    for tick, midi in presses:
        by_tick.setdefault(tick, []).append(midi)
    ticks = sorted(by_tick)
    starts = [mus2tracks._apu_tick(tick) for tick in ticks]
    rows: list[dict] = []
    if starts[0] > 0:
        rows.append({"kind": "rest", "duration": starts[0] / 4.0, "tick": None})
    for index, tick in enumerate(ticks):
        classes = []
        for midi in by_tick[tick]:
            drum_class = mus2tracks.DRUM_CLASS.get(midi, mus2tracks.DRUM_OTHER)
            classes.append((mus2tracks.DRUM_PRIORITY.index(drum_class), midi, drum_class))
        _priority, winner_midi, winning = min(classes)
        preset = mus2tracks.DRUM_PRESET[winning]
        if index + 1 < len(ticks):
            end_apu = starts[index + 1]
        else:
            end_apu = max(starts[index],
                          mus2tracks._apu_tick(_channel_end(score, 15, tick)))
        rows.append({"kind": "drum", "duration": (end_apu - starts[index]) / 4.0,
                     "tick": tick, "preset": preset, "class": winning,
                     "folded": [(midi, drum_class)
                                for _p, midi, drum_class in classes
                                if midi != winner_midi]})
    end_apu = max(starts[-1],
                  mus2tracks._apu_tick(_channel_end(score, 15, ticks[-1])))
    score_apu = mus2tracks._apu_tick(score.score_end_ticks)
    if score_apu > end_apu:
        rows.append({"kind": "rest", "duration": (score_apu - end_apu) / 4.0,
                     "tick": None})
    exact = 0.0
    for row in rows:
        row["start"] = exact
        exact += row["duration"]
    return rows


def check_track(actual: list[mus2tracks.EmittedNote], expected: list[dict],
                label: str, failures: Failures,
                role_name: str | None = None) -> None:
    preset = mus2tracks.ROLE_PRESET.get(role_name) if role_name else None
    if len(actual) != len(expected):
        failures.add(f"{label}: {len(actual)} notes, expected {len(expected)}")
        return
    for index, (note, row) in enumerate(zip(actual, expected)):
        prefix = f"{label}[{index}]"
        if note.kind != row["kind"]:
            failures.add(f"{prefix}: kind {note.kind}, expected {row['kind']}")
            continue
        if abs(note.start_beat - row["start"]) > 1e-6:
            failures.add(f"{prefix}: start {note.start_beat:.6f}, "
                         f"expected {row['start']:.6f}")
        if abs(note.duration - row["duration"]) > 1e-6:
            failures.add(f"{prefix}: duration {note.duration:.6f}, "
                         f"expected {row['duration']:.6f}")
        if note.duration > 0.0:
            ticks = note.duration * 4.0
            if abs(ticks - round(ticks)) > 1e-6:
                failures.add(f"{prefix}: duration {note.duration:.6f} is not a "
                             "0.25-beat (one APU tick) multiple")
        if row["kind"] == "note":
            if NOTE_INDEX.get(note.note) != row["index"] or note.octave != row["octave"]:
                failures.add(f"{prefix}: pitch {note.note}{note.octave}, "
                             f"expected {NOTE_NAMES[row['index']]}{row['octave']} "
                             f"(MIDI {row['midi']} at tick {row['tick']})")
            if note.preset != preset:
                failures.add(f"{prefix}: preset {note.preset}, expected {preset}")
        elif row["kind"] == "drum":
            if note.preset != row["preset"]:
                failures.add(f"{prefix}: preset {note.preset}, expected {row['preset']} "
                             f"for {row['class']} at tick {row['tick']}")
            if note.note != "Rest":
                failures.add(f"{prefix}: drum note is {note.note}, expected Rest")
        else:
            if note.preset is not None:
                failures.add(f"{prefix}: rest carries preset {note.preset}")
        if row["tick"] is not None:
            # One beat is 0.2 s at 300 BPM, so the stored start in seconds is
            # start_beat * 0.2. Half a grid tick is 25 ms.
            error_ms = abs(note.start_beat * 0.2 - row["tick"] / 140.0) * 1000.0
            if error_ms > 25.0 + 1e-6:
                failures.add(f"{prefix}: start {note.start_beat * 0.2 * 1000:.2f} ms, "
                             f"source {row['tick'] / 140.0 * 1000:.2f} ms "
                             f"({error_ms:.2f} ms > 25 ms)")


def check_conversion(conv: mus2tracks.Conversion, failures: Failures,
                     tracks: dict[str, list[mus2tracks.EmittedNote]] | None = None,
                     perc_actual: list[mus2tracks.EmittedNote] | None = None) -> None:
    score = conv.score
    grid_end = mus2tracks._apu_tick(score.score_end_ticks)
    grid_beats = grid_end / 4.0
    role_of, dropped = selected_roles(score)
    reported = [entry[0] for entry in conv.dropped]
    if sorted(reported) != sorted(dropped):
        failures.add(f"{conv.lump}: drop list {sorted(reported)}, expected {sorted(dropped)}")
    for channel in dropped:
        want = len(score.presses[channel])
        for reported_channel, count, _mean_pitch in conv.dropped:
            if reported_channel == channel and count != want:
                failures.add(f"{conv.lump}: ch{channel} drop count {count}, expected {want}")

    role_names = ["melody", "third", "bass"]
    projected: dict[str, int] = {}
    for role_name in role_names:
        expected_role = role_of.get(role_name)
        role = conv.role(role_name)
        if expected_role is None and role is None:
            continue
        if expected_role is None or role is None:
            failures.add(f"{conv.lump}: role {role_name} present={role is not None}, "
                         f"expected={expected_role is not None}")
            continue
        if role.channel != expected_role:
            failures.add(f"{conv.lump}: {role_name} is ch{role.channel}, "
                         f"expected ch{expected_role}")
        actual = tracks.get(role_name, []) if tracks is not None else role.notes
        expected = expected_melodic(score, expected_role)
        check_track(actual, expected, f"{conv.lump}/{role_name}", failures,
                    role_name=role_name)
        projected[role_name] = playback_ticks(actual)
        total = sum(note.duration for note in actual)
        if abs(total - grid_beats) > 1e-6:
            failures.add(f"{conv.lump}/{role_name}: total {total:.6f} beats, "
                         f"expected the score grid {grid_beats:.6f}")
        if projected[role_name] != grid_end:
            failures.add(f"{conv.lump}/{role_name}: projected {projected[role_name]} "
                         f"APU ticks, expected {grid_end}")

    if score.presses.get(15):
        role = conv.role("percussion")
        if role is None:
            failures.add(f"{conv.lump}: percussion missing")
        else:
            actual = (perc_actual if perc_actual is not None else role.notes)
            expected = expected_percussion(score)
            check_track(actual, expected, f"{conv.lump}/percussion", failures)
            projected["percussion"] = playback_ticks(actual)
            total = sum(note.duration for note in actual)
            if abs(total - grid_beats) > 1e-6:
                failures.add(f"{conv.lump}/percussion: total {total:.6f} beats, "
                             f"expected the score grid {grid_beats:.6f}")
            if projected["percussion"] != grid_end:
                failures.add(f"{conv.lump}/percussion: projected "
                             f"{projected['percussion']} APU ticks, expected {grid_end}")
            # The expected sequence has one row per distinct drum tick, and
            # check_track requires one emitted hit per row, so every source
            # tick appears exactly once; the fold list covers the losers.
            expected_folds = set()
            for row in expected:
                if row["kind"] != "drum":
                    continue
                for midi, drum_class in row["folded"]:
                    expected_folds.add((row["tick"], midi, drum_class, row["class"]))
            actual_folds = set(conv.folded)
            if expected_folds != actual_folds:
                failures.add(f"{conv.lump}/percussion: fold list differs "
                             f"(missing {sorted(expected_folds - actual_folds)[:3]}, "
                             f"extra {sorted(actual_folds - expected_folds)[:3]})")

    if projected and (len(set(projected.values())) != 1
                      or next(iter(projected.values())) != grid_end):
        failures.add(f"{conv.lump}: projected lengths differ or miss the grid: "
                     f"{projected} vs {grid_end}")

    # Every press is accounted for: emitted, folded, or dropped.
    selected = set(role_of.values()) | {15}
    for channel, presses in score.presses.items():
        if channel in selected:
            continue
        if channel not in dropped:
            failures.add(f"{conv.lump}: ch{channel} has {len(presses)} presses and is unaccounted")


def parse_asset(text: str, prefix: str) -> tuple[dict[str, list[mus2tracks.EmittedNote]], dict[str, object]]:
    """Parse a committed include back into note lists plus the track fields.

    `prefix` is the lump's C++ name (`kMusE1M1`), so the same parser reads
    every asset without a hand-maintained array table.
    """
    blocks = re.findall(r"const MusicNote (\w+)\[\] = \{(.*?)\n\};", text, re.S)
    tracks: dict[str, list[mus2tracks.EmittedNote]] = {}
    for name, body in blocks:
        if not name.startswith(prefix):
            continue
        role = SUFFIX_ROLE.get(name[len(prefix):])
        if role is None:
            continue
        notes: list[mus2tracks.EmittedNote] = []
        for match in re.finditer(
                r"\{Note::(\w+), (\d+), ([0-9.]+)f, ([0-9.]+)f, "
                r"(nullptr|&(\w+))\},", body):
            note_name, octave, duration, volume, preset = match.groups()[:5]
            preset_name = match.group(6)
            notes.append(mus2tracks.EmittedNote(
                "drum" if preset_name in (mus2tracks.PRESET_KICK,
                                          mus2tracks.PRESET_SNARE,
                                          mus2tracks.PRESET_HIHAT)
                else ("rest" if note_name == "Rest" else "note"),
                note_name, int(octave), float(duration), float(volume),
                preset_name))
        beat = 0.0
        for note in notes:
            note.start_beat = beat
            beat += note.duration
        tracks[role] = notes

    fields: dict[str, object] = {}
    track_match = re.search(
        r"const MusicTrack (\w+) = \{\s*\w+, (\d+)u, (true|false), "
        r"WaveType::(\w+), ([0-9.]+)f,\s*([^}]+)\};", text, re.S)
    if track_match:
        fields["name"] = track_match.group(1)
        fields["count"] = int(track_match.group(2))
        fields["loop"] = track_match.group(3) == "true"
        fields["wave"] = track_match.group(4)
        fields["duty"] = float(track_match.group(5))
        fields["subs"] = [part.strip() for part in track_match.group(6).split(",")]
    return tracks, fields


def parse_track_structs(text: str) -> dict[str, dict[str, object]]:
    fields: dict[str, dict[str, object]] = {}
    for match in re.finditer(
            r"const MusicTrack (\w+) = \{\s*\w+, (\d+)u, (true|false), "
            r"WaveType::(\w+), ([0-9.]+)f,\s*([^}]+)\};", text, re.S):
        name, count, loop, wave, duty, subs = match.groups()
        fields[name] = {
            "count": int(count),
            "loop": loop == "true",
            "wave": wave,
            "duty": float(duty),
            "subs": [part.strip() for part in subs.split(",")],
        }
    return fields


def check_committed(lump: str, conv: mus2tracks.Conversion, failures: Failures) -> None:
    """Every committed include, parsed back, is the asset the MUS describes."""
    path = asset_path(lump)
    if not path.exists():
        failures.add(f"{path} does not exist")
        return
    text = path.read_text(encoding="utf-8")
    fresh = mus2tracks.render_include(conv)
    if text != fresh:
        failures.add(f"{path} is not byte-identical to a fresh conversion")

    prefix = mus2tracks.cpp_name(lump)
    tracks, _fields = parse_asset(text, prefix)
    present = {role for role in ROLE_STRUCT if conv.role(role) is not None}
    for role in ROLE_STRUCT:
        if role in present and role not in tracks:
            failures.add(f"{path.name}: no array for {role}")
        if role not in present and role in tracks:
            failures.add(f"{path.name}: array for absent role {role}")

    structs = parse_track_structs(text)
    for role, (suffix, wave, duty) in ROLE_STRUCT.items():
        name = prefix + suffix
        if role not in present:
            if name in structs:
                failures.add(f"{path.name}: {name} exists for an absent role")
            continue
        entry = structs.get(name)
        if entry is None:
            failures.add(f"{path.name}: no {name} struct")
            continue
        if entry["count"] != len(tracks.get(role, [])):
            failures.add(f"{path.name}: {name}.count {entry['count']}, "
                         f"{role} array {len(tracks.get(role, []))}")
        if entry["loop"] is not True:
            failures.add(f"{path.name}: {name}.loop is not true")
        if entry["wave"] != wave or abs(float(entry["duty"]) - duty) > 1e-9:
            failures.add(f"{path.name}: {name} is {entry['wave']} duty {entry['duty']}, "
                         f"expected {wave} {duty}")

    # The main track names exactly the sub-tracks this score has: a `nullptr`
    # for a role the lump does not carry (E1M3 has no third melodic channel)
    # and a `&` pointer for every one it does.
    main = structs.get(prefix, {})
    subs = main.get("subs", [])
    if len(subs) != len(SUB_ROLES):
        failures.add(f"{path.name}: {prefix} sub-track pointers {subs}, "
                     f"expected {len(SUB_ROLES)}")
    else:
        for role, sub in zip(SUB_ROLES, subs):
            suffix = ROLE_STRUCT[role][0]
            if role in present:
                wanted = f"&{prefix}{suffix}"
            else:
                wanted = "nullptr"
            if sub != wanted:
                failures.add(f"{path.name}: {prefix} sub-track for {role} is {sub}, "
                             f"expected {wanted}")

    # The committed arrays are checked against the MUS, not only against the
    # in-memory conversion, so a renderer/parser-friendly bug cannot pass.
    check_conversion(conv, failures, tracks=tracks,
                     perc_actual=tracks.get("percussion", []))


def playback_ticks(notes: list[mus2tracks.EmittedNote]) -> int:
    """What `ApuCore` will actually advance, in its 4-ticks-per-beat grid.

    The converter emits durations as exact multiples of 0.25, so `duration*4`
    is an integer and the sequencer's truncation returns it unchanged. A 0.0
    duration does not advance. All four tracks must project to the same grid
    tick.
    """
    total = 0
    for note in notes:
        if note.duration <= 0.0:
            continue
        total += int(round(note.duration * 4.0))
    return total


def worst_start_error_ms(conv: mus2tracks.Conversion, role_name: str) -> float:
    """The largest distance between an emitted start and `musTick / 140`."""
    role = conv.role(role_name)
    if role is None:
        return 0.0
    errors = [abs(note.start_beat * 0.2 - note.source_tick / 140.0) * 1000.0
              for note in role.notes
              if note.kind != "rest" and note.source_tick is not None]
    return max(errors) if errors else 0.0


def arithmetic(conv: mus2tracks.Conversion, headroom: int,
               verdict: str | None = None) -> str:
    size = conv.array_notes * mus2tracks.MUSIC_NOTE_BYTES
    if verdict is None:
        verdict = (f"{'fits' if size <= headroom else 'does not fit'} "
                   f"the {headroom:,} B headroom")
    return (f"{conv.lump}: {conv.press_count} presses -> {conv.array_notes} MusicNotes"
            f" * {mus2tracks.MUSIC_NOTE_BYTES} B = {size:,} B ({verdict})")


def lump_verdict(lump: str, conv: mus2tracks.Conversion, headroom: int) -> str:
    """What this lump's byte count means for the two images.

    The headroom is the 4 MB board's, so it decides what that image may ship.
    The native profile is not bound by it: it commits a theme per level on
    purpose.
    """
    size = conv.array_notes * mus2tracks.MUSIC_NOTE_BYTES
    fits = size <= headroom
    if lump == "D_E1M1":
        return (f"the one asset every build ships; "
                f"{'fits' if fits else 'does not fit'} the 4 MB image's "
                f"{headroom:,} B headroom")
    if lump in COMMITTED_LUMPS:
        return ("committed for native only (DOOM_FULL_PROFILE); "
                "not in any 4 MB image")
    if lump == "D_INTRO":
        return (f"{'fits' if fits else 'does not fit'} the {headroom:,} B headroom "
                "but no scene requests mus_intro; checked in memory only")
    return (f"{'fits' if fits else 'does not fit'} the {headroom:,} B headroom; "
            "checked in memory only")


def collect(wad_path: Path, headroom: int) -> tuple[dict[str, mus2tracks.Conversion], Failures]:
    lumps = mus2tracks.read_wad(wad_path)
    music_names = mus2tracks.parse_music_table(ROOT / "lib" / "doom" / "sounds.c")
    conversions: dict[str, mus2tracks.Conversion] = {}
    failures = Failures()
    for lump, name in LUMP_IDS.items():
        data = mus2tracks.find_lump(lumps, name)
        if data is None:
            failures.add(f"{lump}: no {('D_' + name).upper()} lump in {wad_path}")
            continue
        try:
            conv = mus2tracks.convert(data, lump=lump,
                                      music_id=mus2tracks.music_id_for(music_names, name),
                                      music_name=name)
        except ValueError as error:
            failures.add(f"{lump}: {error}")
            continue
        conversions[lump] = conv
        print(arithmetic(conv, headroom, lump_verdict(lump, conv, headroom)))
    return conversions, failures


def run_gate(conversions: dict[str, mus2tracks.Conversion], headroom: int,
             perturb: bool, stub: bool) -> tuple[Failures, dict[str, Failures]]:
    failures = Failures()
    per_lump: dict[str, Failures] = {}
    for lump, conv in conversions.items():
        local = Failures()
        if stub:
            conv = copy.deepcopy(conv)
            for role in conv.roles:
                role.notes = []
                role.beats = 0.0
        elif perturb:
            conv = copy.deepcopy(conv)
            for role in conv.roles:
                if role.role == mus2tracks.ROLE_PERCUSSION:
                    continue
                for note in role.notes:
                    if note.kind != "note":
                        continue
                    midi = NOTE_INDEX[note.note] + (note.octave + 1) * 12
                    index, octave = _midi_to_note(midi + 1)
                    note.note = NOTE_NAMES[index]
                    note.octave = octave
        check_conversion(conv, local)
        if lump in COMMITTED_LUMPS and not (perturb or stub):
            check_committed(lump, conv, local)
        per_lump[lump] = local
        for finding in local:
            failures.add(finding)
    return failures, per_lump


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--wad", type=Path, default=ROOT / "tools" / "DOOM1.WAD")
    parser.add_argument("--headroom", type=int, default=mus2tracks.DEFAULT_HEADROOM)
    group = parser.add_mutually_exclusive_group()
    group.add_argument("--perturb", action="store_true",
                       help="shift every melodic pitch by one semitone; must FAIL")
    group.add_argument("--stub", action="store_true",
                       help="empty every track; must FAIL")
    parser.add_argument("--check", action="store_true",
                       help="require the committed asset to be byte-identical "
                            "(it is always required; the flag is the explicit form)")
    parser.add_argument("--no-record", action="store_true",
                       help="leave tools/music_check.txt alone")
    args = parser.parse_args(argv)

    if not args.wad.exists():
        print(f"FAIL music_check: no WAD at {args.wad}", file=sys.stderr)
        return 2
    conversions, failures = collect(args.wad, args.headroom)
    if failures:
        for finding in failures:
            print(f"FAIL {finding}")
        return 2

    if args.perturb or args.stub:
        failures, per_lump = run_gate(conversions, args.headroom, args.perturb, args.stub)
        label = "perturb" if args.perturb else "stub"
        caught = [lump for lump, local in per_lump.items() if local]
        if len(caught) != len(conversions):
            print(f"FAIL {label}: the gate did not catch "
                  f"{sorted(set(conversions) - set(caught))}")
            return 1
        print(f"OK {label}: the gate failed as it must "
              f"({len(caught)} of {len(conversions)} lumps caught)")
        for lump, local in per_lump.items():
            print(f"   {lump}: {len(local)} finding(s), first: {local[0]}")
        return 0

    failures, per_lump = run_gate(conversions, args.headroom, False, False)
    for lump, local in per_lump.items():
        if local:
            for finding in local:
                print(f"FAIL {finding}")
        else:
            conv = conversions[lump]
            grid_end = mus2tracks._apu_tick(conv.score.score_end_ticks)
            worst = max(worst_start_error_ms(conv, role.role) for role in conv.roles)
            print(f"OK {lump}: {conv.array_notes} MusicNotes, every press accounted for, "
                  f"closes at {grid_end / 4.0:.3f} stored beats "
                  f"({grid_end} APU ticks = {conv.score.score_end_ticks / 140.0:.2f} s)")
            print(f"   grid: {grid_end} ticks on every track, "
                  f"worst start error {worst:.2f} ms")
    if failures:
        print(f"FAIL music_check: {len(failures)} finding(s)")
        return 1

    # Every committed include was compared above. Say what the two images ship
    # and why the intro/intermission stay off both.
    committed_notes = sum(conversions[lump].array_notes for lump in COMMITTED_LUMPS)
    e1m1_notes = conversions["D_E1M1"].array_notes
    print(f"   native profile: {len(COMMITTED_LUMPS)} level themes, "
          f"{committed_notes:,} MusicNotes = "
          f"{committed_notes * mus2tracks.MUSIC_NOTE_BYTES:,} B")
    print(f"   4 MB image: D_E1M1 only, {e1m1_notes} MusicNotes = "
          f"{e1m1_notes * mus2tracks.MUSIC_NOTE_BYTES:,} B")
    intro = conversions.get("D_INTRO")
    inter = conversions.get("D_INTER")
    if intro is not None and inter is not None:
        print(f"   D_INTRO {intro.array_notes * mus2tracks.MUSIC_NOTE_BYTES:,} B "
              f"would fit the {args.headroom:,} B headroom but no scene requests "
              "mus_intro; not committed")
        print(f"   D_INTER {inter.array_notes * mus2tracks.MUSIC_NOTE_BYTES:,} B "
              f"does not fit the {args.headroom:,} B headroom")
    print(f"OK music_check: {len(conversions)} lumps converted, "
          f"{sum(len(conv.roles) for conv in conversions.values())} tracks, "
          f"{len(COMMITTED_LUMPS)} committed includes byte-identical")

    if not args.no_record:
        lines = ["music_check -- MUS -> four tracks (generated by tools/music_check.py)",
                 ""]
        for lump, conv in conversions.items():
            lines.append(arithmetic(conv, args.headroom,
                                    lump_verdict(lump, conv, args.headroom)))
            role_of, dropped = selected_roles(conv.score)
            chosen = ", ".join(f"{role}:ch{channel}" for role, channel in role_of.items())
            dropped_text = ", ".join(f"ch{ch}({len(conv.score.presses[ch])})"
                                     for ch in dropped) or "none"
            lines.append(f"  chosen {chosen}; dropped {dropped_text}; "
                         f"folded {len(conv.folded)} drum hits; "
                         f"pitch wheels {conv.score.pitch_wheels}; clamped {len(conv.clamped)}")
            for role in conv.roles:
                ticks = playback_ticks(role.notes)
                err = worst_start_error_ms(conv, role.role)
                lines.append(f"  {role.role}: {len(role.notes)} MusicNotes, "
                             f"{role.beats:.3f} stored beats; {ticks} APU ticks "
                             f"({ticks / 4.0:.3f} beats); worst start {err:.2f} ms")
            lines.append("")
        lines.append(f"native profile: {len(COMMITTED_LUMPS)} level themes = "
                     f"{committed_notes:,} MusicNotes * "
                     f"{mus2tracks.MUSIC_NOTE_BYTES} B = "
                     f"{committed_notes * mus2tracks.MUSIC_NOTE_BYTES:,} B "
                     "(DOOM_FULL_PROFILE only)")
        lines.append(f"4 MB image: D_E1M1 only = {e1m1_notes} MusicNotes * "
                     f"{mus2tracks.MUSIC_NOTE_BYTES} B = "
                     f"{e1m1_notes * mus2tracks.MUSIC_NOTE_BYTES:,} B")
        lines.append(f"gate: {len(conversions)} lumps pass; every track closes on the "
                     "score's 7-tick grid; " f"{len(COMMITTED_LUMPS)} committed includes "
                     "are byte-identical; D_INTRO/D_INTER are checked in memory only")
        RECORD.write_text("\n".join(lines) + "\n", encoding="utf-8", newline="\n")
        print(f"   record: {RECORD.relative_to(ROOT)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
