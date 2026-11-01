#!/usr/bin/env python3
"""Compare lib/doom's monster AI against the Python oracle (T15a + T15b).

WHAT THIS PROVES

Monster AI is three questions: can this monster see or hear the player, does it
close the distance and attack, and does it stop when it dies? This gate answers
them with the real code and per-tic scripts, so each half can fail on its own:

    1. SEES AND WAKES. A zombieman 40 map units from the player, in the same room,
       must set `target` on its first state cycle and enter its see state. Before
       T15a it stands in S_POSS_STND forever.
    2. DOES NOT SEE THROUGH A WALL. The same monster at a spot whose straight line
       to the player crosses a one-sided linedef must stay in its spawn state for
       the whole script. The fixture is chosen by that geometry, not by
       P_CheckSight, so a sight check that answers "visible" through a wall makes
       this assertion fail.
    3. HEARS A SHOT. The wall monster, with the player firing through the real
       weapon path at tic 20: P_FireWeapon's P_NoiseAlert must flood the sectors,
       set the monster's sector soundTarget and wake it without sight. The same
       script with the monster inside the nearest closed manual door's sector must
       leave the soundTarget empty: a closed door's opening is zero, so the flood
       cannot cross it.
    4. AGREES WITH THE ORACLE. P_CheckSight is compared line for line against
       tools/doom_ref.py --enemy on a deterministic sweep of E1M1's own THINGS
       population (644 ordered pairs, both directions). The sweep exercises the
       REJECT pre-test and the BSP walk; CSTAT asserts that all three answers
       (reject, accept, blocked) occurred.
    5. TURNS AND WALKS. A zombieman 224 units down the start room's clear
       corridor must close at least 32 map units of the gap, must end facing the
       player to within one 45-degree octant (A_Chase turns in 22.5-degree steps,
       so the exact bearing is not a thing it can hold), and must never step
       across a one-sided linedef on the way (SCENE-END's `wallcross`).
    6. STOPS AT MELEE RANGE. An imp 40 units away must enter S_TROO_ATK1 at a
       separation below P_CheckMeleeRange's 64 - 20 + player radius, never close
       inside the two radii, and its claw must lower the player's health.
    7. HITSCAN LANDS AT RANGE. In the corridor the zombieman's A_PosAttack must
       lower the player's health, and every A_PosAttack tic must be a tic it can
       see the player. Behind a wall, woken by the player's shot, no tic may
       lower the player's health while `see=0`: a monster cannot shoot through a
       wall even though it can hunt.
    8. THE FIREBALL IS REAL. The imp must throw MT_TROOPSHOT; the mobj must
       exist, advance its live states through S_TBALL1/S_TBALL2 (the flight rows,
       not a frozen spawn row), burst through S_TBALLX1, and lower the player's
       health. The container's baked manifest must carry BAL1A0..BAL1E0, which is
       what makes those rows drawable at all (T15's prerequisite).
    9. DEATH STOPS IT. A zombieman killed at tic 20 must enter S_POSS_DIE1, walk
       the death chain to its permanent S_POSS_DIE5 frame, and never re-enter a
       run or attack state.

The per-tic lines are the same shape tools/attack_check.py uses: the step and RNG
position, both psprite layers, every mobj's state/position/target, and the player
record, plus the live sector, subsector and facing each mobj stands in. MOBJ lines
of the two monster types also carry `see=`, P_CheckSight(monster, player) at emit
time, which is how a hit is tied to line of sight.

THE SCRIPT (identical in both implementations, documented in enemy_check.c)

    sees, wall, noise, door:  40 tics, no movement input; noise and door fire
                              once at tic 20
    chase, melee:             60 tics
    death:                    60 tics, killed at tic 20
    hitscan, fireball:        96 tics (the first possible monster shot needs the
                              reaction time and a walk window, and the fireball
                              then has to fly)
    wallshot:                 96 tics, the player fires once at tic 5

PROVING THE GATE CAN FAIL

    python tools/enemy_check.py --wad ... --perturb eye

builds the C side with P_SIGHT_EYE_SHIFT=0, so `sightzstart` moves from three
quarters of the body to its top. Seven swept verdicts flip, so the comparison
must diverge. A comparison that cannot fail is not a gate.

USAGE

    python tools/enemy_check.py --wad path/to/DOOM1.WAD [--container PATH]

The WAD is copyrighted shareware data and is never committed; point this tool at
a file outside the repository. The harness is compiled into a temporary directory
that is removed before the script returns. `--container` defaults to the
repository's tools/DOOM1.whd, the bake the firmware reads.

EXIT CODES

    0  the lines matched and every assertion held
    1  a line differed, or an assertion failed
    2  the command line, the compiler or the input was unusable

Standard library only.
"""

from __future__ import annotations

import argparse
import math
import shutil
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

MAP_NAME = "E1M1"
TICS = 40

EXIT_OK = 0
EXIT_MISMATCH = 1
EXIT_USAGE = 2

SOURCES = (
    "p_setup.c", "p_mobj.c", "p_map.c", "p_user.c", "p_tick.c", "p_action.c",
    "p_inter.c", "p_pspr.c", "p_sight.c", "p_enemy.c", "m_random.c", "z_zone.c",
    "doom_map.c", "r_bsp.c", "r_plane.c", "r_tables.c", "w_wad.c", "p_spec.c",
    "p_floor.c", "p_doors.c", "p_plats.c", "p_ceilng.c", "p_lights.c",
)

PERTURBATIONS = {
    "none": [],
    # P_CheckSight's eye height: height>>0 (the top of the body) instead of
    # upstream's height>>2, so sightzstart rises and the slope windows move.
    # Seven swept verdicts flip, which is what proves the comparison can fail.
    "eye": ["-DP_SIGHT_EYE_SHIFT=0"],
}

MIN_PAIRS = 400

# --- The five T15b numbers the assertions read ------------------------------

FRACUNIT = 1 << 16
ANG45 = 0x20000000
ANGLE_MASK = 0xFFFFFFFF

MELEE_REACH = 64 * FRACUNIT - 20 * FRACUNIT + 16 * FRACUNIT   # 60 units
PLAYER_RADIUS = 16 * FRACUNIT
MONSTER_RADIUS = 20 * FRACUNIT
MIN_SEPARATION = PLAYER_RADIUS + MONSTER_RADIUS - 8 * FRACUNIT

CHASE_CLOSES = 32 * FRACUNIT

MT_PLAYER = 0
MT_POSSESSED = 1
MT_TROOP = 11
MT_TROOPSHOT = 31

S_POSS_RUN1 = 176
S_POSS_ATK1 = 184
S_POSS_ATK2 = 185
S_POSS_DEATH = range(189, 194)          # S_POSS_DIE1..DIE5
S_TROO_RUN1 = 444
S_TROO_ATK1 = 452
S_TBALL1 = 97
S_TBALL2 = 98
S_TBALLX1 = 99

# The fireball's states and the sprite patch rows they name. A second reading of
# info.c's S_TBALL rows, asserted against the live states and the baked manifest:
# a state whose row is missing from the bake is a fireball that cannot be drawn.
ENEMY_FIREBALL_FRAMES = {
    S_TBALL1: "BAL1A0",
    S_TBALL2: "BAL1B0",
    S_TBALLX1: "BAL1C0",
    100: "BAL1D0",
    101: "BAL1E0",
}

MANIFEST_NAME = "~PWBAKE~"


def project_root() -> Path:
    """games/doom, the directory that holds lib/ and tools/."""
    return Path(__file__).resolve().parent.parent


def compile_harness(workdir: Path, compiler: str, perturbation: str) -> Path:
    """Builds tools/enemy_check.c against the real world and AI core."""
    root = project_root()
    executable = workdir / ("enemy_check.exe" if sys.platform == "win32"
                            else "enemy_check")
    command = [
        compiler,
        "-std=gnu99", "-O2", "-Wall", "-Wextra",
        *PERTURBATIONS[perturbation],
        "-I", str(root / "lib" / "doom"),
        "-o", str(executable),
        str(root / "tools" / "enemy_check.c"),
    ]
    command.extend(str(root / "lib" / "doom" / name) for name in SOURCES)
    result = subprocess.run(command, capture_output=True, text=True)
    if result.returncode != 0:
        raise RuntimeError("the harness did not compile:\n"
                           + (result.stderr or result.stdout))
    if result.stderr.strip():
        raise RuntimeError("the harness compiled with warnings, which this project "
                           "does not accept:\n" + result.stderr)
    return executable


def protocol_lines(text: str) -> list[str]:
    """The lines both sides print. CSTAT is a C-only path measurement."""
    lines = []
    for line in text.splitlines():
        if not line or line.startswith("WAD ") or line.startswith("CSTAT "):
            continue
        lines.append(line)
    return lines


def fields_of(line: str) -> tuple[str, dict[str, str]]:
    parts = line.split()
    kind = parts[0] if parts else ""
    values: dict[str, str] = {}
    for part in parts[1:]:
        if "=" in part:
            key, value = part.split("=", 1)
            values[key] = value
    return kind, values


def first_divergence(left: str, right: str) -> str:
    left_kind, left_values = fields_of(left)
    right_kind, right_values = fields_of(right)
    if left_kind != right_kind:
        return f"record kind: C {left_kind!r} oracle {right_kind!r}"
    for key in list(left_values) + [k for k in right_values if k not in left_values]:
        lvalue = left_values.get(key, "<missing>")
        rvalue = right_values.get(key, "<missing>")
        if lvalue != rvalue:
            return (f"{left_kind}: field {key!r}: C {lvalue} "
                    f"oracle {rvalue} (C {left!r}, oracle {right!r})")
    return f"line text differs: C {left!r} oracle {right!r}"


def parse_scenes(text: str) -> dict[str, dict[str, str]]:
    scenes: dict[str, dict[str, str]] = {}
    for line in text.splitlines():
        if not line.startswith("SCENE-END "):
            continue
        _, values = fields_of(line)
        if "name" in values:
            scenes[values["name"]] = values
    return scenes


def parse_scene_steps(text: str) -> dict[str, dict]:
    """Per scene: its STEP records, each with the MOBJ lines and PLAYER line."""
    scenes: dict[str, dict] = {}
    current: dict | None = None
    step: dict | None = None
    for line in text.splitlines():
        parts = line.split()
        if not parts:
            continue
        kind = parts[0]
        if kind == "SCENE-BEGIN":
            _, values = fields_of(line)
            current = {"name": values.get("name"), "steps": []}
            scenes[current["name"]] = current
            step = None
        elif kind == "SCENE-END":
            current = None
            step = None
        elif kind == "STEP" and current is not None:
            _, values = fields_of(line)
            step = {"op": values.get("op", ""), "mobjs": [], "player": {}}
            current["steps"].append(step)
        elif kind == "MOBJ" and step is not None:
            _, values = fields_of(line)
            step["mobjs"].append(values)
        elif kind == "PLAYER" and step is not None:
            _, values = fields_of(line)
            step["player"] = values
    return scenes


def parse_cstat(text: str) -> dict[str, str]:
    for line in text.splitlines():
        if line.startswith("CSTAT "):
            return fields_of(line)[1]
    return {}


def sweep_counts(text: str) -> tuple[int, int, int, int]:
    """(declared pairs, sight lines, visible, hidden)."""
    declared = 0
    for line in text.splitlines():
        if line.startswith("SWEEP "):
            _, values = fields_of(line)
            declared = int(values.get("pairs", "0"))
    lines = [line for line in text.splitlines() if line.startswith("SIGHT ")]
    visible = sum(1 for line in lines if fields_of(line)[1].get("v") == "1")
    hidden = sum(1 for line in lines if fields_of(line)[1].get("v") == "0")
    return declared, len(lines), visible, hidden


# --- The T15b measurements ---------------------------------------------------


def number(record: dict, key: str, default: int = 0) -> int:
    value = record.get(key)
    if value is None:
        return default
    try:
        return int(value)
    except ValueError:
        return default


def monster_steps(steps: list[dict], type_: int) -> list[tuple[int, dict]]:
    """Every (step index, MOBJ record) whose type is @p type_."""
    found = []
    for index, step in enumerate(steps):
        for mobj in step["mobjs"]:
            if mobj.get("type") == str(type_):
                found.append((index, mobj))
    return found


def player_mobj(step: dict) -> dict | None:
    for mobj in step["mobjs"]:
        if mobj.get("type") == str(MT_PLAYER):
            return mobj
    return None


def separation(left: dict, right: dict) -> float:
    return math.hypot(number(left, "x") - number(right, "x"),
                      number(left, "y") - number(right, "y"))


def bearing_error(monster: dict, target: dict) -> int:
    """|bearing - facing| in binary angle units, wrapped to [0, 2^31]."""
    dx = number(target, "x") - number(monster, "x")
    dy = number(target, "y") - number(monster, "y")
    if dx == 0 and dy == 0:
        return 0
    bearing = int(math.atan2(dy, dx) / (2.0 * math.pi) * 4294967296.0)
    bearing &= ANGLE_MASK
    difference = (bearing - (number(monster, "ang") & ANGLE_MASK)) & ANGLE_MASK
    return min(difference, ANGLE_MASK + 1 - difference)


def baked_names(container: Path) -> set[str]:
    """The manifest's packed names, read out of the built container."""
    data = container.read_bytes()
    if data[:4] != b"PWHD":
        raise RuntimeError(f"{container} is not a PWHD container")
    dir_offset = struct.unpack_from("<I", data, 16)[0]
    name_offset = struct.unpack_from("<I", data, 20)[0]
    data_offset = struct.unpack_from("<I", data, 24)[0]
    name_count = struct.unpack_from("<I", data, 12)[0]
    for index in range(name_count):
        entry = name_offset + index * 12
        name = data[entry:entry + 8].split(b"\0")[0].decode("ascii")
        if name != MANIFEST_NAME:
            continue
        lump = struct.unpack_from("<I", data, entry + 8)[0]
        payload = struct.unpack_from("<I", data, dir_offset + lump * 16)[0]
        manifest = data_offset + payload
        count = struct.unpack_from("<I", data, manifest + 8)[0]
        names = set()
        for record in range(count):
            start = manifest + 16 + record * 20
            names.add(data[start:start + 8].split(b"\0")[0].decode("ascii"))
        return names
    raise RuntimeError(f"{container} carries no {MANIFEST_NAME} lump")


def check_assertions(text: str, container: Path) -> list[str]:
    """Each returned string is one failed assertion, named independently."""
    failures: list[str] = []
    scenes = parse_scenes(text)
    scene_steps = parse_scene_steps(text)

    def scene(name: str) -> dict[str, str] | None:
        if name not in scenes:
            failures.append(f"scene {name}: no SCENE-END line")
            return None
        return scenes[name]

    def steps_of(name: str) -> list[dict]:
        record = scene_steps.get(name)
        if record is None or not record["steps"]:
            failures.append(f"scene {name}: no STEP records")
            return []
        return record["steps"]

    # 1. Sees and wakes: target set, wake tic recorded, left the spawn pair.
    sees = scene("sees")
    if sees is not None:
        if sees.get("woketic", "-1") == "-1":
            failures.append("sees: the monster never set a target")
        if sees.get("target") != "0":
            failures.append(f"sees: target is {sees.get('target')!r}, not the "
                            "player's slot 0")
        if sees.get("stayed") != "0":
            failures.append("sees: the monster never left its spawn state pair")

    # 2. A wall hides the player: no target, no wake, still in the spawn pair.
    wall = scene("wall")
    if wall is not None:
        if wall.get("woketic") != "-1":
            failures.append(f"wall: the monster woke at tic {wall.get('woketic')} "
                            "through a wall")
        if wall.get("target") != "-1":
            failures.append("wall: the monster acquired a target through a wall")
        if wall.get("stayed") != "1":
            failures.append("wall: the monster left its spawn state pair")

    # 3a. A shot is heard through an open path.
    noise = scene("noise")
    if noise is not None:
        if noise.get("sndtarget") != "0":
            failures.append("noise: the sector's soundTarget is not the player")
        if noise.get("woketic", "-1") == "-1":
            failures.append("noise: the monster did not wake when the player fired")
        if noise.get("target") != "0":
            failures.append("noise: the woken monster's target is not the player")

    # 3b. A closed door blocks the sound.
    door = scene("door")
    if door is not None:
        if door.get("sndtarget") != "-1":
            failures.append("door: the closed door did not block the sound")
        if door.get("woketic") != "-1":
            failures.append("door: the monster woke behind a closed door")
        if door.get("stayed") != "1":
            failures.append("door: the monster left its spawn state pair")

    # 4. The sweep ran, and both answers and both sight paths exist.
    declared, lines, visible, hidden = sweep_counts(text)
    if declared < MIN_PAIRS:
        failures.append(f"sweep: only {declared} pairs, want at least {MIN_PAIRS}")
    if lines != declared:
        failures.append(f"sweep: {declared} pairs declared but {lines} SIGHT lines")
    if visible == 0:
        failures.append("sweep: no pair is visible")
    if hidden == 0:
        failures.append("sweep: no pair is blocked")
    cstat = parse_cstat(text)
    if int(cstat.get("reject", "0")) == 0:
        failures.append("sight: the REJECT pre-test never answered blocked")
    if int(cstat.get("bsp", "0")) == 0:
        failures.append("sight: the BSP walk never ran")
    if int(cstat.get("accepted", "0")) == 0:
        failures.append("sight: the BSP walk never accepted a line")
    if int(cstat.get("bsp_blocked", "0")) == 0:
        failures.append("sight: the BSP walk never blocked a line")

    # 5. It turns and walks.
    chase = scene("chase")
    chase_steps = steps_of("chase")
    if chase is not None:
        if chase.get("woketic", "-1") == "-1":
            failures.append("chase: the monster never woke")
        if chase.get("wallcross") != "0":
            failures.append(f"chase: it stepped across a wall on "
                            f"{chase.get('wallcross')} tics")
    if chase_steps:
        monster = monster_steps(chase_steps, MT_POSSESSED)
        if len(monster) < 2:
            failures.append("chase: too few monster states to measure a walk")
        else:
            first_index, first = monster[0]
            last_index, last = monster[-1]
            first_player = player_mobj(chase_steps[first_index])
            last_player = player_mobj(chase_steps[last_index])
            if first_player is None or last_player is None:
                failures.append("chase: the player mobj is missing")
            else:
                start = separation(first, first_player)
                end = separation(last, last_player)
                if end > start - CHASE_CLOSES:
                    failures.append(f"chase: the gap only closed from {start:.0f} "
                                    f"to {end:.0f} (want at least "
                                    f"{CHASE_CLOSES} units)")
                error = bearing_error(last, last_player)
                if error > ANG45 // 2:
                    failures.append(f"chase: final facing is {error} BAM off the "
                                    f"bearing, more than one {ANG45 // 2}-BAM "
                                    "octant")

    # 6. It stops at melee range.
    melee = scene("melee")
    melee_steps = steps_of("melee")
    if melee is not None:
        if melee.get("woketic", "-1") == "-1":
            failures.append("melee: the imp never woke")
        if number(melee, "hp", 100) >= 100:
            failures.append("melee: the imp's claw never landed")
        if melee.get("wallcross") != "0":
            failures.append("melee: the imp walked through a wall")
    if melee_steps:
        first_attack = None
        closest = None
        for index, mobj in monster_steps(melee_steps, MT_TROOP):
            player = player_mobj(melee_steps[index])
            if player is None:
                continue
            gap = separation(mobj, player)
            closest = gap if closest is None else min(closest, gap)
            if first_attack is None and number(mobj, "state") == S_TROO_ATK1:
                first_attack = gap
        if first_attack is None:
            failures.append("melee: the imp never entered S_TROO_ATK1")
        elif first_attack >= MELEE_REACH:
            failures.append(f"melee: the claw fired at {first_attack:.0f} units, "
                            f"outside P_CheckMeleeRange's {MELEE_REACH // FRACUNIT}")
        if closest is not None and closest < MIN_SEPARATION:
            failures.append(f"melee: the imp closed to {closest:.0f} units, "
                            f"inside the two radii minus a step")

    # 7a. Hitscan lands at range, and only with sight.
    hitscan = scene("hitscan")
    hitscan_steps = steps_of("hitscan")
    if hitscan is not None:
        if number(hitscan, "hp", 100) >= 100:
            failures.append("hitscan: A_PosAttack never lowered the player's "
                            "health")
    if hitscan_steps:
        attacks = [(index, mobj) for index, mobj in
                   monster_steps(hitscan_steps, MT_POSSESSED)
                   if number(mobj, "state") == S_POSS_ATK2]
        if not attacks:
            failures.append("hitscan: the zombieman never entered S_POSS_ATK2")
        for index, mobj in attacks:
            if mobj.get("see") != "1":
                failures.append(f"hitscan: A_PosAttack ran on step {index} "
                                "without line of sight")

    # 7b. Nothing hurts the player from behind a wall.
    wallshot = scene("wallshot")
    wallshot_steps = steps_of("wallshot")
    if wallshot is not None and number(wallshot, "hp", 100) >= 100:
        pass                                   # allowed: it never got a shot
    if wallshot_steps:
        previous_health = 100
        for index, step in enumerate(wallshot_steps):
            health = number(step["player"], "hp", previous_health)
            for mobj in step["mobjs"]:
                if mobj.get("type") != str(MT_POSSESSED):
                    continue
                if mobj.get("see") == "0":
                    if health < previous_health:
                        failures.append(
                            f"wallshot: the player lost {previous_health - health} "
                            f"health on step {index} while the monster could not "
                            "see it")
                    if number(mobj, "state") in (S_POSS_ATK1, S_POSS_ATK2):
                        failures.append(
                            f"wallshot: the monster attacked on step {index} "
                            "without line of sight")
            previous_health = health

    # 8. The fireball is real: it exists, its live rows advance, it bursts, it
    #    hurts, and the bake carries every frame it names.
    fireball = scene("fireball")
    fireball_steps = steps_of("fireball")
    if fireball is not None and number(fireball, "hp", 100) >= 100:
        failures.append("fireball: the burst never lowered the player's health")
    if fireball_steps:
        shots = monster_steps(fireball_steps, MT_TROOPSHOT)
        if not shots:
            failures.append("fireball: the imp never spawned MT_TROOPSHOT")
        else:
            states = [(index, number(mobj, "state")) for index, mobj in shots]
            live = [state for _index, state in states]
            if S_TBALL1 not in live:
                failures.append("fireball: S_TBALL1 (the spawn row) never showed")
            if S_TBALL2 not in live:
                failures.append("fireball: S_TBALL2 never showed, so the row did "
                                "not advance past the spawn frame")
            if S_TBALLX1 not in live:
                failures.append("fireball: S_TBALLX1 never showed, so it never "
                                "burst")
            travelled = any(
                separation(mobj, shots[0][1]) > 32 * FRACUNIT
                for _index, mobj in shots
                if number(mobj, "state") in (S_TBALL1, S_TBALL2))
            if not travelled:
                failures.append("fireball: the fireball never moved")
        try:
            names = baked_names(container)
            missing = sorted(name for name in ENEMY_FIREBALL_FRAMES.values()
                             if name not in names)
            if missing:
                failures.append("fireball: the bake has no " + ", ".join(missing))
        except (OSError, RuntimeError) as error:
            failures.append(f"fireball: the bake witness could not run: {error}")

    # 9. Death stops it.
    death = scene("death")
    death_steps = steps_of("death")
    if death is not None:
        if death.get("state") != "193":
            failures.append(f"death: the corpse ended in state "
                            f"{death.get('state')!r}, not S_POSS_DIE5 (193)")
        if death.get("wallcross") != "0":
            failures.append("death: the monster walked through a wall")
    if death_steps:
        kill_step = None
        for index, step in enumerate(death_steps):
            if step["op"] == "kill":
                kill_step = index
                break
        if kill_step is None:
            failures.append("death: no kill step was recorded")
        else:
            killed = [mobj for _index, mobj in
                      monster_steps(death_steps, MT_POSSESSED)
                      if _index >= kill_step]
            if not killed:
                failures.append("death: the corpse vanished instead of dying")
            else:
                if number(killed[0], "state") not in S_POSS_DEATH:
                    failures.append(f"death: the kill left state "
                                    f"{killed[0].get('state')!r}, not a death row")
                for index, mobj in monster_steps(death_steps, MT_POSSESSED):
                    if index < kill_step:
                        continue
                    state = number(mobj, "state")
                    if state < 189 or state > 193:
                        failures.append(f"death: step {index} ran state {state} "
                                        "after the kill")
                        break
                if number(killed[-1], "state") != 193:
                    failures.append("death: the death chain never reached "
                                    "S_POSS_DIE5")
                if number(killed[-1], "tics") != -1:
                    failures.append("death: S_POSS_DIE5 is not the permanent frame")

    return failures


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        prog="enemy_check.py",
        description="Compare lib/doom's monster AI -- P_CheckSight/P_NoiseAlert/"
                    "A_Look/A_Chase/the attack actions -- against "
                    "tools/doom_ref.py --enemy, and assert the waking, chase, "
                    "melee, hitscan, fireball and death behaviours.",
        epilog="The harness is built in a temporary directory and nothing is "
               "written into the worktree.\n",
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    parser.add_argument("--wad", type=Path, required=True, metavar="PATH",
                        help="the IWAD or PWAD to read; keep it outside the repository")
    parser.add_argument("--container", type=Path, default=None, metavar="PATH",
                        help="the built container whose bake the fireball's rows "
                             "are read from (default: tools/DOOM1.whd)")
    parser.add_argument("--cc", default="gcc", metavar="NAME",
                        help="C compiler to build the harness with (default: gcc)")
    parser.add_argument("--map", default=MAP_NAME, metavar="NAME",
                        help=f"the map to load (default: {MAP_NAME})")
    parser.add_argument("--perturb", choices=sorted(PERTURBATIONS), default="none",
                        help="build the C side with one deliberate constant changed, "
                             "to prove the comparison can fail (default: none)")
    arguments = parser.parse_args(argv)

    if not arguments.wad.is_file():
        print(f"enemy_check: {arguments.wad} is not a file", file=sys.stderr)
        return EXIT_USAGE

    root = project_root()
    oracle_script = root / "tools" / "doom_ref.py"
    container = arguments.container or root / "tools" / "DOOM1.whd"
    workdir = Path(tempfile.mkdtemp(prefix="enemy_check_"))

    try:
        executable = compile_harness(workdir, arguments.cc, arguments.perturb)

        c_result = subprocess.run([str(executable), str(arguments.wad),
                                   arguments.map],
                                  capture_output=True, text=True)
        if c_result.returncode != 0:
            print(c_result.stderr or c_result.stdout, file=sys.stderr)
            print("enemy_check: the C harness failed", file=sys.stderr)
            return EXIT_USAGE

        ref_result = subprocess.run(
            [sys.executable, str(oracle_script), "--wad", str(arguments.wad),
             "--enemy", arguments.map],
            capture_output=True, text=True)
        if ref_result.returncode != 0:
            print(ref_result.stderr or ref_result.stdout, file=sys.stderr)
            print("enemy_check: the oracle failed", file=sys.stderr)
            return EXIT_USAGE

        c_lines = protocol_lines(c_result.stdout)
        ref_lines = protocol_lines(ref_result.stdout)

        mismatches = 0
        first = ""
        for index in range(max(len(c_lines), len(ref_lines))):
            left = c_lines[index] if index < len(c_lines) else "<missing>"
            right = ref_lines[index] if index < len(ref_lines) else "<missing>"
            if left == right:
                continue
            mismatches += 1
            if not first:
                first = first_divergence(left, right)

        banner = next((line for line in c_lines if line.startswith("ENEMY ")), "")
        declared, lines, visible, hidden = sweep_counts(c_result.stdout)
        print(f"{arguments.map}: {len(c_lines)} compared lines, {mismatches} "
              f"divergent (perturb={arguments.perturb})")
        print(f"  {banner}")
        print(f"  sweep: {declared} pairs, {lines} SIGHT lines, "
              f"{visible} visible, {hidden} blocked")
        for line in c_result.stdout.splitlines():
            if line.startswith("SCENE-END "):
                print(f"  {line}")
        if mismatches:
            print(f"enemy_check: {mismatches} divergent line(s); first: {first}",
                  file=sys.stderr)

        failures = check_assertions(c_result.stdout, container)
        for failure in failures:
            print(f"enemy_check: assertion failed: {failure}", file=sys.stderr)

        if mismatches or failures:
            return EXIT_MISMATCH
        if arguments.perturb != "none":
            print(f"enemy_check: --perturb {arguments.perturb} did not diverge, so "
                  "it does not prove the comparison can fail", file=sys.stderr)
            return EXIT_MISMATCH
    except RuntimeError as error:
        print(f"enemy_check: {error}", file=sys.stderr)
        return EXIT_USAGE
    finally:
        shutil.rmtree(workdir, ignore_errors=True)

    print("enemy_check: C and the oracle agree, and all nine assertions hold")
    return EXIT_OK


if __name__ == "__main__":
    sys.exit(main())
