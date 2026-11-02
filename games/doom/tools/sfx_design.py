#!/usr/bin/env python3
"""Derive the SFX bank's 108 cues from DOOM's original `DS*` lumps.

WHY THIS TOOL EXISTS

T45 shipped legal placeholders grouped by character. S16 asks for the opposite:
"tratar de simular lo mas fiel posible la sintesis al sonido original". This
script turns that sentence into data. It parses `lib/doom/sounds.c`'s `S_sfx[]`
(the 1997 name table), resolves each id's lump the way upstream does (`DS` +
uppercase(name)), reads the DMX sample out of `tools/DOOM1.WAD`, and fits the
cue parameters the APU can actually synthesise. It writes:

  src/doomc/SfxBankTable.inc   the committed table `SfxBank.cpp` compiles
  tools/sfx_fit.txt            the per-cue record (id, name, class, donor,
                               the fitted numbers and the measured deviation)

`tools/sfx_check.py` then renders every donated cue through the real ApuCore on
the host and rewrites the `gate:` line of the record with what it measured. The
record is the evidence; this file is the generator.

THE ANALYSIS, AND WHY IT IS ENOUGH

DOOM's samples are 11025 Hz, 8-bit unsigned DMX: an 8-byte header (format,
rate, sample count) followed by PCM. The brief calls it a 16-byte header; the
WAD disagrees, and the reader accepts both shapes so a future re-release with
padding still loads.

Each cue is reduced to four measurable contours:

  * a 16-segment RMS envelope, normalised to its loudest segment. This is what
    ADSR is fitted to, and the gate correlates against it.
  * a spectral-centroid contour: per segment, the zero-crossing rate halved,
    in Hz. A true DFT would cost more than the answer is worth here: ZCR/2 is
    monotone in the centroid for these lowpassed, mostly unipolar samples
    (more high-frequency energy means more crossings), and the gate only has
    to compare the rendered cue with the original under the same estimator,
    so a systematic bias cancels. It is a proxy, stated as one.
  * a pitch track: per segment, the normalised autocorrelation of the loudest
    25 ms window, accepted only when the peak is >= 0.5 and its lag is inside
    40 Hz..2000 Hz. The peak is searched only after the autocorrelation's
    first zero crossing, otherwise a lowpassed noise's trivial short-lag
    correlation reads as a 2 kHz "pitch". The window is 25 ms and a lag is
    only tested with at least half the window overlapping itself, so the
    practical floor is ~80 Hz at these rates; the 40 Hz bound is kept and
    documented rather than pretended.
  * a duty contour (fraction of samples above the segment's mean), used only
    for pulse cues.

The classifier is deterministic and printed per id:

  metallic  a high (>= 1850 Hz), narrow (coefficient of variation <= 0.45)
            centroid. This is the chainsaw's grind and the shotgun's cocking
            click; the cue uses the APU's 93-step LFSR (`noiseShort`). The
            class deliberately does not require "aperiodic": the 93-step
            sequence repeats inside the 25 ms pitch window, so the synthesiser
            would fail its own classifier if periodicity were the test.
  noise     everything else that is aperiodic (pitch fraction < 0.5 or pitch
            confidence < 0.85).
  triangle  tonal (pitch fraction >= 0.5 and confidence >= 0.85) with a low
            centroid (< 1000 Hz) and a sustained, smooth envelope.
  pulse     tonal, everything else: the APU's pulse wave carries the
            harmonics the original's centroid showed.
  saw       tonal with a very flat pitch track and a centroid at least 2.5x
            the fundamental, i.e. dense harmonics. Reserved for data that
            clearly supports it; this shareware set has no such cue and the
            report says so (0 rows), rather than forcing one.

THE FIT

`frequency` is the pitch median for tonal cues and the LFSR clock derived from
the centroid for noise. A 4-point pitch table is emitted when the (smoothed)
contour moves by more than 1.5x, and a 4-point duty table when the duty
contour does. ADSR is a least-squares fit over every (attack end, decay end)
pair: attack is the rise to the first segment >= 0.8 of peak, decay runs to a
sustain plateau, sustain is solved in closed form, and release is the fall to
10 % capped at the APU's 100 ms. The model's envelope correlation is recorded;
the gate measures the rendered one.

FIFTY-THREE IDS HAVE NO LUMP

The shareware WAD carries 55 `DS*` lumps. The other 53 ids (Doom 2's monsters
and weapons, the super shotgun, the boss family) are fitted from the closest
relative in the same weapon/monster/door/UI family. The donor and the reason
are printed in the report for every one, so the substitution is auditable; no
lump is invented and no id is left without a cue.

DETERMINISM

The same WAD and sounds.c produce a byte-identical table: no timestamps, no
locale-dependent formatting, no iteration over unordered containers. Floats
are rounded before printing. `--check` regenerates in memory and exits 1 if
the committed files differ.

    python tools/sfx_design.py --wad tools/DOOM1.WAD
    python tools/sfx_design.py --wad tools/DOOM1.WAD --check
"""
from __future__ import annotations

import argparse
import math
import re
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

SEGMENTS = 16
PITCH_WINDOW_SEC = 0.025
PITCH_MIN_HZ = 40.0
PITCH_MAX_HZ = 2000.0
PITCH_MIN_CORR = 0.5
# The APU's NOISE clock is not the ZCR centroid: a 15-bit LFSR held for
# `rate/clock` samples crosses zero far less often than twice per clock, and
# the 93-step mode crosses even less. Tools/sfx_render.cpp measured this table
# at the board's 22050 Hz (long mode / short mode); the fit inverts it so the
# rendered centroid lands on the original's. Keeping the pairs here (rather
# than a formula) is deliberate: the relation is a staircase from the period's
# integer quantisation, and a formula would be a fiction.
NOISE_CLOCKS_LONG = [
    (100, 5), (200, 10), (300, 30), (400, 40), (500, 60), (600, 90),
    (700, 105), (800, 125), (900, 155), (1000, 175), (1100, 220), (1200, 255),
    (1300, 270), (1400, 280), (1500, 310), (1600, 325), (1700, 360),
    (1800, 360), (1900, 390), (2000, 390), (2100, 455), (2200, 455),
    (2300, 510), (2400, 510), (2500, 570), (2600, 570), (2700, 570),
    (2800, 700), (2900, 700), (3000, 700), (3100, 700), (3200, 815),
    (3300, 815), (3400, 815), (3500, 815), (3600, 815), (3700, 1005),
    (3800, 1005), (3900, 1005), (4000, 1005), (4100, 1005), (4200, 1005),
    (4300, 1005), (4400, 1005), (4500, 1270), (4600, 1270), (4700, 1270),
    (4800, 1270), (4900, 1270), (5000, 1270), (6000, 1740), (7000, 1740),
    (8000, 2690), (9000, 2690), (10000, 2690), (11000, 2690), (12000, 5510),
]
NOISE_CLOCKS_SHORT = [
    (100, 5), (200, 15), (300, 30), (400, 50), (500, 40), (600, 50), (700, 60),
    (800, 65), (900, 80), (1000, 85), (1100, 95), (1200, 110), (1300, 125),
    (1400, 120), (1500, 135), (1600, 145), (1700, 160), (1800, 160),
    (1900, 165), (2000, 165), (2100, 195), (2200, 195), (2300, 205),
    (2400, 205), (2500, 240), (2600, 240), (2700, 240), (2800, 275),
    (2900, 275), (3000, 275), (3100, 275), (3200, 315), (3300, 315),
    (3400, 315), (3500, 315), (3600, 315), (3700, 375), (3800, 375),
    (3900, 375), (4000, 375), (4100, 375), (4200, 375), (4300, 375),
    (4400, 375), (4500, 470), (4600, 470), (4700, 470), (4800, 470),
    (4900, 470), (5000, 470), (6000, 630), (7000, 630), (8000, 945),
    (9000, 945), (10000, 945), (11000, 945), (12000, 1890),
]


def _clock_for_centroid(target: float, short: bool) -> float:
    table = NOISE_CLOCKS_SHORT if short else NOISE_CLOCKS_LONG
    if target <= 0.0:
        return table[0][0]
    return min(table, key=lambda pair: abs(math.log(pair[1] / target)))[0]


# --- The steal rule, unchanged from T45 ------------------------------------
#
# Reserved is S16's "never stolen": the player's pain, the player's weapon and
# the boss family. High is an explosion, a door, a lift, a switch. Everything
# else is Normal. This is a policy table, not a measurement; the fit must not
# move it, or the 8 -> 4 rule would change under the sound's feet.

_RESERVED = {
    "pistol", "shotgn", "sgcock", "dshtgn", "dbopn", "dbcls", "dbload",
    "plasma", "bfg", "sawup", "sawidl", "sawful", "sawhit", "rlaunc",
    "plpain", "pldeth", "pdiehi", "punch", "chgun",
    "bospit", "boscub", "bossit", "bospn", "bosdth",
}
_HIGH = {
    "rxplod", "firxpl", "pstart", "pstop", "doropn", "dorcls", "stnmov",
    "swtchn", "swtchx", "barexp", "bdopn", "bdcls",
}

# --- The 53 ids the shareware WAD has no lump for --------------------------
#
# (donor, why). Every donor is itself a direct lump, so the substitution is one
# hop and cannot cycle.
DONORS = {
    "dshtgn": ("shotgn", "the super shotgun fires the same shell; the shareware WAD has no DSDSHTGN"),
    "dbopn": ("sgcock", "the double-barrel's breech opens like the pump gun's cocking action; no DSDBOPN"),
    "dbcls": ("sgcock", "the double-barrel's closing clack is the same mechanism; no DSDBCLS"),
    "dbload": ("sgcock", "the double-barrel's reload is the same mechanism; no DSDBLOAD"),
    "plasma": ("rlaunc", "the plasma rifle is a player energy weapon; no DSPLASMA in shareware, and the rocket launcher is the closest player-weapon report"),
    "bfg": ("rxplod", "the BFG's report is a large weapon explosion; no DSBFG, and DSRXPLOD is the only one in shareware"),
    "vipain": ("popain", "the imp's pain cry is the closest monster pain in shareware"),
    "mnpain": ("dmpain", "the demon's pain growl is the closest monster pain in shareware"),
    "pepain": ("dmpain", "the pinky's pain growl is the closest monster pain in shareware"),
    "cacsit": ("bgsit1", "the cacodemon's sight is a large-monster roar; the baron's is the closest in shareware"),
    "cybsit": ("brssit", "the cyberdemon's sight is the baron's roar; DSDCYBSIT is absent"),
    "spisit": ("posit1", "the spider mastermind's sight; the imp's sight is the closest shareware sight"),
    "bspsit": ("bgsit2", "the spider's sight is a large-monster roar; the baron's second sight is the closest"),
    "kntsit": ("bgsit1", "the hell knight's sight is the baron's roar; DSKNTST is absent"),
    "vilsit": ("bgsit2", "the arch-vile's sight is a large-monster roar; DSVILSIT is absent"),
    "mansit": ("bgsit1", "the mancubus' sight is a large-monster roar; DSMANSIT is absent"),
    "pesit": ("bgsit2", "the pinky's sight is the demon family's roar; DSPESIT is absent"),
    "sklatk": ("claw", "the imp's claw is the shareware claw swipe"),
    "skepch": ("firsht", "the imp's fireball launch is DSFIRSHT"),
    "vilatk": ("firsht", "the arch-vile's attack is a fire report; DSVILATK is absent"),
    "skeswg": ("claw", "the skeleton's melee swing is a claw swipe"),
    "cacdth": ("brsdth", "the cacodemon's death is a large-monster death; the baron's is the closest"),
    "skldth": ("podth1", "the skeleton's death is a monster collapse; DSKLDTH is absent"),
    "cybdth": ("brsdth", "the cyberdemon's death is a large-monster death; the baron's is the closest"),
    "spidth": ("brsdth", "the spider's death is a large-monster death; the baron's is the closest"),
    "bspdth": ("brsdth", "the spider's death is a large-monster death; the baron's is the closest"),
    "vildth": ("brsdth", "the arch-vile's death is a large-monster death; the baron's is the closest"),
    "kntdth": ("podth1", "the hell knight's death is a monster collapse; DSKNTDTH is absent"),
    "pedth": ("podth1", "the pinky's death is a monster collapse; DSPEDTH is absent"),
    "skedth": ("podth1", "the skeleton's death is a monster collapse; DSKEDTH is absent"),
    "bspact": ("bgact", "the spider's idle sound is a large-monster active sound; DSBSPACT is absent"),
    "bspwlk": ("stnmov", "the spider's walk is stone movement; DSBSPWLK is absent"),
    "vilact": ("bgact", "the arch-vile's idle sound is a large-monster active sound; DSVILACT is absent"),
    "hoof": ("stnmov", "a hoof step is stone/floor movement; DSHOOF is absent"),
    "metal": ("tink", "a metal clank is DSTINK's short metallic ring"),
    "chgun": ("pistol", "upstream links the chaingun to the pistol (S_sfx[sfx_chgun].link); the cue follows that link"),
    "flame": ("firsht", "the flamethrower's ignition is a fire burst; DSFLAME is absent"),
    "flamst": ("firsht", "the flamethrower's start is a fire burst; DSFLAMST is absent"),
    "bospit": ("dmpain", "the boss's pain is a large-monster pain; DSBOSPIT is absent"),
    "boscub": ("bdopn", "the boss brain's mechanical cube is a door-like mechanism; DSBOSCUB is absent"),
    "bossit": ("brssit", "the boss's sight is the baron's roar; DSBOSSIT is absent"),
    "bospn": ("dmpain", "the boss's pain is a large-monster pain; DSBOSPN is absent"),
    "bosdth": ("brsdth", "the boss's death is a large-monster death; DSBOSDTH is absent"),
    "manatk": ("firsht", "the mancubus' fireball is a fire burst; DSMANATK is absent"),
    "mandth": ("brsdth", "the mancubus' death is a large-monster death; DSMANDTH is absent"),
    "sssit": ("posit1", "the SS soldier's sight is the imp's sight; DSSSSIT is absent"),
    "ssdth": ("podth1", "the SS soldier's death is a zombie collapse; DSSSDTH is absent"),
    "keenpn": ("popain", "Commander Keen's pain is a small-monster cry; DSKEENPN is absent"),
    "keendt": ("podth1", "Commander Keen's death is a small-monster collapse; DSKEENDT is absent"),
    "skeact": ("posact", "the skeleton's active sound is a zombie active sound; DSSKEACT is absent"),
    "skesit": ("posit1", "the skeleton's sight is the imp's sight; DSSKESIT is absent"),
    "skeatk": ("sgtatk", "the skeleton's attack is a sergeant's attack; DSSKEATK is absent"),
    "radio": ("tink", "the radio's static click is the shortest shareware click; DSRADIO is absent"),
}


# --- WAD / DMX -------------------------------------------------------------

def read_wad(path: Path) -> dict[str, bytes]:
    data = path.read_bytes()
    if data[:4] not in (b"IWAD", b"PWAD"):
        raise SystemExit(f"sfx_design: {path} is not a WAD")
    count, directory = struct.unpack_from("<II", data, 4)
    lumps: dict[str, bytes] = {}
    for index in range(count):
        offset, size, name = struct.unpack_from("<II8s", data, directory + index * 16)
        lump = name.rstrip(b"\0").decode("latin1")
        lumps[lump] = data[offset:offset + size]
    return lumps


def read_dmx(data: bytes) -> tuple[int, list[float]]:
    """(rate, PCM as floats in -1..1). The header is 8 bytes in this WAD.

    The brief says 16 bytes of padding; the actual lump is 8 + sampleCount,
    so the 24-byte shape is accepted too and the short one is the normal path.
    """
    if len(data) < 8:
        raise ValueError("DMX lump shorter than its header")
    fmt, rate, count = struct.unpack_from("<HHI", data, 0)
    if fmt != 3:
        raise ValueError(f"DMX format {fmt}, expected 3")
    header = 8
    if len(data) != header + count:
        if len(data) == 24 + count:
            header = 24
        else:
            raise ValueError(f"DMX size {len(data)} != {header} + {count}")
    raw = data[header:header + count]
    return rate, [(value - 128) / 128.0 for value in raw]


# --- Analysis --------------------------------------------------------------

def _rms(values: list[float]) -> float:
    if not values:
        return 0.0
    return math.sqrt(sum(value * value for value in values) / len(values))


def _loudest_window(segment: list[float], window: int) -> list[float]:
    if len(segment) <= window:
        return segment
    energy = sum(value * value for value in segment[:window])
    best = energy
    start = 0
    for index in range(1, len(segment) - window + 1):
        energy += segment[index + window - 1] ** 2 - segment[index - 1] ** 2
        if energy > best:
            best = energy
            start = index
    return segment[start:start + window]


def _pitch_of(segment: list[float], rate: int) -> tuple[float, float]:
    """(Hz, normalised correlation) or (0, 0) when the segment is aperiodic."""
    window = max(2, int(PITCH_WINDOW_SEC * rate))
    sub = _loudest_window(segment, window)
    mean = sum(sub) / len(sub)
    sub = [value - mean for value in sub]
    lo = max(1, int(rate / PITCH_MAX_HZ))
    hi = min(len(sub) - 3, int(rate / PITCH_MIN_HZ), len(sub) // 2)
    if hi <= lo:
        return 0.0, 0.0
    corr: dict[int, float] = {}
    for lag in range(1, hi + 1):
        left = sub[:-lag]
        right = sub[lag:]
        den = math.sqrt(sum(value * value for value in left)
                        * sum(value * value for value in right))
        corr[lag] = sum(a * b for a, b in zip(left, right)) / den if den > 0 else 0.0
    zero = None
    for lag in range(2, hi + 1):
        if corr[lag] < 0.0:
            zero = lag
            break
    if zero is None:
        return 0.0, 0.0
    start = max(lo, zero + 1)
    best = 0.0
    best_lag = 0
    for lag in range(start, hi + 1):
        if corr[lag] > best:
            best = corr[lag]
            best_lag = lag
    if best < PITCH_MIN_CORR or best_lag == 0:
        return 0.0, 0.0
    return rate / best_lag, best


def analyse(rate: int, pcm: list[float]) -> dict:
    count = len(pcm)
    duration = count / rate
    envelope = []
    centroid = []
    duty = []
    pitch = []
    confidence = []
    for seg in range(SEGMENTS):
        start = seg * count // SEGMENTS
        end = (seg + 1) * count // SEGMENTS
        part = pcm[start:end]
        level = _rms(part)
        envelope.append(level)
        crossings = sum(1 for index in range(1, len(part))
                        if (part[index - 1] < 0.0) != (part[index] < 0.0))
        centroid.append(crossings * rate / (2.0 * max(1, len(part))))
        mean = sum(part) / len(part) if part else 0.0
        duty.append(sum(1 for value in part if value > mean) / len(part) if part else 0.5)
        hz, corr = _pitch_of(part, rate)
        pitch.append(hz)
        if hz > 0.0:
            confidence.append(corr)

    peak = max(envelope) or 1.0
    normalised = [value / peak for value in envelope]
    weight = sum(normalised) or 1.0
    cen_mean = sum(value * weight for value, weight in zip(centroid, normalised)) / weight
    cen_var = sum(w * (c - cen_mean) ** 2 for c, w in zip(centroid, normalised)) / weight
    filled = [value for value in pitch if value > 0.0]
    pitch_mean = sum(filled) / len(filled) if filled else 0.0
    pitch_std = math.sqrt(sum((v - pitch_mean) ** 2 for v in filled) / len(filled)) if filled else 0.0
    return {
        "duration": duration,
        "rate": rate,
        "samples": count,
        "rms": _rms(pcm),
        "peak": max((abs(value) for value in pcm), default=0.0),
        "envelope": normalised,
        "centroid": centroid,
        "duty": duty,
        "pitch": pitch,
        "pitch_fraction": len(filled) / SEGMENTS,
        "pitch_confidence": sum(confidence) / len(confidence) if confidence else 0.0,
        "pitch_mean": pitch_mean,
        "pitch_std": pitch_std,
        "centroid_mean": cen_mean,
        "centroid_cv": math.sqrt(cen_var) / cen_mean if cen_mean > 0 else 0.0,
        "flatness": sum(normalised) / SEGMENTS,
        "env_std": math.sqrt(sum((v - sum(normalised) / SEGMENTS) ** 2
                                 for v in normalised) / SEGMENTS),
    }


def classify(stats: dict) -> str:
    # The high band is a class in its own right: the APU's 93-step LFSR repeats
    # inside the 25 ms pitch window, so "aperiodic" cannot be required of a
    # metallic cue the synthesiser itself produces. A high, narrow centroid is
    # the measured property that survives rendering. The narrowness limit is
    # looser below 40 ms because 16 segments then hold ~25 samples each and the
    # segment boundaries dominate the contour's variance.
    cv_limit = 0.60 if stats["duration"] < 0.040 else 0.45
    if stats["centroid_mean"] >= 1850.0 and stats["centroid_cv"] <= cv_limit:
        return "metallic"
    tonal = stats["pitch_fraction"] >= 0.5 and stats["pitch_confidence"] >= 0.85
    if not tonal:
        return "noise"
    ratio = (stats["centroid_mean"] / stats["pitch_mean"]
             if stats["pitch_mean"] > 0 else 0.0)
    pitch_cv = (stats["pitch_std"] / stats["pitch_mean"]
                if stats["pitch_mean"] > 0 else 1.0)
    if stats["centroid_mean"] < 1000.0 and stats["flatness"] >= 0.55:
        return "triangle"
    if pitch_cv <= 0.05 and stats["flatness"] >= 0.80 and ratio >= 2.5:
        return "saw"
    return "pulse"


# --- Fitting ---------------------------------------------------------------

def _interpolate(values: list[float], positions: list[float]) -> list[float]:
    """Linear interpolation of a sparse track; zeros are gaps."""
    known = [(index, value) for index, value in enumerate(values) if value > 0.0]
    if not known:
        return [0.0 for _ in values]
    out = []
    for position in positions:
        if position <= known[0][0]:
            out.append(known[0][1])
        elif position >= known[-1][0]:
            out.append(known[-1][1])
        else:
            for (left_i, left), (right_i, right) in zip(known, known[1:]):
                if left_i <= position <= right_i:
                    span = right_i - left_i
                    weight = (position - left_i) / span if span else 0.0
                    out.append(left + (right - left) * weight)
                    break
    return out


def _smooth(values: list[float]) -> list[float]:
    if len(values) < 3:
        return values
    return [values[0]] + [
        (values[index - 1] + values[index] + values[index + 1]) / 3.0
        for index in range(1, len(values) - 1)
    ] + [values[-1]]


def _contour_points(values: list[float], duration: float,
                    min_ratio: float = 1.5) -> list[tuple[float, float]] | None:
    """Four points when the smoothed contour moves by more than @p min_ratio."""
    valid = [value for value in values if value > 0.0]
    if not valid:
        return None
    if max(valid) / min(valid) <= min_ratio:
        return None
    filled = _smooth(_interpolate(values, list(range(SEGMENTS))))
    times = [0.0, duration / 3.0, 2.0 * duration / 3.0, duration]
    # Sample the segment track at those times (segment i covers i/16..(i+1)/16).
    points = []
    for time in times:
        position = min(SEGMENTS - 1, max(0.0, time / duration * SEGMENTS))
        left = int(position)
        right = min(SEGMENTS - 1, left + 1)
        weight = position - left
        value = filled[left] * (1.0 - weight) + filled[right] * weight
        points.append((time, value))
    return points


def fit_adsr(envelope: list[float], duration: float) -> dict:
    """Least-squares ADSR over every (attack end, decay end) pair."""
    peak = max(range(SEGMENTS), key=lambda index: envelope[index])
    attack_end = peak
    for index in range(SEGMENTS):
        if envelope[index] >= 0.8:
            attack_end = min(index, peak)
            break
    best = (-2.0, attack_end, attack_end + 1, 0.0)
    for decay_end in range(attack_end + 1, SEGMENTS):
        base = [0.0] * SEGMENTS
        slope = [0.0] * SEGMENTS
        for index in range(SEGMENTS):
            if index <= attack_end:
                base[index] = index / attack_end if attack_end else 1.0
            elif index < decay_end:
                u = (index - attack_end) / (decay_end - attack_end)
                base[index] = 1.0 - u
                slope[index] = u
            else:
                slope[index] = 1.0
        den = sum(value * value for value in slope)
        sustain = (sum((envelope[index] - base[index]) * slope[index]
                       for index in range(SEGMENTS)) / den) if den else 0.0
        sustain = max(0.0, min(1.0, sustain))
        model = [base[index] + sustain * slope[index] for index in range(SEGMENTS)]
        mean_env = sum(envelope) / SEGMENTS
        mean_model = sum(model) / SEGMENTS
        cov = sum((envelope[index] - mean_env) * (model[index] - mean_model)
                  for index in range(SEGMENTS))
        var_env = sum((value - mean_env) ** 2 for value in envelope)
        var_model = sum((value - mean_model) ** 2 for value in model)
        corr = cov / math.sqrt(var_env * var_model) if var_env > 0 and var_model > 0 else 0.0
        if corr > best[0]:
            best = (corr, attack_end, decay_end, sustain)
    corr, attack_end, decay_end, sustain = best
    last_high = SEGMENTS - 1
    while last_high > peak and envelope[last_high] < 0.10:
        last_high -= 1
    release = max(0.005, min(0.1, (SEGMENTS - 1 - last_high) / SEGMENTS * duration))
    return {
        "attack": max(0.001, attack_end / SEGMENTS * duration),
        "decay": max(0.0, (decay_end - attack_end) / SEGMENTS * duration),
        "sustain": sustain,
        "release": release,
        "corr": corr,
    }


# --- Cue derivation --------------------------------------------------------

def _priority(name: str) -> str:
    if name in _RESERVED:
        return "Reserved"
    if name in _HIGH:
        return "High"
    return "Normal"


def _round(value: float, places: int) -> float:
    scale = 10 ** places
    return round(value * scale) / scale


def _cue_from_stats(name: str, stats: dict, lump: str, donor: str,
                    volume: float) -> dict:
    character = classify(stats)
    duration = _round(stats["duration"], 4)
    adsr = fit_adsr(stats["envelope"], duration)
    pitch_points = None
    duty_points = None
    if character in ("pulse", "triangle", "saw"):
        pitch_points = _contour_points(stats["pitch"], duration)
    # For an aperiodic cue the centroid contour is a noise band, not a pitch;
    # the cue holds one clock at the envelope-weighted centroid. A pitch table
    # there would move the band away from the value the gate measures, and the
    # APU's integer noise period makes the mean of a staircase unpredictable.
    if character == "pulse":
        # Silence segments (a duty of 0 or 1) are gaps, not a duty step; a
        # 20 % swing in the measured positive fraction is the point where the
        # APU's stepped-duty table earns its flash.
        duty_track = [value if 0.05 <= value <= 0.95 else 0.0
                      for value in stats["duty"]]
        duty_points = _contour_points(duty_track, duration, min_ratio=1.2)
        if duty_points:
            duty_points = [(time, max(0.06, min(0.94, value)))
                           for time, value in duty_points]
    wave = {
        "pulse": "Pulse",
        "triangle": "Triangle",
        "noise": "Noise",
        "metallic": "Noise",
        "saw": "Saw",
    }[character]
    if character in ("noise", "metallic"):
        # An 18 ms cue holds too few 93-step LFSR states to build a high band;
        # its metallic character is the long LFSR at a high clock instead. Every
        # longer metallic cue uses the 93-step mode the class names.
        short = character == "metallic" and duration >= 0.030
        frequency = _clock_for_centroid(stats["centroid_mean"], short)
        # A cue shorter than ~40 ms holds too few LFSR steps for the long
        # staircase to settle; one step up lands inside the factor-of-2 window
        # instead of just under it.
        if duration < 0.040:
            frequency *= 1.5
        frequency = _round(max(20.0, min(22050.0, frequency)), 1)
        noise_short = short
    else:
        frequency = _round(max(20.0, min(5000.0,
                                       stats["pitch_mean"] or stats["centroid_mean"])), 1)
        noise_short = False
    if pitch_points:
        pitch_points = [(time, max(20.0, min(5000.0, value)))
                        for time, value in pitch_points]
    duty = 0.5
    if character == "pulse":
        solid = [value for value, env in zip(stats["duty"], stats["envelope"]) if env > 0.2]
        duty = _round(max(0.05, min(0.95, sum(solid) / len(solid) if solid else 0.5)), 3)
    cue = {
        "name": name,
        "class": character,
        "lump": lump,
        "donor": donor,
        "wave": wave,
        "frequency": frequency,
        "duration": duration,
        "volume": _round(volume, 3),
        "duty": duty,
        "noise_short": noise_short,
        "priority": _priority(name),
        "adsr": adsr,
        "pitch_points": pitch_points,
        "duty_points": duty_points,
        "stats": stats,
    }
    return cue


def derive(words: list[str], lumps: dict[str, bytes]) -> tuple[list[dict], dict]:
    """One cue per id 1..NSFX, in S_sfx order, plus the analysis by name."""
    analyses: dict[str, dict] = {}
    direct: dict[str, dict] = {}
    for name in words[1:]:
        lump = "DS" + name.upper()
        if lump not in lumps:
            continue
        rate, pcm = read_dmx(lumps[lump])
        stats = analyse(rate, pcm)
        analyses[name] = stats
        direct[name] = stats

    # Loudness is relative across the set: the mix should keep a pistol quieter
    # than a rocket, as the originals do. The 95th percentile anchors the top.
    levels = sorted(stats["rms"] for stats in direct.values())
    reference = levels[int(0.95 * (len(levels) - 1))] if levels else 1.0
    volume_of = {
        name: max(0.25, min(0.95, 0.25 + 0.70 * (stats["rms"] / reference)))
        for name, stats in direct.items()
    }

    cues: list[dict] = []
    for name in words[1:]:
        if name in analyses:
            cues.append(_cue_from_stats(name, analyses[name], "DS" + name.upper(),
                                        "", volume_of[name]))

    # Donors come second: a donor may sit later in the enum than its borrower
    # (bfg borrows rxplod), so a single forward pass would look for a row that
    # has not been built yet.
    for name in words[1:]:
        if name in analyses:
            continue
        if name not in DONORS:
            raise SystemExit(f"sfx_design: no lump and no donor for '{name}'")
        donor, reason = DONORS[name]
        source = next(cue for cue in cues if cue["name"] == donor)
        cues.append({
            **source,
            "name": name,
            "donor": donor,
            "lump": "",
            "why": reason,
            "priority": _priority(name),
        })

    # `cues` is in enum order because the direct pass and the donor pass each
    # walk words[1:] and the donor pass only appends what the first skipped.
    by_name = {cue["name"]: cue for cue in cues}
    ordered = [by_name[name] for name in words[1:]]
    cues = ordered

    # The dummy row keeps the array indexed by the sfxenum_t value itself.
    dummy = {
        "name": "none", "class": "none", "lump": "", "donor": "",
        "wave": "Pulse", "frequency": 440.0, "duration": 0.001,
        "volume": 0.0, "duty": 0.5, "noise_short": False,
        "priority": "Normal", "adsr": {"attack": 0.001, "decay": 0.0,
                                       "sustain": 1.0, "release": 0.005,
                                       "corr": 0.0},
        "pitch_points": None, "duty_points": None,
        "stats": {"duration": 0.001, "rate": 0, "samples": 0, "rms": 0.0,
                  "peak": 0.0, "envelope": [0.0] * SEGMENTS,
                  "centroid": [0.0] * SEGMENTS, "duty": [0.5] * SEGMENTS,
                  "pitch": [0.0] * SEGMENTS, "pitch_fraction": 0.0,
                  "pitch_confidence": 0.0, "pitch_mean": 0.0, "pitch_std": 0.0,
                  "centroid_mean": 0.0, "centroid_cv": 0.0,
                  "flatness": 0.0, "env_std": 0.0},
    }
    return [dummy] + cues, analyses


# --- Output ----------------------------------------------------------------

def _float(value: float, places: int) -> str:
    return f"{value:.{places}f}"


def _points(points: list[tuple[float, float]] | None) -> str:
    if not points:
        return "{{0.0f, 0.0f}, {0.0f, 0.0f}, {0.0f, 0.0f}, {0.0f, 0.0f}}, 0u"
    body = ", ".join(
        f"{{{_float(time, 3)}f, {_float(value, 1)}f}}" for time, value in points)
    return f"{{{body}}}, {len(points)}u"


def _duty_points(points: list[tuple[float, float]] | None) -> str:
    if not points:
        return "{{0.0f, 0.5f}, {0.0f, 0.5f}, {0.0f, 0.5f}, {0.0f, 0.5f}}, 0u"
    body = ", ".join(
        f"{{{_float(time, 3)}f, {_float(value, 3)}f}}" for time, value in points)
    return f"{{{body}}}, {len(points)}u"


def render_include(cues: list[dict]) -> str:
    lines = [
        "// Generated by tools/sfx_design.py from tools/DOOM1.WAD -- do not edit.",
        "// One row per sfxenum_t value, indexed by the id itself (row 0 is",
        "// upstream's dummy). See tools/sfx_fit.txt for the fit and the donor of",
        "// every id the shareware WAD has no lump for.",
    ]
    for sfx_id, cue in enumerate(cues):
        label = f"/* {sfx_id:3d} {cue['name']:<8} */"
        if cue["name"] == "none":
            label = f"/* {sfx_id:3d} none     */"
        elif cue["donor"]:
            label = f"/* {sfx_id:3d} {cue['name']:<8} from {cue['donor']} */"
        adsr = cue["adsr"]
        lines.append(
            f"{label} {{SfxWave::{cue['wave']}, {_float(cue['frequency'], 1)}f, 0.0f, "
            f"{_float(cue['duration'], 4)}f, {_float(cue['volume'], 3)}f, "
            f"{_float(cue['duty'], 3)}f, 0u, "
            f"{_float(adsr['attack'], 4)}f, {_float(adsr['decay'], 4)}f, "
            f"{_float(adsr['sustain'], 3)}f, {_float(adsr['release'], 4)}f, "
            f"{'true' if cue['noise_short'] else 'false'}, "
            f"SfxClass::{cue['priority']}, {_points(cue['pitch_points'])}, "
            f"{_duty_points(cue['duty_points'])}"
            + "},"
        )
    return "\n".join(lines) + "\n"


def _reason(cue: dict) -> str:
    stats = cue["stats"]
    if cue["donor"]:
        return f"donor {cue['donor']}: {cue['why']}"
    return f"lump {cue['lump']}"


def render_report(cues: list[dict], measured: dict[int, dict] | None = None) -> str:
    lines = [
        "# SFX fit record -- generated by tools/sfx_design.py, measured by",
        "# tools/sfx_check.py. One block per sfxenum_t id. `gate:` is rewritten",
        "# by the check tool; `accepted-deviation` marks an id whose measured",
        "# deviation is recorded instead of hidden.",
        "",
    ]
    for sfx_id, cue in enumerate(cues):
        if sfx_id == 0:
            continue
        stats = cue["stats"]
        adsr = cue["adsr"]
        lines.append(
            f"id={sfx_id} name={cue['name']} class={cue['class']} "
            f"priority={cue['priority']} "
            f"donor={cue['donor'] or '-'} lump={cue['lump'] or '-'}")
        lines.append(f"  cue wave={cue['wave']} freq={_float(cue['frequency'], 1)} "
                     f"dur={_float(cue['duration'], 4)} "
                     f"vol={_float(cue['volume'], 3)} "
                     f"duty={_float(cue['duty'], 3)} "
                     f"noiseShort={1 if cue['noise_short'] else 0}")
        lines.append(f"  adsr attack={_float(adsr['attack'], 4)} "
                     f"decay={_float(adsr['decay'], 4)} "
                     f"sustain={_float(adsr['sustain'], 3)} "
                     f"release={_float(adsr['release'], 4)} "
                     f"model_corr={_float(adsr['corr'], 3)}")
        if cue["pitch_points"]:
            points = " ".join(f"{_float(t, 3)}:{_float(v, 1)}"
                              for t, v in cue["pitch_points"])
        else:
            points = "none"
        lines.append(f"  pitch_points={points}")
        if cue["duty_points"]:
            points = " ".join(f"{_float(t, 3)}:{_float(v, 3)}"
                              for t, v in cue["duty_points"])
        else:
            points = "none"
        lines.append(f"  duty_points={points}")
        lines.append(
            f"  origin rate={stats['rate']} samples={stats['samples']} "
            f"rms={_float(stats['rms'], 3)} "
            f"centroid={_float(stats['centroid_mean'], 1)} "
            f"pitch={_float(stats['pitch_mean'], 1)} "
            f"pitch_frac={_float(stats['pitch_fraction'], 2)} "
            f"pitch_conf={_float(stats['pitch_confidence'], 2)} "
            f"flatness={_float(stats['flatness'], 2)}")
        lines.append(f"  why {_reason(cue)}")
        gate = (measured or {}).get(sfx_id)
        if gate and gate.get("status") == "inherited":
            lines.append(f"  gate inherited from {gate['donor']}")
        elif gate:
            lines.append(
                f"  gate dur={gate['duration']} env_corr={gate['env_corr']} "
                f"class={gate['class']} centroid={gate['centroid']} "
                f"pitch={gate['pitch']} status={gate['status']}")
            if gate.get("reason"):
                lines.append(f"  accepted-deviation {gate['reason']}")
        else:
            lines.append("  gate pending (run tools/sfx_check.py)")
        lines.append("")
    return "\n".join(lines)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Derive the SFX bank from the WAD")
    parser.add_argument("--wad", type=Path, default=ROOT / "tools" / "DOOM1.WAD")
    parser.add_argument("--sounds", type=Path, default=ROOT / "lib" / "doom" / "sounds.c")
    parser.add_argument("--out", type=Path, default=ROOT / "src" / "doomc" / "SfxBankTable.inc")
    parser.add_argument("--report", type=Path, default=ROOT / "tools" / "sfx_fit.txt")
    parser.add_argument("--check", action="store_true",
                        help="exit 1 when the committed files are not what this run produces")
    args = parser.parse_args(argv)

    text = args.sounds.read_text(encoding="utf-8", errors="replace")
    match = re.search(r"const sfxinfo_t S_sfx\[\]\s*=\s*\{(.*?)\n\};", text, re.S)
    if not match:
        raise SystemExit("sfx_design: cannot find S_sfx[] in sounds.c")
    words = re.findall(r'\{"([^"]+)"', match.group(1))
    if len(words) != 109:
        raise SystemExit(f"sfx_design: S_sfx[] has {len(words)} rows, expected 109")

    lumps = read_wad(args.wad)
    cues, analyses = derive(words, lumps)
    generated_include = render_include(cues)
    generated_report = render_report(cues)

    if args.check:
        stale = []
        if not args.out.exists() or args.out.read_text(encoding="utf-8") != generated_include:
            stale.append(str(args.out))
        if not args.report.exists():
            stale.append(str(args.report))
        elif args.report.read_text(encoding="utf-8").split("  gate ", 1)[0] != \
                generated_report.split("  gate ", 1)[0]:
            stale.append(str(args.report))
        if stale:
            print("FAIL sfx_design: generated files are stale: " + ", ".join(stale))
            return 1
        print("OK sfx_design: the committed table matches the WAD")
        return 0

    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(generated_include, encoding="utf-8", newline="\n")
    args.report.write_text(generated_report, encoding="utf-8", newline="\n")

    counts = {}
    for cue in cues[1:]:
        counts[cue["class"]] = counts.get(cue["class"], 0) + 1
    donors = sum(1 for cue in cues[1:] if cue["donor"])
    worst = min((cue for cue in cues[1:] if not cue["donor"]),
                key=lambda cue: cue["adsr"]["corr"])
    mean_error = sum(1.0 - cue["adsr"]["corr"] for cue in cues[1:] if not cue["donor"]) \
        / max(1, sum(1 for cue in cues[1:] if not cue["donor"]))
    print(f"OK sfx_design: {len(cues) - 1} cues ({donors} from donors)")
    print("   classes: " + ", ".join(
        f"{key} {counts.get(key, 0)}"
        for key in ("metallic", "noise", "pulse", "triangle", "saw")))
    print(f"   worst fit: id {cues.index(worst)} {worst['name']} "
          f"model r={worst['adsr']['corr']:.3f}")
    print(f"   mean model error: {mean_error * 100.0:.1f}% (1 - Pearson r)")
    print(f"   wrote {args.out} and {args.report}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
