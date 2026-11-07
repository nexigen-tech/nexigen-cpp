#!/usr/bin/env python3
"""Cyclomatic complexity gate for nexigen-cpp.

Usage:
    check_complexity.py [--threshold N] [dir ...]

Scans the given directories (default: include/ src/ tests/) with lizard,
complains about every C++ function whose cyclomatic complexity exceeds
the threshold (default 15), and filters out known false positives:

- ``alignas(...)`` pseudo-functions: lizard counts inline assembly
  markers as real functions with high complexity.

Returns exit code 0 (pass) or 1 (fail).
"""

from __future__ import annotations

import argparse
import subprocess
import sys

DEFAULT_THRESHOLD = 15
DEFAULT_DIRS = ["include/", "src/", "tests/"]


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--threshold", type=int, default=DEFAULT_THRESHOLD,
                        help="CCN threshold (default %d)" % DEFAULT_THRESHOLD)
    parser.add_argument("dirs", nargs="*", default=DEFAULT_DIRS,
                        help="Directories to scan (default: %s)" % " ".join(DEFAULT_DIRS))
    args = parser.parse_args()

    result = subprocess.run(
        ["lizard", "-l", "cpp", "-C", str(args.threshold), "-w"] + args.dirs,
        capture_output=True, text=True,
    )

    # Lizard writes warnings to stdout; stderr may contain "No thresholds exceeded"
    # when there are no warnings. Filter out alignas false positives.
    warnings = [
        line for line in result.stdout.splitlines()
        if "alignas" not in line
    ]

    if warnings:
        print("Complexity gate FAILED — CCN > %d found:" % args.threshold)
        for line in warnings:
            print(line)
        if result.stderr:
            print(result.stderr)
        return 1

    n_warnings = result.stdout.count("\n") - (result.stdout.count("alignas\n") if result.stdout else 0)
    if n_warnings == 0:
        print("Complexity gate passed: no C++ function in %s > CCN %d" %
              (" ".join(args.dirs), args.threshold))
    else:
        print("Complexity gate passed (%d non-alignas warnings filtered)" % n_warnings)
    return 0


if __name__ == "__main__":
    sys.exit(main())