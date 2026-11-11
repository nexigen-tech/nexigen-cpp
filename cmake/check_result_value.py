#!/usr/bin/env python3
"""Fail when C++ code calls `.value()` on a Result/Status receiver.

The project bans `std::expected::value()` because it aborts under
`-fno-exceptions`. This guard scans tracked C++ sources, strips comments and
literals, and checks every `.value()` receiver against a small allowlist of
known non-Result types:

  * span/trace ID accessors (`trace_id`, `span_id`, `parent_span_id`, `id`)
  * nlohmann JSON iterators (`entry`, `item`)
  * local ID values used by tests (`trace`, `span`, `TraceId`, `SpanId`)

Anything else is reported as `path:line: receiver.value()`. A line may opt out
with `// NX_ALLOW_VALUE` when a new non-Result receiver is genuinely needed.

Usage:
  check_result_value.py [--root <dir>]
"""

from __future__ import annotations

import argparse
import re
import subprocess
import sys
from pathlib import Path

ALLOWED_RECEIVERS = {"entry", "item", "id", "trace", "span", "TraceId", "SpanId"}
ALLOWED_SUFFIXES = ("_id",)
SOURCE_SUFFIXES = {".h", ".hh", ".hpp", ".hxx", ".cc", ".cpp", ".cxx"}
VALUE_CALL = re.compile(r"\.\s*value\s*\(\s*\)")
RECEIVER_CHAIN = re.compile(
    r"([A-Za-z_]\w*(?:\s*(?:->|\.)\s*[A-Za-z_]\w*)*)\s*$"
)
SKIP_PREFIXES = (
    "build/",
    ".git/",
    "openspec/",
    "third_party/",
    "external/",
)


def tracked_sources(root: Path) -> list[Path]:
    try:
        result = subprocess.run(
            ["git", "ls-files", "-z", "--cached", "--others",
             "--exclude-standard"],
            cwd=root,
            check=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.DEVNULL,
        )
    except (OSError, subprocess.CalledProcessError):
        return sorted(
            path
            for path in root.rglob("*")
            if path.is_file() and path.suffix in SOURCE_SUFFIXES
        )
    names = [name for name in result.stdout.decode("utf-8").split("\0") if name]
    return [
        root / name
        for name in names
        if Path(name).suffix in SOURCE_SUFFIXES
        and not name.startswith(SKIP_PREFIXES)
    ]


HEX_DIGITS = "0123456789abcdefABCDEF"


def is_digit_separator(text: str, index: int) -> bool:
    """True for a C++14 digit separator (``1'000``, ``0xA'B``).

    The apostrophe is only a separator when it sits between two hex digits;
    everything else (``'x'``, ``L'x'``) is a character literal.
    """
    if index == 0 or index + 1 >= len(text):
        return False
    return text[index - 1] in HEX_DIGITS and text[index + 1] in HEX_DIGITS


def blank_literals_and_comments(text: str) -> str:
    """Replace comments and literals with spaces while preserving newlines."""
    out = list(text)
    index = 0
    length = len(text)
    while index < length:
        char = text[index]
        nxt = text[index + 1] if index + 1 < length else ""

        if char == "/" and nxt == "/":
            end = text.find("\n", index)
            end = length if end == -1 else end
            for position in range(index, end):
                out[position] = " "
            index = end
            continue

        if char == "/" and nxt == "*":
            end = text.find("*/", index + 2)
            end = length if end == -1 else end + 2
            for position in range(index, end):
                if out[position] != "\n":
                    out[position] = " "
            index = end
            continue

        if char == "R" and nxt == '"':
            open_paren = text.find("(", index + 2)
            if open_paren != -1:
                delimiter = text[index + 2 : open_paren]
                closing = ")" + delimiter + '"'
                end = text.find(closing, open_paren + 1)
                end = length if end == -1 else end + len(closing)
                for position in range(index, end):
                    if out[position] != "\n":
                        out[position] = " "
                index = end
                continue

        if char == "'" and is_digit_separator(text, index):
            index += 1
            continue

        if char in {'"', "'"}:
            quote = char
            position = index + 1
            while position < length:
                if text[position] == "\\":
                    position += 2
                    continue
                if text[position] == quote:
                    position += 1
                    break
                position += 1
            for blank in range(index, min(position, length)):
                if out[blank] != "\n":
                    out[blank] = " "
            index = position
            continue

        index += 1
    return "".join(out)


def receiver_before(stripped: str, dot: int) -> str:
    prefix = stripped[:dot].rstrip()
    if prefix.endswith("}"):
        depth = 0
        position = len(prefix) - 1
        while position >= 0:
            if prefix[position] == "}":
                depth += 1
            elif prefix[position] == "{":
                depth -= 1
                if depth == 0:
                    break
            position -= 1
        if position > 0:
            match = re.search(r"([A-Za-z_]\w*)\s*$", prefix[:position])
            if match is not None:
                return match.group(1)
    match = RECEIVER_CHAIN.search(prefix)
    if match is None:
        return ""
    chain = match.group(1)
    return re.split(r"\s*(?:->|\.)\s*", chain)[-1]


def allowed(receiver: str) -> bool:
    if receiver in ALLOWED_RECEIVERS:
        return True
    return any(receiver.endswith(suffix) for suffix in ALLOWED_SUFFIXES)


def find_in_text(text: str) -> list[tuple[int, str]]:
    """(line, receiver) for every forbidden .value() call in one source text."""
    stripped = blank_literals_and_comments(text)
    original_lines = text.splitlines()
    found: list[tuple[int, str]] = []
    for match in VALUE_CALL.finditer(stripped):
        line_number = stripped.count("\n", 0, match.start()) + 1
        original = (
            original_lines[line_number - 1] if line_number - 1 < len(original_lines) else ""
        )
        if "NX_ALLOW_VALUE" in original:
            continue
        receiver = receiver_before(stripped, match.start())
        if allowed(receiver):
            continue
        found.append((line_number, receiver))
    return found


def scan(root: Path) -> list[tuple[str, int, str]]:
    violations: list[tuple[str, int, str]] = []
    for path in tracked_sources(root):
        try:
            text = path.read_text(encoding="utf-8")
        except (OSError, UnicodeDecodeError):
            continue
        try:
            relative = path.relative_to(root)
        except ValueError:
            relative = path
        for line_number, receiver in find_in_text(text):
            violations.append((str(relative), line_number, receiver))
    return violations


SELF_TEST_FIXTURES: list[tuple[str, list[str]]] = [
    # A digit separator must not hide the call after it.
    ("constexpr int k = 1'000;\nvoid f() { r.value(); }\n", ["r"]),
    ("constexpr int k = 0xA'B;\nvoid g() { q.value(); }\n", ["q"]),
    # Literal and comment contents stay invisible.
    ('// r.value();\nconst char* s = "r.value()";\n', []),
    # Allowlisted receivers are still accepted.
    ("void h() { span.value(); trace.value(); item.value(); }\n", []),
    # A plain violation is still reported.
    ("void k() { r.value(); }\n", ["r"]),
]


def self_test() -> int:
    failures = 0
    for source, expected in SELF_TEST_FIXTURES:
        found = sorted(receiver for _, receiver in find_in_text(source))
        if found != expected:
            failures += 1
            print(f"self-test FAIL: expected {expected}, got {found} in {source!r}")
    if failures:
        return 1
    print("result-value guard self-test: ok (digit separators, literals, comments, allowlist)")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--root",
        default=".",
        help="repository root to scan (default: current directory)",
    )
    parser.add_argument(
        "--self-test",
        action="store_true",
        help="run the embedded fixtures instead of scanning a tree",
    )
    args = parser.parse_args()
    if args.self_test:
        return self_test()
    root = Path(args.root).resolve()
    violations = scan(root)
    if not violations:
        print("result-value guard: no forbidden .value() calls")
        return 0
    for path, line, receiver in violations:
        print(f"{path}:{line}: {receiver}.value()")
    print(
        f"result-value guard: {len(violations)} forbidden .value() call(s); "
        "branch on has_value() and use *value or value->"
    )
    return 1


if __name__ == "__main__":
    sys.exit(main())
