#!/usr/bin/env python3
"""Generate the kSinQ14 quarter-wave sine table committed in src/pool/Trig.cpp.

Standard library only (no numpy, no engine dependency): this script is a
one-time generator, never a build-time dependency. The C++ build never
invokes Python, so the ESP32 firmware build has no Python toolchain
requirement. Run it manually and paste the printed array body into Trig.cpp
if the angle resolution (1,024 steps/revolution) or the fixed-point scale
(Q14) ever changes.

kSinQ14[i] holds round(16384 * sin(i * 2*pi / 1024)) for i in 0..256, i.e. one
quarter of a full revolution (0..90 degrees) in Q14 fixed point (16384 = 1.0).
pool::sinQ14 folds the other three quadrants from this quarter at runtime
(see src/pool/Trig.cpp).
"""
import math

ANGLE_STEPS = 1024
Q14_ONE = 16384
QUARTER = ANGLE_STEPS // 4  # 256 entries: angle 0 .. 256 inclusive


def generate() -> list:
    """Return the 257 quarter-wave Q14 sine samples, rounded to the nearest int."""
    return [
        round(Q14_ONE * math.sin(i * 2.0 * math.pi / ANGLE_STEPS))
        for i in range(QUARTER + 1)
    ]


def format_table(values: list, per_line: int = 8) -> str:
    """Format values as C++ array-initializer body lines, one row per per_line entries."""
    lines = []
    for start in range(0, len(values), per_line):
        chunk = values[start:start + per_line]
        lines.append("    " + ", ".join(str(v) for v in chunk) + ",")
    return "\n".join(lines)


def main() -> None:
    values = generate()
    assert len(values) == QUARTER + 1
    assert values[0] == 0
    assert values[QUARTER] == Q14_ONE
    print(format_table(values))


if __name__ == "__main__":
    main()
