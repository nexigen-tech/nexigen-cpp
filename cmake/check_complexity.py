#!/usr/bin/env python3
"""Cyclomatic complexity gate for nexigen-cpp (spec quality/code-structure).

Usage:
    check_complexity.py [--threshold N] [dir ...]

Scans the given directories (default: include/ src/ tests/ tools/) with lizard
and fails on every C++ function whose cyclomatic complexity exceeds the
threshold (default 15). Daemon entry points under tools/ are in scope.

Known false positives are filtered by identifier, not by line content: lizard
parses ``class alignas(64) T { ... }`` as a function named ``alignas`` and sums
the complexity of every member.

Exit codes:
    0  every scanned function is within the threshold
    1  at least one function exceeds it
    77 lizard is not installed (CTest SKIP_RETURN_CODE — reported as skipped,
       never as a pass)
"""

from __future__ import annotations

import argparse
import shutil
import subprocess
import sys

DEFAULT_THRESHOLD = 15
DEFAULT_DIRS = ["include", "src", "tests", "tools"]
SKIP_EXIT = 77


def is_false_positive(line: str) -> bool:
    """True for the ``alignas`` pseudo-functions lizard reports on class bodies."""
    if ": warning:" not in line:
        return False
    function = line.split(": warning:", 1)[1].strip().split(" has ", 1)[0]
    leaf = function.rsplit("::", 1)[-1]
    return leaf == "alignas" or leaf.startswith("alignas(")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--threshold", type=int, default=DEFAULT_THRESHOLD,
                        help="CCN threshold (default %d)" % DEFAULT_THRESHOLD)
    parser.add_argument("dirs", nargs="*", default=DEFAULT_DIRS,
                        help="Directories to scan (default: %s)" % " ".join(DEFAULT_DIRS))
    args = parser.parse_args()

    if shutil.which("lizard") is None:
        print("Complexity gate SKIPPED: lizard not found (pip install lizard)")
        return SKIP_EXIT

    result = subprocess.run(
        ["lizard", "-l", "cpp", "-C", str(args.threshold), "-w", *args.dirs],
        capture_output=True, text=True,
    )

    warnings = [line for line in result.stdout.splitlines() if ": warning:" in line]
    violations = [line for line in warnings if not is_false_positive(line)]
    filtered = len(warnings) - len(violations)

    if violations:
        print("Complexity gate FAILED — CCN > %d found:" % args.threshold)
        for line in violations:
            print(line)
        if filtered:
            print("(%d alignas pseudo-function warning(s) filtered)" % filtered)
        return 1

    # lizard exits 1 whenever it printed any warning, including the filtered
    # alignas pseudo-functions; >1 means the analyser itself failed.
    if result.returncode > 1:
        print("Complexity gate FAILED — lizard exited with %d" % result.returncode)
        print(result.stderr)
        return 1

    print("Complexity gate passed: no C++ function in %s > CCN %d (%d alignas warning(s) "
          "filtered)" % (" ".join(args.dirs), args.threshold, filtered))
    return 0


if __name__ == "__main__":
    sys.exit(main())
