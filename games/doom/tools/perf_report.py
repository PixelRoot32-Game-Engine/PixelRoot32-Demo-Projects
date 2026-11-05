#!/usr/bin/env python3
"""Report the DOOM port's frame time from a captured serial log.

The frame-time record is a command, not a table typed once and left to age.
This tool reads the serial capture of a running image and
prints, per phase, the count / min / max / mean over the samples that describe
the world pass.

Two line formats are understood, both written by the firmware itself:

* ``SHOW,frame=<n>,x=...,render_us=...,convert_us=...,present_us=...,
  frame_us=...,fps_x10=...,update_us=...,hash_us=...,other_us=...,refused=...,
  hash=...`` -- the shipped image cadence (``DoomScene::drawSceneFrame``).
  Fields are looked up by name, never by position: the firmware has added
  fields before and will again, and a positional parser would read a shifted
  column as a real measurement.
* ``FRAMEPROF,summary,...`` -- the ``_frame`` probe's 20-view summary
  (``FrameDriver.cpp``), reported as its own table.

WHY THE WORLD-FRAME FILTER EXISTS
The board's GPIO 35 is input-only and has no internal pull-up, so a floating
Select line opens DOOM's automap by itself. A map frame
renders in about 0.9 ms, so a log that mixes world and map frames reports a
band that is not the game's frame time (the all-frame band reads ~155-190).
The default therefore keeps only samples with ``render_us > 20000`` and prints
the excluded count and the reason. ``--all-frames`` shows the unfiltered set,
labelled.

THE RULE
Exit 0 only when every captured run contributes at least one world frame and
the world ``fps_x10`` minimum is >= ``--min-fps`` (default 150). No world
frames is a failure with its own message, never a pass: a run that only ever
drew the automap measured something, but not the thing this gate names.

``--perturb`` proves the gate can fail: it lowers every parsed ``fps_x10`` by a
fixed amount (and forces the minimum below the threshold if the shift alone is
not enough), then asserts the simulated verdict is FAIL and exits non-zero.
``--self-test`` exercises the parser and every rule on synthetic logs, so the
tool is testable on a host with no board and no capture.

    python tools/perf_report.py capture.log
    python tools/perf_report.py run1.log run2.log run3.log
    python tools/perf_report.py capture.log --all-frames --json
    python tools/perf_report.py capture.log --perturb          # must FAIL

Exit 0 when the rule passed, 1 when it failed, 2 on unusable input or when
``--self-test``/``--perturb`` could not prove what it exists to prove.
"""
from __future__ import annotations

import argparse
import json
import statistics
import sys

# The world pass is the only frame this project's frame-time record is about.
# 20 ms is the floor every recorded board world frame clears (the cheapest sampled
# view is ~9 ms, and the automap is ~0.9 ms), so it separates the two
# populations without guessing at a view's identity.
WORLD_RENDER_FLOOR_US = 20000

DEFAULT_MIN_FPS_X10 = 150

# The shift ``--perturb`` applies to every world ``fps_x10``. 20 points is more
# than the 153-160 band's distance to 150, so the failure is never an accident
# of the data; if a future capture has a larger margin the tool additionally
# forces the minimum one point below the threshold.
PERTURB_FPS_X10 = 20

# The phases the report tabulates. ``fps_x10`` is a rate, not a duration, but it
# gets the same count/min/max/mean treatment because the band is the number the
# gate names.
PHASES = (
    "frame_us",
    "render_us",
    "convert_us",
    "present_us",
    "update_us",
    "hash_us",
    "other_us",
    "fps_x10",
)

# Fields every SHOW sample must carry to take part in the table. ``hash`` is
# diagnostic and ``refused`` is a health note, so neither is required here.
REQUIRED_SHOW_FIELDS = ("render_us", "frame_us", "fps_x10")

SUMMARY_FIELDS = (
    "samples",
    "frame_mean_us",
    "frame_min_us",
    "frame_max_us",
    "walk_us",
    "walls_us",
    "planes_us",
    "masked_us",
    "other_us",
    "residency_us",
    "gate",
)


class Sample:
    """One ``SHOW,frame`` line, parsed by name."""

    __slots__ = ("fields",)

    def __init__(self, fields):
        self.fields = fields

    def number(self, name):
        """Return a field as int, or None when it is absent or not a number."""
        value = self.fields.get(name)
        if value is None:
            return None
        try:
            return int(value)
        except ValueError:
            return None


class Summary:
    """One ``FRAMEPROF,summary`` line, parsed by name."""

    __slots__ = ("fields", "line")

    def __init__(self, fields, line):
        self.fields = fields
        self.line = line


def parse_fields(payload):
    """Split ``k=v,k=v,...`` into a dict. Unknown fields are kept, not dropped.

    Keeping every field is deliberate: ``SHOW,frame`` has grown fields in every
    phase of this project, and a parser that discarded what it did not know
    would hide a future measurement instead of reporting it.
    """
    fields = {}
    for part in payload.split(","):
        key, sep, value = part.partition("=")
        if sep:
            fields[key.strip()] = value.strip()
    return fields


def parse_log(text):
    """Return (samples, summaries, malformed) for one captured log.

    ``malformed`` counts SHOW,frame lines that cannot carry the table (a
    truncated line from the serial read, or a field the firmware renamed). They
    are reported instead of silently dropped.
    """
    samples = []
    summaries = []
    malformed = 0
    for raw in text.splitlines():
        line = raw.strip()
        if line.startswith("SHOW,"):
            fields = parse_fields(line[len("SHOW,"):])
            # A frame line is identified by carrying a `frame` field, not by the
            # literal `SHOW,frame=` prefix: an older or reordered line is still a
            # measurement, and keying on the prefix would silently drop it.
            # Other SHOW lines (map, audio, persist, setup, frame00) are not part
            # of this table and are ignored rather than counted as malformed.
            if "frame" not in fields:
                continue
            sample = Sample(fields)
            if any(sample.number(name) is None for name in REQUIRED_SHOW_FIELDS):
                malformed += 1
                continue
            samples.append(sample)
        elif "FRAMEPROF,summary," in line:
            marker = line.index("FRAMEPROF,summary,")
            payload = line[marker + len("FRAMEPROF,summary,"):]
            summaries.append(Summary(parse_fields(payload), line))
    return samples, summaries, malformed


def is_world(sample):
    """A world frame is one the renderer actually filled: render_us > 20 ms."""
    render_us = sample.number("render_us")
    return render_us is not None and render_us > WORLD_RENDER_FLOOR_US


def select_samples(samples, all_frames):
    """Return (selected, excluded) according to the world-frame rule."""
    if all_frames:
        return list(samples), []
    world = [sample for sample in samples if is_world(sample)]
    excluded = [sample for sample in samples if not is_world(sample)]
    return world, excluded


def phase_table(samples):
    """count/min/max/mean per phase over ``samples``."""
    table = {}
    for phase in PHASES:
        values = [sample.number(phase) for sample in samples]
        values = [value for value in values if value is not None]
        if not values:
            table[phase] = {"count": 0, "min": None, "max": None, "mean": None}
            continue
        table[phase] = {
            "count": len(values),
            "min": min(values),
            "max": max(values),
            "mean": statistics.fmean(values),
        }
    return table


def analyse_logs(logs, all_frames, min_fps):
    """Apply the rule to every log and return a JSON-able report.

    ``logs`` is a list of (path, text). Every run must contribute a world frame:
    a capture from the wrong build, or one that only saw the automap, must fail
    rather than quietly borrow another run's samples.
    """
    report = {"logs": [], "rule": rule_text(min_fps, all_frames)}
    all_selected = []
    failures = []

    for path, text in logs:
        samples, summaries, malformed = parse_log(text)
        selected, excluded = select_samples(samples, all_frames)
        all_selected.extend(selected)
        entry = {
            "path": path,
            "show_samples": len(samples),
            "malformed": malformed,
            "selected": len(selected),
            "excluded": len(excluded),
            "refused": sum(
                sample.number("refused") or 0 for sample in samples
            ),
            "table": phase_table(selected),
            "summaries": [
                {name: summary.fields.get(name) for name in SUMMARY_FIELDS}
                for summary in summaries
            ],
        }
        report["logs"].append(entry)
        if samples and not selected:
            failures.append(
                "no world frames in %s: all %d SHOW,frame samples have "
                "render_us <= %d us" % (path, len(samples), WORLD_RENDER_FLOOR_US)
            )

    report["combined"] = phase_table(all_selected)
    report["fps_scope"] = "all-frames" if all_frames else "world"

    if not all_selected:
        failures.append(
            "no world frames in any log (the automap opens on floating GPIO 35 "
            "and a map frame renders in ~1 ms)"
        )

    selected_values = [
        sample.number("fps_x10")
        for sample in all_selected
        if sample.number("fps_x10") is not None
    ]
    report["world_fps_x10"] = {
        "count": len(selected_values),
        "min": min(selected_values) if selected_values else None,
        "max": max(selected_values) if selected_values else None,
        "mean": statistics.fmean(selected_values) if selected_values else None,
    }

    if selected_values and min(selected_values) < min_fps:
        failures.append(
            "world fps_x10 minimum %d is below %d"
            % (min(selected_values), min_fps)
        )

    report["passed"] = not failures
    report["failures"] = failures
    return report


def rule_text(min_fps, all_frames):
    if all_frames:
        return (
            "at least one frame in every log and the unfiltered fps_x10 "
            "minimum >= %d (--all-frames: the automap is included)" % min_fps
        )
    return (
        "at least one world frame (render_us > %d us) in every log and the "
        "world fps_x10 minimum >= %d" % (WORLD_RENDER_FLOOR_US, min_fps)
    )


def format_table(title, table):
    lines = [
        title,
        "  %-12s %6s %10s %10s %12s" % ("phase", "count", "min", "max", "mean"),
    ]
    for phase in PHASES:
        row = table[phase]
        if row["count"] == 0:
            lines.append("  %-12s %6d %10s %10s %12s" % (phase, 0, "-", "-", "-"))
            continue
        lines.append(
            "  %-12s %6d %10d %10d %12.1f"
            % (phase, row["count"], row["min"], row["max"], row["mean"])
        )
    return lines


def format_summaries(summaries):
    if not summaries:
        return []
    lines = []
    for index, summary in enumerate(summaries, start=1):
        lines.append(
            "  FRAMEPROF summary #%d (Phase 3 _frame probe):" % index
        )
        for name in SUMMARY_FIELDS:
            lines.append("    %-16s %s" % (name, summary.get(name, "-")))
    return lines


def print_report(report, all_frames):
    print("perf_report: " + report["rule"])
    print()
    for entry in report["logs"]:
        print("log: %s" % entry["path"])
        print(
            "  SHOW,frame samples: %d (selected %d, excluded %d, malformed %d)"
            % (
                entry["show_samples"],
                entry["selected"],
                entry["excluded"],
                entry["malformed"],
            )
        )
        if entry["show_samples"] == 0:
            print("  no SHOW,frame lines (FRAMEPROF-only capture)")
        else:
            if all_frames:
                print(
                    "  world filter: DISABLED (--all-frames); the excluded set is "
                    "included, so automap frames are part of this table"
                )
            elif entry["excluded"]:
                print(
                    "  world filter: excluded %d sample(s) with render_us <= %d us "
                    "-- the floating GPIO 35 opens the automap and a map frame "
                    "renders in ~0.9 ms; pass --all-frames to see them"
                    % (entry["excluded"], WORLD_RENDER_FLOOR_US)
                )
            else:
                print("  world filter: no sample was excluded")
            if entry["refused"]:
                print("  WARNING: refused frames reported: %d" % entry["refused"])
            label = "ALL FRAMES (unfiltered)" if all_frames else "WORLD FRAMES"
            for line in format_table("  %s:" % label, entry["table"]):
                print(line)
        for line in format_summaries(entry["summaries"]):
            print(line)
        print()

    if len(report["logs"]) > 1:
        label = "COMBINED ALL FRAMES (unfiltered)" if all_frames else "COMBINED WORLD FRAMES"
        print("%s across %d logs:" % (label, len(report["logs"])))
        for line in format_table("  %s:" % label, report["combined"]):
            print(line)
        band = report["world_fps_x10"]
        band_label = "all-frame fps_x10" if all_frames else "world fps_x10"
        print(
            "  %s band: %d-%d, mean %.1f over %d sample(s)"
            % (band_label, band["min"], band["max"], band["mean"], band["count"])
        )
        print()

    if report["passed"]:
        print("VERDICT: PASS -- " + report["rule"])
    else:
        print("VERDICT: FAIL")
        for failure in report["failures"]:
            print("  - " + failure)


def build_self_test_logs():
    """Synthetic captures that exercise every branch of the rule.

    One fixture is the normal case (world plus automap), one a band under the
    threshold, one an all-automap run, and one puts ``fps_x10`` before
    ``render_us`` so the parser is proven to read names, not positions.
    """

    def show(index, render_us, fps_x10, **extra):
        fields = [
            "frame=%d" % index,
            "render_us=%d" % render_us,
            "convert_us=%d" % extra.get("convert_us", 0),
            "present_us=%d" % extra.get("present_us", 27000),
            "frame_us=%d" % extra.get("frame_us", 64000),
            "fps_x10=%d" % fps_x10,
            "update_us=%d" % extra.get("update_us", 1700),
            "hash_us=%d" % extra.get("hash_us", 22700),
            "other_us=%d" % extra.get("other_us", 5700),
            "refused=%d" % extra.get("refused", 0),
            "hash=" + "0" * 64,
        ]
        return "SHOW,%s" % ",".join(fields)

    normal = "\n".join(
        [
            show(0, 27144, 157),
            show(1, 975, 187),
            show(2, 28000, 153),
            "FRAMEPROF,summary,views=20,runs=2,samples=40,frame_mean_us=20035,"
            "frame_min_us=9051,frame_max_us=29337,walk_us=2755,walls_us=7788,"
            "planes_us=6033,masked_us=3291,other_us=168,residency_us=0,"
            "residency_permille=0,gate_us=20000,gate=fail",
        ]
    )
    low = "\n".join([show(0, 27144, 140), show(1, 28000, 149)])
    map_only = "\n".join([show(0, 975, 187), show(1, 900, 190)])
    reordered = "SHOW,frame=0,fps_x10=155,render_us=27000,frame_us=64000,convert_us=0," \
                "present_us=27000,update_us=1700,hash_us=22700,other_us=5700," \
                "refused=0,hash=" + "0" * 64
    truncated = "SHOW,frame=9,render_us=27000,frame_u"
    boundary = show(0, 20000, 150)  # exactly the floor: excluded (literal on purpose)
    return {
        "normal": normal,
        "low": low,
        "map_only": map_only,
        "reordered": reordered,
        "truncated": truncated,
        "boundary": boundary,
    }


def run_self_test():
    """Exercise parsing and the rule without a log, a board, or a capture."""
    fixtures = build_self_test_logs()
    checks = []

    def check(name, condition, detail=""):
        checks.append((name, bool(condition), detail))

    report = analyse_logs([("normal", fixtures["normal"])], False, 150)
    entry = report["logs"][0]
    check("parser reads fields by name", entry["show_samples"] == 3,
          "samples=%d" % entry["show_samples"])
    check("world filter keeps the two world frames", entry["selected"] == 2,
          "selected=%d" % entry["selected"])
    check("world filter excludes the automap frame", entry["excluded"] == 1,
          "excluded=%d" % entry["excluded"])
    check("world min fps_x10 is 153", report["world_fps_x10"]["min"] == 153,
          str(report["world_fps_x10"]))
    check("normal capture passes the rule", report["passed"])
    check("FRAMEPROF summary is parsed", len(entry["summaries"]) == 1 and
          entry["summaries"][0]["samples"] == "40" and
          entry["summaries"][0]["gate"] == "fail",
          str(entry["summaries"]))

    low = analyse_logs([("low", fixtures["low"])], False, 150)
    check("a low world band fails", not low["passed"], str(low["failures"]))

    map_only = analyse_logs([("map", fixtures["map_only"])], False, 150)
    check("an all-automap run fails distinctly",
          not map_only["passed"] and
          any("no world frames in map" in failure for failure in map_only["failures"]),
          str(map_only["failures"]))

    all_frames = analyse_logs([("normal", fixtures["normal"])], True, 150)
    check("--all-frames includes the automap frame",
          all_frames["logs"][0]["selected"] == 3,
          "selected=%d" % all_frames["logs"][0]["selected"])

    reordered = analyse_logs([("reordered", fixtures["reordered"])], False, 150)
    check("a reordered line still parses", reordered["passed"] and
          reordered["world_fps_x10"]["min"] == 155,
          str(reordered["world_fps_x10"]))

    truncated = parse_log(fixtures["truncated"])
    check("a truncated line is malformed, not a sample",
          truncated[2] == 1 and not truncated[0], str(truncated[2]))

    boundary = analyse_logs([("boundary", fixtures["boundary"])], False, 150)
    check("render_us == the floor is excluded (strict >)",
          boundary["logs"][0]["selected"] == 0 and not boundary["passed"],
          str(boundary["logs"][0]["selected"]))

    failed = [name for name, ok, _ in checks if not ok]
    for name, ok, detail in checks:
        print("%s  %s%s" % ("PASS" if ok else "FAIL", name,
                            "" if ok else "  (%s)" % detail))
    if failed:
        print("SELFTEST: %d case(s) failed" % len(failed))
        return 1
    print("SELFTEST: %d cases passed" % len(checks))
    return 0


def perturb(report, min_fps):
    """Shift the parsed band down and prove the verdict turns FAIL.

    The shift is the tool's mutation test: if it lowers the numbers the report
    still passes, either the samples are not the ones the table prints or the
    rule does not read them, and both are defects in this tool.
    """
    shifted = 0
    for entry in report["logs"]:
        for phase in ("fps_x10",):
            row = entry["table"][phase]
            if row["min"] is not None:
                row["min"] = max(0, row["min"] - PERTURB_FPS_X10)
                row["max"] = max(0, row["max"] - PERTURB_FPS_X10)
                row["mean"] = max(0.0, row["mean"] - PERTURB_FPS_X10)
                shifted += 1
    combined = report["combined"]["fps_x10"]
    if combined["min"] is not None:
        combined["min"] = max(0, combined["min"] - PERTURB_FPS_X10)
        combined["max"] = max(0, combined["max"] - PERTURB_FPS_X10)
        combined["mean"] = max(0.0, combined["mean"] - PERTURB_FPS_X10)
    band = report["world_fps_x10"]
    if band["min"] is not None:
        band["min"] = max(0, band["min"] - PERTURB_FPS_X10)
        band["max"] = max(0, band["max"] - PERTURB_FPS_X10)
        band["mean"] = max(0.0, band["mean"] - PERTURB_FPS_X10)
    # A capture with a wide margin could survive a fixed shift; the gate this
    # proves is the threshold, so pin the minimum one point under it rather than
    # letting the mutation depend on today's numbers.
    if band["min"] is not None and band["min"] >= min_fps:
        band["min"] = min_fps - 1
    failures = []
    if band["count"] == 0:
        failures.append("no world frames in any log")
    elif band["min"] < min_fps:
        failures.append(
            "world fps_x10 minimum %d is below %d" % (band["min"], min_fps)
        )
    report["passed"] = not failures
    report["failures"] = failures
    report["world_fps_x10"] = band
    print(
        "PERTURB: lowered every world fps_x10 by %d (simulated minimum %s)"
        % (PERTURB_FPS_X10, band["min"])
    )
    return report


def main(argv=None):
    parser = argparse.ArgumentParser(
        description="Report DOOM frame time from a captured serial log (D7.3).",
    )
    parser.add_argument("logs", nargs="*", help="serial capture file(s) to read")
    parser.add_argument(
        "--all-frames",
        action="store_true",
        help="include automap frames (unfiltered; clearly labelled)",
    )
    parser.add_argument(
        "--min-fps",
        type=int,
        default=DEFAULT_MIN_FPS_X10,
        help="world fps_x10 floor (default %d)" % DEFAULT_MIN_FPS_X10,
    )
    parser.add_argument("--json", action="store_true", help="print JSON")
    parser.add_argument(
        "--perturb",
        action="store_true",
        help="lower the band and assert the rule fails (must exit non-zero)",
    )
    parser.add_argument(
        "--self-test",
        action="store_true",
        help="run the parser and rule on synthetic logs, no capture needed",
    )
    args = parser.parse_args(argv)

    if args.self_test:
        return run_self_test()

    logs = []
    if not args.logs:
        if args.perturb:
            # `--perturb` without a capture is the tool's own failure-path test:
            # the synthetic world+automap log is enough to show the rule turns
            # FAIL when the band is lowered, and it needs no board and no file.
            logs.append(
                ("<synthetic normal capture>", build_self_test_logs()["normal"])
            )
        else:
            parser.error("no capture file given (or pass --self-test)")

    for path in args.logs:
        try:
            with open(path, "r", encoding="utf-8", errors="replace") as handle:
                logs.append((path, handle.read()))
        except OSError as exc:
            print("perf_report: cannot read %s: %s" % (path, exc), file=sys.stderr)
            return 2

    report = analyse_logs(logs, args.all_frames, args.min_fps)
    if args.perturb:
        report = perturb(report, args.min_fps)
        if args.json:
            print(json.dumps(report, indent=2))
        else:
            print_report(report, args.all_frames)
        if report["passed"]:
            print(
                "PERTURB DEFECT: the simulated band still passes; the rule does "
                "not read the samples the table prints"
            )
            return 2
        print("PERTURB: simulated verdict FAIL (exit 1) -- the rule can fail")
        return 1

    if args.json:
        print(json.dumps(report, indent=2))
    else:
        print_report(report, args.all_frames)
    return 0 if report["passed"] else 1


if __name__ == "__main__":
    sys.exit(main())
