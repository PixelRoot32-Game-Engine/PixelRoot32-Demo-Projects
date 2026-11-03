#!/usr/bin/env python3
"""Convert DOOM's `MUS` lumps into the APU's four-track `MusicTrack` format.

WHY THIS TOOL EXISTS

`lib/doom/s_sound.c` keeps upstream's `S_ChangeMusic`, `S_StartMusic`,
`S_StopMusic` and `S_PauseSound`; the DMX/OPL half became a narrow C seam
(`doom_music_*`) implemented over the APU. The APU has no streaming decoder and
no PCM channel (pure synthesis, `Legacy` profile), so the music has to
exist as `const MusicNote` tables in flash, produced here, offline.

THE INPUT

A `MUS` lump is a 16-byte header (`"MUS\\x1a"`, `scorelen`, `scorestart`,
primary/secondary channel counts, 16 instrument numbers) followed by events.
The descriptor byte is `0x80 | (type << 4) | channel`; the last descriptor of a
group has bit 7 set and a variable-length delay follows it in base 128 (high bit
= continue). The types are upstream's:

    0 release note  1 data byte (the note)
    1 press note    1 note byte; a velocity byte follows only when the note
                    byte has bit 7 set, otherwise the channel keeps its
                    previous velocity
    2 pitch wheel   1 data byte
    3 system        1 data byte
    4 controller    2 data bytes
    6 end of score

This is chocolate-doom's `src/mus2mid.c` reading, and it is what the counts in
this file's header comments were verified against.

THE CLOCK

`MUS` has no tempo field. Its tick clock is fixed at 140 ticks a second and
`mus2mid` writes the score at MIDI resolution 70 ticks per quarter note, so
`seconds = ticks / 140`; a quarter note is 70 ticks.

The APU's `MusicPlayer` expresses tempo in BPM and `ApuCore` allocates
`TICKS_PER_BEAT` (4) sequencer ticks to a beat. `ApuCore` clamps `tempoBPM` to
300, so the finest grid it can represent is 60 / (300 * 4) = 0.05 s, which is
exactly 7 MUS ticks. This converter runs the player at **300 BPM** and writes
durations in that grid, not as `mus_ticks / 70`.

WHY THE STORED VALUE IS NOT `mus_ticks / 70`

At 120 BPM the sequencer tick is 125 ms (17.5 MUS ticks). A stored duration is
truncated to whole APU ticks, so a 9-tick note became 0 ticks and `ApuCore`
clamped it to one (125 ms), while a 30-tick note became one tick (125 ms)
instead of 214 ms. The error was per note and accumulated: on E1M1 the bass
played 179.0 beats against a written 192.0, the percussion 215.75, and the four
loops no longer closed together.

The grid removes that. For each track, every press's start is placed on the
nearest APU tick:

    apuTick[k] = round(cumulativeMusTicks[k] / 7)

and the stored duration is the difference of consecutive grid starts divided
by `TICKS_PER_BEAT`:

    duration_beats[k] = (apuTick[k+1] - apuTick[k]) / 4

Because a duration is always a multiple of 0.25 (exactly representable as a
float), `duration * 4` is an exact integer and the sequencer's truncation
returns it unchanged. The track closes at `round(scoreEnd / 7) / 4` beats, so
all four tracks end on the same grid tick and the loops align exactly. Every
note starts within half a grid tick (25 ms) of `mus_ticks / 140`, and the loop
length is the score's rounded up by at most 25 ms. The 25 ms is the price of
the API's 300 BPM ceiling; it is measured by the gate per track.

THE CHANNELS

`MUS` channel 15 is the percussion channel (mus2mid maps it to MIDI channel 9).
Every other channel that carries press events is melodic. Polyphony in the
shareware set is at most three melodic voices and usually one or two, so a
monophonic sequencer track per channel is nearly faithful.

WHY ROLES ARE DERIVED, NOT FIXED

A fixed channel number would not survive the nine shareware levels:

    D_E1M1  bass ch2 (mean pitch 37.6)   D_E1M3  bass ch0 (37.2)
    D_E1M5  bass ch1 (29.3)              D_E1M6  bass ch5 (29.7)
    D_INTER bass ch2 (29.7)              D_INTRO bass ch0 (8.0)

So the rule is measured per lump:

  1. rank melodic channels by note count (ties broken by the lower channel
     number, so the result never depends on dict order);
  2. keep the three busiest -- they carry the material;
  3. among those, the lowest mean MIDI pitch becomes the bass (triangle,
     `INSTR_TRIANGLE_BASS`), the highest becomes the melody (pulse, duty 0.5,
     `INSTR_PULSE_LEAD`), and the one left is the third voice (pulse,
     duty 0.125, `INSTR_PULSE_HARMONY`).

A channel that does not make the three is dropped; the tool prints its number
and note count, and the gate treats that printed list as the only allowed drop.
A one-channel score becomes the melody (bass/melody need two registers to be
distinguishable).

THE NOTES

Within a channel a press starts a note and the next press ends it: one
`MusicNote` per press, with the duration being the difference of their rounded
grid starts (above). A gap that rounds to the same APU tick yields
`duration = 0.0` -- `ApuCore`'s "fire without advancing" path, which is exactly
a same-tick retrigger on a monophonic track. The 1-tick clamp is never reached:
0.0 is not positive and every other duration is an exact multiple of 0.25.

The last press has no next press. It is bounded by that channel's last release
event (type 0), or by the score end when no release follows, and one rest
closes the track to the score's grid tick. A channel whose first press is late
(E1M1's melody starts at tick 1221) gets a leading rest of its rounded grid
start. The sum of every track's durations is `round(scoreEnd / 7) / 4` beats,
identical for all four tracks, so the loops stay aligned.

PITCH

`octave = midi // 12 - 1`, `note = midi % 12` indexes `Note` (C, C#, ... B).
Anything outside octave 0..8 is clamped and counted in the generated header:
D_INTRO's bass channel is MIDI 8, one octave below the APU's floor, so its 24
notes become G#0. Pitch-wheel events are ignored -- they move these scores by
a few cents at most, and the APU has no per-note bend on a sequencer track --
and their count is recorded for every lump.

PERCUSSION

One percussion track. Simultaneous hits are folded to one by a fixed priority
(kick > snare > tom > open hi-hat > closed hi-hat > crash > ride > other); the
losers are counted and printed. Each hit is a `Note::Rest` carrying a noise
preset, which is the only shape `ApuCore` treats as a drum (`channelType ==
NOISE && preset && preset.duty == 0`). `ApuCore` uses the preset's
`defaultDuration` for the *sound* (kick 120 ms, snare 150 ms, hi-hat 50 ms) and
the note's `duration` only to advance the sequencer, so the emitted duration is
the gap to the next hit and the timbre comes from the preset.

The APU has three percussion presets, so General MIDI's families collapse:

    35, 36          kick            INSTR_KICK
    38, 39, 40      snare           INSTR_SNARE
    41,43,45,47,48,50 tom           INSTR_SNARE (mid-band body; the APU cannot
                                    pitch noise per note, and folding toms into
                                    the kick would erase the tom patterns)
    46              open hi-hat     INSTR_HIHAT
    42, 44          closed hi-hat   INSTR_HIHAT
    49,52,55,57     crash           INSTR_HIHAT (the only metallic voice)
    51,53,59        ride/bell       INSTR_HIHAT
    anything else   other           INSTR_HIHAT, and the tool prints it

The note numbers the shareware WAD actually uses are all in that table; a
number outside it is not silently dropped, it is folded to the hi-hat and
named on stderr.

OUTPUT

`src/assets/music/MusE1M1.inc` is the committed asset: the melody array
`kMusE1M1Notes`, the second/third/percussion arrays, three sub-track structs and
`const MusicTrack kMusE1M1` with `loop = true`. It is deterministic: no
timestamps, no locale-dependent formatting (Python's `%.6f` always uses `.`),
no unordered iteration. `--check` regenerates in memory and fails if the
committed bytes differ.

Flash is the binding constraint. `sizeof(MusicNote)` is 16 B on the ESP32
(1+1+2 pad+4+4+4), so `D_E1M1`'s emitted array is the number in its header and
`D_INTER`'s would not fit the image; the tool prints the arithmetic for every
`d_*` lump and only E1M1 is written.

    python tools/mus2tracks.py --wad tools/DOOM1.WAD
    python tools/mus2tracks.py --wad tools/DOOM1.WAD --check
"""
from __future__ import annotations

import argparse
import re
import struct
import sys
from dataclasses import dataclass, field
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

# MUS's fixed clock. One quarter note is 70 ticks at 140 ticks/s.
MUS_TICKS_PER_SECOND = 140.0
MUS_TICKS_PER_QUARTER = 70.0
# ApuCore clamps tempoBPM to 300, so 300 is the finest grid the sequencer can
# represent: one beat is 60/300 = 0.2 s and one sequencer tick is 1/4 beat =
# 0.05 s = exactly 7 MUS ticks (1/140 s). At 120 BPM the tick was 125 ms
# (17.5 MUS ticks) and every short note had to be clamped to 125 ms, which
# stretched each track by a different amount.
PLAYER_BPM = 300.0
APU_TICKS_PER_BEAT = 4
MUS_TICKS_PER_APU_TICK = MUS_TICKS_PER_SECOND * 60.0 / (PLAYER_BPM * APU_TICKS_PER_BEAT)  # 7.0
MUSIC_NOTE_BYTES = 16  # sizeof(MusicNote) on the 32-bit ESP32

# The flash headroom of the _hud image before any music was added. A fixed
# default keeps --check reproducible.
DEFAULT_HEADROOM = 49_795

NOTE_NAMES = ["C", "Cs", "D", "Ds", "E", "F", "Fs", "G", "Gs", "A", "As", "B"]
PITCH_CLASS_COUNT = 12
MIN_OCTAVE = 0
MAX_OCTAVE = 8

# Preset names as spelled in AudioMusicTypes.h.
PRESET_MELODY = "INSTR_PULSE_LEAD"
PRESET_THIRD = "INSTR_PULSE_HARMONY"
PRESET_BASS = "INSTR_TRIANGLE_BASS"
PRESET_KICK = "INSTR_KICK"
PRESET_SNARE = "INSTR_SNARE"
PRESET_HIHAT = "INSTR_HIHAT"
PRESET_OCTAVE = {PRESET_KICK: 1, PRESET_SNARE: 2, PRESET_HIHAT: 3}
PRESET_VOLUME = {
    PRESET_MELODY: 0.40,
    PRESET_THIRD: 0.28,
    PRESET_BASS: 0.36,
    PRESET_KICK: 0.45,
    PRESET_SNARE: 0.35,
    PRESET_HIHAT: 0.25,
}

# Percussion classes, in folding priority order (lower wins).
DRUM_KICK = "kick"
DRUM_SNARE = "snare"
DRUM_TOM = "tom"
DRUM_OPEN_HAT = "open hi-hat"
DRUM_CLOSED_HAT = "closed hi-hat"
DRUM_CRASH = "crash"
DRUM_RIDE = "ride"
DRUM_OTHER = "other"
DRUM_PRIORITY = [
    DRUM_KICK,
    DRUM_SNARE,
    DRUM_TOM,
    DRUM_OPEN_HAT,
    DRUM_CLOSED_HAT,
    DRUM_CRASH,
    DRUM_RIDE,
    DRUM_OTHER,
]
DRUM_PRESET = {
    DRUM_KICK: PRESET_KICK,
    DRUM_SNARE: PRESET_SNARE,
    DRUM_TOM: PRESET_SNARE,
    DRUM_OPEN_HAT: PRESET_HIHAT,
    DRUM_CLOSED_HAT: PRESET_HIHAT,
    DRUM_CRASH: PRESET_HIHAT,
    DRUM_RIDE: PRESET_HIHAT,
    DRUM_OTHER: PRESET_HIHAT,
}
DRUM_CLASS = {
    35: DRUM_KICK,
    36: DRUM_KICK,
    38: DRUM_SNARE,
    39: DRUM_SNARE,
    40: DRUM_SNARE,
    41: DRUM_TOM,
    43: DRUM_TOM,
    45: DRUM_TOM,
    47: DRUM_TOM,
    48: DRUM_TOM,
    50: DRUM_TOM,
    42: DRUM_CLOSED_HAT,
    44: DRUM_CLOSED_HAT,
    46: DRUM_OPEN_HAT,
    49: DRUM_CRASH,
    52: DRUM_CRASH,
    55: DRUM_CRASH,
    57: DRUM_CRASH,
    51: DRUM_RIDE,
    53: DRUM_RIDE,
    59: DRUM_RIDE,
}
# The shareware WAD's odd notes (E1M2/E1M8): GM claves / mute-cuica / open
# triangle. Named here so the survey prints the intended family, not "other".
DRUM_CLASS.update({75: DRUM_CRASH, 80: DRUM_CRASH, 81: DRUM_CRASH})

ROLE_MELODY = "melody"
ROLE_THIRD = "third"
ROLE_BASS = "bass"
ROLE_PERCUSSION = "percussion"
ROLE_ORDER = [ROLE_MELODY, ROLE_THIRD, ROLE_BASS, ROLE_PERCUSSION]
ROLE_PRESET = {
    ROLE_MELODY: PRESET_MELODY,
    ROLE_THIRD: PRESET_THIRD,
    ROLE_BASS: PRESET_BASS,
}
ROLE_CHANNEL_TYPE = {
    ROLE_MELODY: "PULSE",
    ROLE_THIRD: "PULSE",
    ROLE_BASS: "TRIANGLE",
    ROLE_PERCUSSION: "NOISE",
}
ROLE_DUTY = {ROLE_MELODY: 0.5, ROLE_THIRD: 0.125, ROLE_BASS: 0.5, ROLE_PERCUSSION: 0.0}


# --- WAD -------------------------------------------------------------------

def read_wad(path: Path) -> dict[str, bytes]:
    """Lump name (upper case) -> bytes. Later duplicates win, as upstream's
    directory lookup does."""
    data = path.read_bytes()
    if data[:4] not in (b"IWAD", b"PWAD"):
        raise SystemExit(f"mus2tracks: {path} is not a WAD")
    count, directory = struct.unpack_from("<II", data, 4)
    lumps: dict[str, bytes] = {}
    for index in range(count):
        offset, size, name = struct.unpack_from("<II8s", data, directory + index * 16)
        lump = name.rstrip(b"\0").decode("latin1").upper()
        lumps[lump] = data[offset:offset + size]
    return lumps


# --- MUS -------------------------------------------------------------------

@dataclass
class MusScore:
    score_end_ticks: int
    presses: dict[int, list[tuple[int, int]]] = field(default_factory=dict)
    releases: dict[int, list[tuple[int, int]]] = field(default_factory=dict)
    pitch_wheels: int = 0
    controllers: int = 0
    system_events: int = 0


def parse_mus(data: bytes) -> MusScore:
    """Parse a MUS lump. Raises ValueError on an unknown descriptor type."""
    if len(data) < 16 or data[:4] != b"MUS\x1a":
        raise ValueError("not a MUS lump")
    _scorelen, scorestart = struct.unpack_from("<HH", data, 4)
    if scorestart > len(data):
        raise ValueError("scorestart past the lump")

    score = MusScore(score_end_ticks=0)
    offset = scorestart
    tick = 0
    while offset < len(data):
        descriptor = data[offset]
        offset += 1
        event_type = (descriptor >> 4) & 0x07
        channel = descriptor & 0x0F
        last = bool(descriptor & 0x80)

        if event_type == 6:  # end of score
            break
        if event_type == 0:  # release note
            note = data[offset] & 0x7F
            offset += 1
            score.releases.setdefault(channel, []).append((tick, note))
        elif event_type == 1:  # press note
            note = data[offset]
            offset += 1
            if note & 0x80:
                offset += 1  # velocity byte; the converter keeps preset volume
                note &= 0x7F
            score.presses.setdefault(channel, []).append((tick, note))
        elif event_type == 2:  # pitch wheel
            offset += 1
            score.pitch_wheels += 1
        elif event_type == 3:  # system
            offset += 1
            score.system_events += 1
        elif event_type == 4:  # controller
            offset += 2
            score.controllers += 1
        else:
            raise ValueError(f"unknown MUS event type {event_type} at byte {offset - 1}")

        if last:
            delay = 0
            while True:
                byte = data[offset]
                offset += 1
                delay = (delay << 7) | (byte & 0x7F)
                if not (byte & 0x80):
                    break
            tick += delay

    if offset > len(data):
        raise ValueError("MUS event ran past the lump")
    score.score_end_ticks = tick
    return score


# --- Conversion ------------------------------------------------------------

@dataclass
class EmittedNote:
    kind: str  # "note" | "rest" | "drum"
    note: str  # Note enum spelling
    octave: int
    duration: float  # beats
    volume: float
    preset: str | None
    # Provenance for the gate: where the source event was, and where this note
    # starts in beats (filled after the list is built).
    source_tick: int | None = None
    source_midi: int | None = None
    drum_class: str | None = None
    start_beat: float = 0.0


@dataclass
class Role:
    role: str
    channel: int
    count: int
    mean_pitch: float
    notes: list[EmittedNote] = field(default_factory=list)
    beats: float = 0.0


@dataclass
class Conversion:
    lump: str
    music_id: int
    music_name: str
    score: MusScore
    roles: list[Role]
    dropped: list[tuple[int, int, float]]
    folded: list[tuple[int, int, str, str]]  # tick, midi, class, winning class
    clamped: list[tuple[int, int, int, int]]  # tick, midi, note index, octave
    other_drums: list[int]
    press_count: int
    note_ons_emitted: int
    array_notes: int

    def role(self, name: str) -> Role | None:
        for role in self.roles:
            if role.role == name:
                return role
        return None


def _mean(values: list[int]) -> float:
    return sum(values) / len(values) if values else 0.0


def _midi_to_note(midi: int) -> tuple[int, int, bool]:
    """(Note index, octave, clamped). Octave is clamped to 0..8."""
    octave = midi // PITCH_CLASS_COUNT - 1
    index = midi % PITCH_CLASS_COUNT
    clamped = False
    if octave < MIN_OCTAVE:
        octave = MIN_OCTAVE
        clamped = True
    elif octave > MAX_OCTAVE:
        octave = MAX_OCTAVE
        clamped = True
    return index, octave, clamped


def _apu_tick(mus_tick: int) -> int:
    """The nearest sequencer tick at 300 BPM: one tick is 7 MUS ticks."""
    return int(round(mus_tick / MUS_TICKS_PER_APU_TICK))


def _grid_beats(apu_ticks: int) -> float:
    """An integer tick count as `MusicNote::duration` beats."""
    return apu_ticks / APU_TICKS_PER_BEAT


def _add_leading_rest(notes: list[EmittedNote], first_apu_tick: int) -> None:
    if first_apu_tick > 0:
        notes.append(EmittedNote("rest", "Rest", 0, _grid_beats(first_apu_tick),
                                 0.0, None))


def _add_tail_rest(notes: list[EmittedNote], last_end_apu: int,
                   score_end_apu: int) -> None:
    if score_end_apu > last_end_apu:
        notes.append(EmittedNote("rest", "Rest", 0,
                                 _grid_beats(score_end_apu - last_end_apu),
                                 0.0, None))


def _channel_end_tick(score: MusScore, channel: int, last_tick: int) -> int:
    releases = [tick for tick, _note in score.releases.get(channel, []) if tick > last_tick]
    if releases:
        return max(releases)
    return max(last_tick, score.score_end_ticks)


def convert(data: bytes, lump: str = "D_E1M1", music_id: int = 1,
            music_name: str = "e1m1") -> Conversion:
    """Convert one MUS lump into four monophonic tracks."""
    score = parse_mus(data)
    press_count = sum(len(notes) for notes in score.presses.values())

    melodic = [ch for ch in score.presses if ch != 15 and score.presses[ch]]
    ranked = sorted(melodic, key=lambda ch: (-len(score.presses[ch]), ch))
    selected = ranked[:3]
    dropped_channels = ranked[3:]

    # Among the selected, register decides the role.
    by_pitch = sorted(selected, key=lambda ch: (_mean([p for _, p in score.presses[ch]]), ch))
    role_of: dict[str, int] = {}
    if len(by_pitch) == 1:
        role_of[ROLE_MELODY] = by_pitch[0]
    elif len(by_pitch) == 2:
        role_of[ROLE_BASS] = by_pitch[0]
        role_of[ROLE_MELODY] = by_pitch[1]
    elif by_pitch:
        role_of[ROLE_BASS] = by_pitch[0]
        role_of[ROLE_MELODY] = by_pitch[-1]
        role_of[ROLE_THIRD] = by_pitch[1]

    roles: list[Role] = []
    clamped: list[tuple[int, int, int, int]] = []
    for role_name in (ROLE_MELODY, ROLE_THIRD, ROLE_BASS):
        if role_name not in role_of:
            continue
        channel = role_of[role_name]
        presses = score.presses[channel]
        role = Role(role_name, channel, len(presses),
                    _mean([pitch for _, pitch in presses]))
        notes = role.notes
        starts = [_apu_tick(tick) for tick, _pitch in presses]
        _add_leading_rest(notes, starts[0])
        for index, (tick, midi) in enumerate(presses):
            if index + 1 < len(presses):
                end_apu = starts[index + 1]
            else:
                end_apu = _apu_tick(_channel_end_tick(score, channel, tick))
            if end_apu < starts[index]:
                end_apu = starts[index]
            duration = _grid_beats(end_apu - starts[index])
            note_index, octave, was_clamped = _midi_to_note(midi)
            if was_clamped:
                clamped.append((tick, midi, note_index, octave))
            notes.append(EmittedNote(
                "note", NOTE_NAMES[note_index], octave, duration,
                PRESET_VOLUME[ROLE_PRESET[role_name]], ROLE_PRESET[role_name],
                source_tick=tick, source_midi=midi))
        last_tick = presses[-1][0]
        last_end = max(starts[-1], _apu_tick(_channel_end_tick(score, channel, last_tick)))
        _add_tail_rest(notes, last_end, _apu_tick(score.score_end_ticks))
        role.beats = sum(note.duration for note in notes)
        roles.append(role)

    folded: list[tuple[int, int, str, str]] = []
    other_drums: list[int] = []
    drum_notes: list[EmittedNote] = []
    drum_presses = score.presses.get(15, [])
    if drum_presses:
        by_tick: dict[int, list[int]] = {}
        for tick, midi in drum_presses:
            by_tick.setdefault(tick, []).append(midi)
        hits: list[tuple[int, str, int]] = []  # tick, class, source midi
        for tick in sorted(by_tick):
            classes = []
            for midi in by_tick[tick]:
                drum_class = DRUM_CLASS.get(midi, DRUM_OTHER)
                if midi not in DRUM_CLASS:
                    other_drums.append(midi)
                classes.append((DRUM_PRIORITY.index(drum_class), midi, drum_class))
            _priority, winner_midi, winning = min(classes)
            # Every other source hit on the same tick is folded: a lower
            # priority class, or a second hit of the same class.
            for _p, midi, drum_class in classes:
                if midi != winner_midi:
                    folded.append((tick, midi, drum_class, winning))
            hits.append((tick, winning, winner_midi))
        _add_leading_rest(drum_notes, _apu_tick(hits[0][0]))
        starts = [_apu_tick(tick) for tick, _class, _midi in hits]
        for index, (tick, drum_class, _midi) in enumerate(hits):
            if index + 1 < len(hits):
                end_apu = starts[index + 1]
            else:
                end_apu = _apu_tick(_channel_end_tick(score, 15, tick))
            if end_apu < starts[index]:
                end_apu = starts[index]
            duration = _grid_beats(end_apu - starts[index])
            preset = DRUM_PRESET[drum_class]
            drum_notes.append(EmittedNote(
                "drum", "Rest", PRESET_OCTAVE[preset], duration,
                PRESET_VOLUME[preset], preset, source_tick=tick,
                drum_class=drum_class))
        last_end = max(starts[-1],
                       _apu_tick(_channel_end_tick(score, 15, hits[-1][0])))
        _add_tail_rest(drum_notes, last_end, _apu_tick(score.score_end_ticks))
        roles.append(Role(ROLE_PERCUSSION, 15, len(hits), 0.0, drum_notes))
        roles[-1].beats = sum(note.duration for note in drum_notes)

    # Fill start beats for the gate.
    for role in roles:
        beat = 0.0
        for note in role.notes:
            note.start_beat = beat
            beat += note.duration

    dropped = [(ch, len(score.presses[ch]),
                _mean([p for _, p in score.presses[ch]])) for ch in dropped_channels]
    # Press events that produced a note (drums survive the fold).
    note_ons_emitted = sum(role.count for role in roles if role.role != ROLE_PERCUSSION)
    if drum_presses:
        note_ons_emitted += len(drum_notes) - sum(
            1 for note in drum_notes if note.kind == "rest")
    array_notes = sum(len(role.notes) for role in roles)
    return Conversion(
        lump=lump, music_id=music_id, music_name=music_name, score=score,
        roles=roles, dropped=dropped, folded=folded, clamped=clamped,
        other_drums=sorted(set(other_drums)), press_count=press_count,
        note_ons_emitted=note_ons_emitted, array_notes=array_notes)


# --- Rendering -------------------------------------------------------------

def _cstring(text: str) -> str:
    return text.replace("\\", "\\\\").replace('"', '\\"')


def _fmt_float(value: float) -> str:
    return f"{value:.6f}f"


def cpp_name(lump: str) -> str:
    return "kMus" + lump[2:].upper()


def file_name(lump: str) -> str:
    return "Mus" + lump[2:].upper() + ".inc"


def render_include(conv: Conversion) -> str:
    cname = cpp_name(conv.lump)
    score = conv.score
    seconds = score.score_end_ticks / MUS_TICKS_PER_SECOND
    score_apu = _apu_tick(score.score_end_ticks)
    stored_beats = _grid_beats(score_apu)
    lines: list[str] = []
    lines.append("// Generated by tools/mus2tracks.py -- do not edit.")
    lines.append("//")
    lines.append(f"// WAD lump: {conv.lump}  (music id {conv.music_id}, "
                 f"S_music[{conv.music_id}].name = \"{_cstring(conv.music_name)}\")")
    lines.append("// Tempo: MUS's fixed 140 ticks/s; a quarter note is 70 ticks.")
    lines.append(f"// MusicPlayer runs at {PLAYER_BPM:.0f} BPM (ApuCore's clamp ceiling). A beat is")
    lines.append("// 0.2 s and the sequencer tick is 1/4 beat = 0.05 s = 7 MUS ticks, the")
    lines.append("// finest grid the API allows. A duration is an integer number of APU")
    lines.append("// ticks / 4 (a multiple of 0.25), so the sequencer's truncation is exact.")
    lines.append(f"// Score: {score.score_end_ticks} MUS ticks = {seconds:.2f} s = "
                 f"{score_apu} APU ticks = {stored_beats:.3f} stored beats.")
    lines.append(f"// Press events: {conv.press_count}; emitted MusicNotes: {conv.array_notes} "
                 f"(including leading/tail rests and folded drums).")
    lines.append(f"// Pitch-wheel events ignored: {score.pitch_wheels} "
                 f"(a few cents; the APU has no per-note bend on a track).")
    lines.append("//")
    for role in conv.roles:
        if role.role == ROLE_PERCUSSION:
            lines.append(f"// percussion: MUS channel 15, {role.count} folded hits, "
                         f"{role.beats:.3f} beats")
        else:
            lines.append(f"// {role.role}: MUS channel {role.channel}, {role.count} press events, "
                         f"mean pitch {role.mean_pitch:.2f}, {role.beats:.3f} beats")
    if conv.dropped:
        dropped = ", ".join(f"ch{ch}({count} notes, mean {mean:.2f})"
                            for ch, count, mean in conv.dropped)
        lines.append(f"// Dropped (not in the busiest three): {dropped}")
    else:
        lines.append("// Dropped (not in the busiest three): none")
    lines.append(f"// Folded simultaneous drum pairs: {len(conv.folded)}"
                 + ("" if not conv.folded else " -- priority kick > snare > tom > open hat > "
                    "closed hat > crash > ride > other"))
    if conv.clamped:
        lines.append(f"// Pitch clamped into octave 0..8: {len(conv.clamped)} notes")
    else:
        lines.append("// Pitch clamped into octave 0..8: none")
    if conv.other_drums:
        lines.append("// Drum notes outside the class table (folded to hi-hat): "
                     + ", ".join(str(value) for value in conv.other_drums))
    lines.append(f"// Flash (ESP32): {conv.array_notes} * {MUSIC_NOTE_BYTES} B = "
                 f"{conv.array_notes * MUSIC_NOTE_BYTES:,} B")
    lines.append("//")
    lines.append("// A note's `duration` is (next grid tick - this grid tick) / 4 beats,")
    lines.append("// an exact multiple of 0.25; 0.0 is a same-tick retrigger (no advance).")
    lines.append("// Every start is the rounded cumulative MUS tick, within 25 ms of the")
    lines.append("// original (half a grid tick), and all four tracks close at the score's")
    lines.append("// grid tick. Drum sound length comes from the preset's defaultDuration,")
    lines.append("// not from the note's duration.")
    lines.append("#pragma once")
    lines.append("")
    lines.append("#include <pixelroot32/apu/AudioMusicTypes.h>")
    lines.append("")
    lines.append("namespace pixelroot32 {")
    lines.append("namespace audio {")
    lines.append("")

    arrays = {
        ROLE_MELODY: f"{cname}Notes",
        ROLE_THIRD: f"{cname}SecondNotes",
        ROLE_BASS: f"{cname}ThirdNotes",
        ROLE_PERCUSSION: f"{cname}PercussionNotes",
    }
    for role in conv.roles:
        array = arrays[role.role]
        lines.append(f"const MusicNote {array}[] = {{")
        for note in role.notes:
            preset = "nullptr" if note.preset is None else f"&{note.preset}"
            lines.append(f"    {{Note::{note.note}, {note.octave}, {_fmt_float(note.duration)}, "
                         f"{note.volume:.2f}f, {preset}}},")
        lines.append("};")
        lines.append("")

    by_role = {role.role: role for role in conv.roles}
    for role in (ROLE_THIRD, ROLE_BASS, ROLE_PERCUSSION):
        if role not in by_role:
            continue
        entry = by_role[role]
        suffix = {"third": "SecondTrack", "bass": "ThirdTrack",
                  "percussion": "PercussionTrack"}[role]
        lines.append(f"const MusicTrack {cname}{suffix} = {{")
        lines.append(f"    {arrays[role]}, {len(entry.notes)}u, true, "
                     f"WaveType::{ROLE_CHANNEL_TYPE[role]}, {ROLE_DUTY[role]:.3f}f,")
        lines.append("    nullptr, nullptr, nullptr};")
        lines.append("")

    melody = by_role[ROLE_MELODY]
    lines.append(f"const MusicTrack {cname} = {{")
    lines.append(f"    {arrays[ROLE_MELODY]}, {len(melody.notes)}u, true, "
                 f"WaveType::{ROLE_CHANNEL_TYPE[ROLE_MELODY]}, {ROLE_DUTY[ROLE_MELODY]:.3f}f,")
    second = f"&{cname}SecondTrack" if ROLE_THIRD in by_role else "nullptr"
    third = f"&{cname}ThirdTrack" if ROLE_BASS in by_role else "nullptr"
    percussion = f"&{cname}PercussionTrack" if ROLE_PERCUSSION in by_role else "nullptr"
    lines.append(f"    {second}, {third}, {percussion}}};")
    lines.append("")
    lines.append("}  // namespace audio")
    lines.append("}  // namespace pixelroot32")
    lines.append("")
    return "\n".join(lines)


# --- Reporting -------------------------------------------------------------

def _survey_line(lump: str, conv: Conversion) -> str:
    chosen = []
    for role in conv.roles:
        if role.role == ROLE_PERCUSSION:
            chosen.append(f"percussion:ch{role.channel}({role.count})")
        else:
            chosen.append(f"{role.role}:ch{role.channel}({role.count},p{role.mean_pitch:.1f})")
    dropped = ",".join(f"ch{ch}({count})" for ch, count, _mean_pitch in conv.dropped) or "none"
    seconds = conv.score.score_end_ticks / MUS_TICKS_PER_SECOND
    return (f"id={conv.music_id} name={conv.music_name} lump={lump} "
            f"presses={conv.press_count} ticks={conv.score.score_end_ticks} "
            f"seconds={seconds:.2f} {' '.join(chosen)} dropped={dropped} "
            f"wheels={conv.score.pitch_wheels} array_notes={conv.array_notes} "
            f"bytes={conv.array_notes * MUSIC_NOTE_BYTES}")


def parse_music_table(path: Path) -> list[str]:
    """`S_music[]` names in upstream's order; index == musicenum_t value."""
    text = path.read_text(encoding="utf-8", errors="replace")
    start = text.index("S_music[]")
    body = text[start:]
    names = []
    for match in re.finditer(r"\{\s*(?:\"([^\"]*)\"|NULL)\s*,", body):
        names.append(match.group(1) or "none")
    return names


def music_id_for(names: list[str], name: str) -> int:
    for index, candidate in enumerate(names):
        if candidate == name:
            return index
    return -1


def find_lump(lumps: dict[str, bytes], name: str) -> bytes | None:
    wanted = ("D_" + name).upper()
    if wanted in lumps:
        return lumps[wanted]
    for key, value in lumps.items():
        if key.upper() == wanted:
            return value
    return None


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--wad", type=Path, default=ROOT / "tools" / "DOOM1.WAD")
    parser.add_argument("--sounds", type=Path, default=ROOT / "lib" / "doom" / "sounds.c")
    parser.add_argument("--lump", default="D_E1M1",
                        help="the lump to write (default D_E1M1, the shipped track)")
    parser.add_argument("--out", type=Path, default=None,
                        help="output path; defaults to src/assets/music/<CppName>.inc")
    parser.add_argument("--check", action="store_true",
                        help="regenerate in memory and fail if the committed file differs")
    parser.add_argument("--headroom", type=int, default=DEFAULT_HEADROOM,
                        help="flash bytes available to this task (pre-T47 measurement)")
    args = parser.parse_args(argv)

    if not args.wad.exists():
        print(f"mus2tracks: no WAD at {args.wad}", file=sys.stderr)
        return 2
    lumps = read_wad(args.wad)
    names = parse_music_table(args.sounds)

    conversions: dict[str, Conversion] = {}
    findings: list[str] = []
    for lump in sorted(lumps):
        if not lump.startswith("D_"):
            continue
        name = lump[2:].lower()
        try:
            conv = convert(lumps[lump], lump=lump, music_id=music_id_for(names, name),
                           music_name=name)
        except ValueError as error:
            findings.append(f"{lump}: {error}")
            continue
        conversions[lump] = conv
        print(_survey_line(lump, conv))
        if conv.folded:
            losers: dict[tuple[str, str], int] = {}
            for _tick, _midi, drum_class, winning in conv.folded:
                key = (drum_class, winning)
                losers[key] = losers.get(key, 0) + 1
            summary = ", ".join(f"{lost}->{won}:{count}"
                                for (lost, won), count in sorted(losers.items()))
            print(f"    folded {len(conv.folded)} simultaneous hits ({summary})")
        for midi in conv.other_drums:
            print(f"    drum note {midi} is outside the class table; folded to hi-hat")

    wanted = args.lump.upper()
    if wanted not in conversions:
        print(f"mus2tracks: {wanted} is not a d_* lump in {args.wad}", file=sys.stderr)
        return 2
    conv = conversions[wanted]
    text = render_include(conv)
    out = args.out
    if out is None:
        out = ROOT / "src" / "assets" / "music" / file_name(wanted)

    if args.check:
        if not out.exists():
            print(f"FAIL mus2tracks --check: {out} does not exist", file=sys.stderr)
            return 1
        committed = out.read_text(encoding="utf-8")
        if committed != text:
            print(f"FAIL mus2tracks --check: {out} differs from a fresh conversion",
                  file=sys.stderr)
            return 1
        print(f"OK mus2tracks --check: {out} is byte-identical")
    else:
        out.parent.mkdir(parents=True, exist_ok=True)
        out.write_text(text, encoding="utf-8", newline="\n")
        print(f"wrote {out} ({len(text)} characters)")

    # The arithmetic that decides what may ship. Only E1M1 is written.
    print(f"flash: sizeof(MusicNote) = {MUSIC_NOTE_BYTES} B on the ESP32")
    for lump in ("D_INTRO", "D_INTER", "D_E1M1"):
        entry = conversions.get(lump)
        if entry is None:
            continue
        size = entry.array_notes * MUSIC_NOTE_BYTES
        verdict = "fits" if size <= args.headroom else "does not fit"
        print(f"  {lump}: {entry.array_notes} notes * {MUSIC_NOTE_BYTES} = "
              f"{size:,} B -- {verdict} the {args.headroom:,} B headroom")
    print(f"  committed: {wanted} only.")

    if findings:
        for finding in findings:
            print(f"FAIL {finding}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
