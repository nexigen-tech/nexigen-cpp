#!/usr/bin/env python3
"""Aggregate GCC gcov JSON output for the nexigen coverage target.

Only the library sources (include/ and src/) are scored; tests, examples and
system headers are ignored. Coverage is line coverage: a source line counts as
covered when it was executed by at least one object file.

Usage:
  coverage_report.py --clean-only --build-dir <dir>
  coverage_report.py --build-dir <dir> --source-dir <dir> [--gcov gcov]
                     [--fail-under 85]
"""

from __future__ import annotations

import argparse
import gzip
import glob
import json
import os
import subprocess
import sys
import tempfile
from collections import defaultdict


def clean_counters(build_dir: str) -> int:
    removed = 0
    for path in glob.glob(os.path.join(build_dir, "**", "*.gcda"), recursive=True):
        try:
            os.remove(path)
            removed += 1
        except OSError:
            pass
    return removed


def collect_line_counts(build_dir: str, source_dir: str, gcov: str) -> dict:
    source_dir = os.path.realpath(source_dir) + os.sep
    gcda_files = sorted(glob.glob(os.path.join(build_dir, "**", "*.gcda"), recursive=True))
    if not gcda_files:
        raise SystemExit("coverage: no .gcda files found; build and run the unit tests first")

    counts: dict[tuple[str, int], int] = {}
    with tempfile.TemporaryDirectory(prefix="nexigen-gcov-") as root:
        for index, gcda in enumerate(gcda_files):
            work = os.path.join(root, f"obj{index:04d}")
            os.makedirs(work, exist_ok=True)
            try:
                subprocess.run(
                    [gcov, "-j", "-o", os.path.dirname(gcda), gcda],
                    cwd=work,
                    check=True,
                    stdout=subprocess.DEVNULL,
                    stderr=subprocess.DEVNULL,
                )
            except (OSError, subprocess.CalledProcessError) as error:
                raise SystemExit(f"coverage: gcov failed for {gcda}: {error}") from error

            for report in glob.glob(os.path.join(work, "**", "*.gcov.json.gz"), recursive=True):
                with gzip.open(report, "rt", encoding="utf-8") as handle:
                    data = json.load(handle)
                for source in data.get("files", []):
                    name = os.path.realpath(source.get("file", ""))
                    if not name.startswith(source_dir):
                        continue
                    rel = name[len(source_dir):]
                    if not (rel.startswith("include/") or rel.startswith("src/")):
                        continue
                    for entry in source.get("lines", []):
                        key = (name, int(entry.get("line_number", 0)))
                        count = int(entry.get("count", 0))
                        if count > counts.get(key, -1):
                            counts[key] = count
    return counts


def summarize(counts: dict, source_dir: str) -> tuple[int, int]:
    source_dir = os.path.realpath(source_dir)
    modules = ["core", "chrono", "logging", "metrics", "tracing", "diagnostics", "app"]

    def module_of(path: str) -> str:
        rel = os.path.realpath(path)[len(source_dir) + 1:]
        parts = rel.split(os.sep)
        if len(parts) >= 3 and parts[0] == "include" and parts[1] == "nexigen":
            return parts[2]
        if len(parts) >= 2 and parts[0] == "src":
            return parts[1]
        return "other"

    totals: dict[str, list[int]] = defaultdict(lambda: [0, 0])
    for (path, _), count in counts.items():
        bucket = module_of(path)
        totals[bucket][0] += 1
        if count > 0:
            totals[bucket][1] += 1

    def percent(hit: int, total: int) -> float:
        return 100.0 * hit / total if total else 0.0

    print("\nnexigen coverage (line, unit tests only)\n")
    print(f"{'module':14} {'covered':>8} {'total':>8} {'%':>8}")
    print("-" * 42)
    overall_hit = overall_total = 0
    for module in modules + ["other"]:
        if module not in totals:
            continue
        total, hit = totals[module][0], totals[module][1]
        if total == 0:
            continue
        overall_hit += hit
        overall_total += total
        print(f"{module:14} {hit:8d} {total:8d} {percent(hit, total):7.1f}%")
    print("-" * 42)
    print(f"{'TOTAL':14} {overall_hit:8d} {overall_total:8d} "
          f"{percent(overall_hit, overall_total):7.1f}%\n")
    return overall_hit, overall_total


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", required=True)
    parser.add_argument("--source-dir", default=os.getcwd())
    parser.add_argument("--gcov", default="gcov")
    parser.add_argument("--clean-only", action="store_true")
    parser.add_argument("--fail-under", type=float, default=0.0)
    args = parser.parse_args()
    build_dir = os.path.realpath(args.build_dir)

    if args.clean_only:
        removed = clean_counters(build_dir)
        print(f"coverage: removed {removed} .gcda counter file(s)")
        return 0

    counts = collect_line_counts(build_dir, args.source_dir, args.gcov)
    hit, total = summarize(counts, args.source_dir)
    if args.fail_under > 0.0:
        coverage = 100.0 * hit / total if total else 0.0
        if coverage < args.fail_under:
            print(f"coverage: {coverage:.1f}% is below the required {args.fail_under:.1f}%")
            return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
