"""Reports how far each player sprite is from its own mirror, and from the others.

    python check_sprites.py

The walk cycle spends the horizontal-flip bit as an animation frame for the two
vertical directions, so a sprite that happens to be left-right SYMMETRIC makes
its own walk cycle invisible. That bug has already shipped once here: the
north-facing sprite was symmetric, the flip was a no-op, and it read as a broken
timer rather than as broken art.

It also reports pairs that are mirrors of each other, because storing both
halves of a mirror pair is 128 bytes of duplicate - and because claiming in a
comment that two frames are "a real bitmap, not a mirror" should be a measured
statement rather than a hopeful one.

Exit code is non-zero only for the symmetric case, which is always a bug. A
mirror pair is a size opportunity, not an error.
"""
import sys
import pathlib

sys.path.insert(0, str(pathlib.Path(__file__).parent))
import art_source as art  # noqa: E402

# Below this many differing pixels a flip is not visibly an animation frame.
MIN_ASYMMETRY = 2


def mirror(rows):
    return [row[::-1] for row in rows]


def distance(a, b):
    return sum(1 for ra, rb in zip(a, b) for ca, cb in zip(ra, rb) if ca != cb)


sprites = {name: rows for name, _doc, rows in art.PLAYER_SPRITES}
fail = []

print(f"{len(sprites)} bitmaps\n")

print("self vs own mirror  (the vertical walk cycle IS this difference)")
for name, rows in sprites.items():
    d = distance(rows, mirror(rows))
    flag = ""
    if d < MIN_ASYMMETRY:
        flag = "  <-- SYMMETRIC: a mirrored walk frame would be invisible"
        fail.append(f"{name} is left-right symmetric ({d} px); mirroring it animates nothing")
    print(f"  {name:<16} {d:>4} px{flag}")

print("\npairs vs each other's mirror  (0 means one of them is redundant)")
names = list(sprites)
for i, a in enumerate(names):
    for b in names[i + 1:]:
        d = distance(mirror(sprites[a]), sprites[b])
        note = "  <-- mirror pair: drop one and use the flip bit" if d == 0 else ""
        print(f"  mirror({a}) vs {b:<16} {d:>4} px{note}")

if fail:
    print("\nFAILURES:")
    for f in fail:
        print("  -", f)
    sys.exit(1)
print("\nOK: every bitmap is asymmetric enough for its flip to read as animation")
