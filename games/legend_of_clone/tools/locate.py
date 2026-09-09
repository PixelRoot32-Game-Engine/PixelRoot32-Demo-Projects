"""Finds the demo these tools write to.

These scripts used to live under the engine's gitignored docs/audits/, outside
the tree they operate on, and walked up looking for examples/legend_of_clone.
They now sit inside the demo itself, so the answer is simply the parent of this
tools/ directory. An explicit path is still accepted for the unusual layouts:
a worktree, or a checkout somewhere else entirely.
"""
import pathlib
import sys

EXAMPLE_NAME = "legend_of_clone"


def locate_example(argv=None):
    """Returns the demo root - the directory holding src/assets.

    Takes the first command-line argument as an override, so these scripts still
    work against a worktree or a checkout somewhere else entirely.
    """
    args = sys.argv[1:] if argv is None else argv

    if args:
        found = pathlib.Path(args[0]).resolve()
        if (found / "src" / "assets").is_dir():
            return found
        raise SystemExit(f"{found} does not look like the {EXAMPLE_NAME} demo "
                         f"(no src/assets under it)")

    found = pathlib.Path(__file__).resolve().parent.parent
    if (found / "src" / "assets").is_dir():
        return found

    raise SystemExit(f"{found} does not look like the {EXAMPLE_NAME} demo "
                     f"(no src/assets under it). These tools expect to live in "
                     f"<demo>/tools/. Pass the demo path as the first argument.")
