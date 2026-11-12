#!/usr/bin/env python3
"""Fail when a JSON field-extraction helper is defined outside its canonical home.

The project keeps a single definition site for the field-extraction vocabulary
(spec `core/serialization`, requirement "Manifest and config deserialization
uses a shared field vocabulary"):

  * `read_*` / `collect_*` / `to_*` live in `include/nexigen/core/detail/json_field.hpp`
  * the enum glue (`read_enum`, `read_optional_enum`, `enum_to_json`,
    `enum_from_json`) lives in `include/nexigen/core/enum_traits.hpp`

This guard scans tracked C++ sources and reports any DEFINITION of a vocabulary
function outside its canonical header. Call sites (`core::detail::read_string(...)`,
a plain `read_string(...)`) are not definitions and are ignored.

Usage:
  check_json_field_vocabulary.py [--root <dir>]
"""

from __future__ import annotations

import argparse
import re
import subprocess
import sys
from pathlib import Path

FIELD_HEADER = "include/nexigen/core/detail/json_field.hpp"
ENUM_HEADER = "include/nexigen/core/enum_traits.hpp"

# The exact field-extraction vocabulary. Every function here reads object fields
# and takes `const nlohmann::json&` as its first parameter. Composite/domain
# helpers (`read_log_level`, `read_histogram_fields`, `settings_io::read_uint`,
# ...) are deliberately absent — they compose this vocabulary rather than
# re-implement it, and are not part of the single-definition-site contract.
FIELD_READERS = [
    "read_string",
    "read_bool",
    "read_signed",
    "read_unsigned",
    "read_number",
    "read_counter",
    "read_object",
    "collect_array",
    "collect_array_into",
    "collect_object_array",
    "read_fixed_string",
    "read_fixed_bytes",
    "read_nullable_signed",
    "read_optional_string",
    "read_optional_bool",
    "read_optional_unsigned",
    "read_optional_object",
]
ENUM_READERS = {"read_enum", "read_optional_enum"}


def canonical_header(name: str) -> str:
    return ENUM_HEADER if name in ENUM_READERS else FIELD_HEADER


NAME_ALT = "|".join(
    re.escape(name) for name in sorted(FIELD_READERS + list(ENUM_READERS), key=len, reverse=True)
)

# A definition is `<return-type> <name>( const nlohmann::json& ...)`. Requiring
# the `const nlohmann::json&` first parameter distinguishes a field reader from a
# same-named non-vocabulary function (for example a test probe whose first
# parameter is a `const char*` key). Every vocabulary function returns `bool`
# or `void`.
DEF_RE = re.compile(
    r"(?:inline\s+)?(?:template\s*<[^>\n]*>\s+)?(?:\[\[nodiscard\]\]\s+)?"
    r"(?:bool|void)\s+(" + NAME_ALT + r")\s*\(\s*const\s+nlohmann::json\s*&"
)

SOURCE_SUFFIXES = {".h", ".hh", ".hpp", ".hxx", ".cc", ".cpp", ".cxx"}
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
            ["git", "ls-files", "-z", "--cached", "--others", "--exclude-standard"],
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
        if Path(name).suffix in SOURCE_SUFFIXES and not name.startswith(SKIP_PREFIXES)
    ]


def find_definitions(text: str) -> list[tuple[int, str]]:
    """(line, name) for every vocabulary-function definition in one source text."""
    found: list[tuple[int, str]] = []
    for match in DEF_RE.finditer(text):
        line = text.count("\n", 0, match.start()) + 1
        found.append((line, match.group(1)))
    return found


def scan(root: Path) -> list[tuple[str, int, str]]:
    violations: list[tuple[str, int, str]] = []
    for path in tracked_sources(root):
        try:
            text = path.read_text(encoding="utf-8")
        except (OSError, UnicodeDecodeError):
            continue
        try:
            relative = str(path.relative_to(root))
        except ValueError:
            relative = str(path)
        for line, name in find_definitions(text):
            if relative == canonical_header(name):
                continue  # the canonical definition site
            violations.append((relative, line, name))
    return violations


SELF_TEST_FIXTURES: list[tuple[str, list[str]]] = [
    # A canonical definition is found (and then allowed by path).
    ("inline bool read_string(const nlohmann::json& o, const char* k, std::string& s) noexcept {",
     ["read_string"]),
    ("template <class T> bool read_signed(const nlohmann::json& o, const char* k, T& v) noexcept {",
     ["read_signed"]),
    ("template <class Enum> void read_enum(const nlohmann::json& o, const char* k, Enum& v) noexcept {",
     ["read_enum"]),
    # A call site is not a definition.
    ("void f() { core::detail::read_string(j, \"a\", s); read_bool(j, \"b\", b); }", []),
    # A same-named function whose first parameter is not a JSON object is
    # ignored (read_all takes a fd; a test probe takes a const char* key).
    ("void read_all(int fd, void* buf, size_t n) {}", []),
    ("inline void read_counter(const char* key, const nlohmann::json& doc) {}", []),
]


def self_test() -> int:
    failures = 0
    for source, expected in SELF_TEST_FIXTURES:
        found = [name for _, name in find_definitions(source)]
        if found != expected:
            failures += 1
            print(f"self-test FAIL: expected {expected}, got {found} in {source!r}")
    if failures:
        return 1
    print("json-field-vocabulary guard self-test: ok")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", default=".", help="repository root to scan")
    parser.add_argument("--self-test", action="store_true", help="run embedded fixtures")
    args = parser.parse_args()
    if args.self_test:
        return self_test()
    root = Path(args.root).resolve()
    violations = scan(root)
    if not violations:
        print("json-field-vocabulary guard: single definition site held")
        return 0
    for path, line, name in violations:
        print(f"{path}:{line}: {name} redefined outside {canonical_header(name)}")
    print(
        f"json-field-vocabulary guard: {len(violations)} duplicate definition(s); "
        "route deserialization through nexigen::core::detail / nexigen::core::mandate"
    )
    return 1


if __name__ == "__main__":
    sys.exit(main())
