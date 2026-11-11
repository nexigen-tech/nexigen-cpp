#!/usr/bin/env python3
"""Fail when a C++ type/enum/concept name violates PascalCase (mandate 8).

Mandate 8 requires PascalCase for types, enums and concepts (snake_case is for
functions/variables). A single-shot rename already fixed the public API, but
declarations of the form `struct alignas(64) Foo_bar` slipped through a naive
regex that assumed the name directly follows the keyword.

This guard scans tracked C++ sources, strips comments/literals, and reports any
`struct`/`class`/`enum`/`union`/`concept` declaration or `using` type alias
whose name is `UpperCamel_with_underscore`. Attributes (`alignas(...)`,
`[[...]]`) between the keyword and the name are handled — that is precisely the
case this gate exists to catch.

A line may opt out with `// NX_ALLOW_NAMING` when a genuine exception is needed.

Usage:
  check_naming.py [--root <dir>]
"""

from __future__ import annotations

import argparse
import re
import subprocess
import sys
from pathlib import Path

SOURCE_SUFFIXES = {".h", ".hh", ".hpp", ".hxx", ".cc", ".cpp", ".cxx"}

# A declaration keyword followed by optional attributes, then a snake_case
# (UpperCamel_with_underscore) name. `enum class Foo_bar` is caught through the
# trailing `class` keyword; plain `enum Foo_bar`, `union Foo_bar` and
# `concept Foo_bar` through their own.
TYPE_DECL = re.compile(
    r"\b(struct|class|enum|union|concept)\b"
    r"(?:\s+(?:alignas\s*\([^()]*\)|\[\[[^\]]*\]\]))*"
    r"\s*([A-Z][A-Za-z0-9]*_[A-Za-z0-9_]*)"
)

# A `using` type alias with a snake_case alias name (`using Foo_bar = ...`).
ALIAS_DECL = re.compile(r"\busing\s+([A-Z][A-Za-z0-9]*_[A-Za-z0-9_]*)\s*=")

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


def find_in_text(text: str) -> list[tuple[int, str]]:
    """(line, name) for every snake_case declaration in one source text."""
    stripped = blank_literals_and_comments(text)
    original_lines = text.splitlines()
    found: list[tuple[int, str]] = []
    for pattern in (TYPE_DECL, ALIAS_DECL):
        for match in pattern.finditer(stripped):
            line_number = stripped.count("\n", 0, match.start()) + 1
            original = (
                original_lines[line_number - 1] if line_number - 1 < len(original_lines) else ""
            )
            if "NX_ALLOW_NAMING" in original:
                continue
            found.append((line_number, match.group(len(match.groups()))))
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
        for line_number, name in find_in_text(text):
            violations.append((str(relative), line_number, name))
    return violations


SELF_TEST_FIXTURES: list[tuple[str, list[str]]] = [
    # A digit separator must not hide the declaration after it.
    ("constexpr int k = 250'000;\nstruct Bad_name {};\n", ["Bad_name"]),
    ("constexpr int k = 0xA'B;\nstruct Also_bad {};\n", ["Also_bad"]),
    # Literal and comment contents stay invisible.
    ('// struct In_comment {};\nconst char* s = "struct In_string {};";\n', []),
    # Prefixed character literals are not digit separators.
    ("int f(char c) { return c == L'x' ? 1 : 0; }\nstruct Good {};\n", []),
    # A plain violation is still reported.
    ("struct Plain_bad {};\n", ["Plain_bad"]),
]


def self_test() -> int:
    failures = 0
    for source, expected in SELF_TEST_FIXTURES:
        found = sorted(name for _, name in find_in_text(source))
        if found != expected:
            failures += 1
            print(f"self-test FAIL: expected {expected}, got {found} in {source!r}")
    if failures:
        return 1
    print("naming guard self-test: ok (digit separators, literals, comments, prefixed literals)")
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
        print("naming guard: no snake_case type/enum/concept names")
        return 0
    for path, line, name in violations:
        print(f"{path}:{line}: {name}")
    print(
        f"naming guard: {len(violations)} snake_case type/enum/concept name(s); "
        "use PascalCase for types, enums and concepts (mandate 8)"
    )
    return 1


if __name__ == "__main__":
    sys.exit(main())
