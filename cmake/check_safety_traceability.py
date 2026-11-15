#!/usr/bin/env python3
"""Fail when the safety traceability matrix drifts from its pinned catalog.

The repository keeps two artefacts that must agree:

  docs/safety/rs-safety-r24-11.tsv   immutable catalog of the 62 requirements
                                     of AUTOSAR_FO_RS_Safety R24-11
  docs/safety/traceability.md        living matrix: one row per requirement
                                     with status, capability, evidence, note

This gate cross-checks them structurally. It does not judge whether a
`supported` claim is true; review does that.

Catalog grammar
--------------
  * line 1 starts with `#` and pins `document 986`, `R24-11`, `44 pages`, and
    `source sha256 <hex>`;
  * line 2 is the tab-separated header `id<TAB>title<TAB>level`;
  * every later non-empty line is `id<TAB>title<TAB>level`, where `id` matches
    `RS_<CLUSTER>_<DIGITS>`.

Matrix grammar
--------------
  * the table header is `| ID | Summary | Status | Capability | Evidence | Note |`
    with six columns, in that order;
  * every row whose first cell is an `RS_...` id carries exactly six cells;
  * `Status` is one of `supported`, `partial`, `not-implemented`,
    `out-of-scope`;
  * `Capability` and `Evidence` hold backticked repo-relative tokens; every
    token containing `/` is checked on disk. A capability token `a/b` must
    resolve to `openspec/specs/a/b/spec.md`.
  * `supported` and `partial` need capability and evidence; `partial` needs a
    note; `not-implemented` needs `closing:` or `deviation:` in the note;
    `out-of-scope` needs `boundary:` in the note.

Exit codes
----------
  0  catalog and matrix agree
  1  one or more violations (printed on stdout, one per line)
  2  usage error or an artefact could not be read

Usage
-----
  check_safety_traceability.py [--root DIR] [--self-test]
"""

from __future__ import annotations

import argparse
import re
import sys
import tempfile
from collections import Counter
from pathlib import Path

CATALOG_PATH = "docs/safety/rs-safety-r24-11.tsv"
MATRIX_PATH = "docs/safety/traceability.md"
SPEC_ROOT = "openspec/specs"
PROVENANCE_MARKERS = ("document 986", "R24-11", "44 pages", "source sha256")
STATUSES = ("supported", "partial", "not-implemented", "out-of-scope")
COLUMNS = ("ID", "Summary", "Status", "Capability", "Evidence", "Note")
ID_PATTERN = re.compile(r"RS_[A-Z]+_\d+")
TOKEN_PATTERN = re.compile(r"`([^`]+)`")


def parse_catalog(text: str) -> tuple[list[str], list[str]]:
    """Return (ids, errors) for one catalog text."""
    errors: list[str] = []
    ids: list[str] = []
    comments: list[str] = []
    header_seen = False
    for number, line in enumerate(text.splitlines(), start=1):
        if not line.strip():
            continue
        if line.startswith("#"):
            comments.append(line)
            continue
        if not header_seen:
            header = [cell.strip() for cell in line.split("\t")]
            if header != ["id", "title", "level"]:
                errors.append(
                    f"catalog:{number}: expected header id<TAB>title<TAB>level, got {header}"
                )
            header_seen = True
            continue
        parts = line.split("\t")
        if len(parts) != 3:
            errors.append(f"catalog:{number}: expected 3 tab-separated fields, got {len(parts)}")
            continue
        candidate = parts[0].strip()
        if not ID_PATTERN.fullmatch(candidate):
            errors.append(f"catalog:{number}: '{candidate}' is not a requirement identifier")
            continue
        ids.append(candidate)
    if not header_seen:
        errors.append("catalog: missing id/title/level header row")
    provenance = "\n".join(comments)
    for marker in PROVENANCE_MARKERS:
        if marker not in provenance:
            errors.append(f"catalog: provenance header does not pin '{marker}'")
    return ids, errors


def parse_matrix(text: str) -> tuple[list[list[str]], list[str]]:
    """Return (rows, errors) for one matrix text; rows are six-cell lists."""
    errors: list[str] = []
    rows: list[list[str]] = []
    header_seen = False
    for line in text.splitlines():
        stripped = line.strip()
        if not stripped.startswith("|"):
            continue
        cells = [cell.strip() for cell in stripped.strip("|").split("|")]
        if cells == list(COLUMNS):
            header_seen = True
            continue
        if not cells or not ID_PATTERN.fullmatch(cells[0]):
            continue
        if len(cells) != len(COLUMNS):
            errors.append(
                f"matrix: row {cells[0]} has {len(cells)} cells, expected {len(COLUMNS)}"
            )
            continue
        rows.append(cells)
    if not header_seen:
        errors.append("matrix: missing the | ID | Summary | Status | Capability | Evidence | Note | header")
    return rows, errors


def _tokens(cell: str) -> list[str]:
    return [token for token in TOKEN_PATTERN.findall(cell) if "/" in token]


def _check_capability(cell: str, root: Path, requirement: str, errors: list[str]) -> None:
    tokens = _tokens(cell)
    if not tokens:
        errors.append(f"matrix: {requirement} names no capability spec")
        return
    for token in tokens:
        spec = root / SPEC_ROOT / token / "spec.md"
        if not spec.is_file():
            errors.append(f"matrix: {requirement} capability spec is missing: {SPEC_ROOT}/{token}/spec.md")


def _check_evidence(cell: str, root: Path, requirement: str, errors: list[str]) -> None:
    tokens = _tokens(cell)
    if not tokens:
        errors.append(f"matrix: {requirement} names no evidence path")
        return
    for token in tokens:
        if not (root / token).exists():
            errors.append(f"matrix: {requirement} evidence path is missing: {token}")


def scan(catalog_text: str, matrix_text: str, root: Path) -> tuple[list[str], Counter[str]]:
    """Validate one catalog/matrix pair; return (errors, status counts)."""
    catalog_ids, errors = parse_catalog(catalog_text)
    rows, matrix_errors = parse_matrix(matrix_text)
    errors.extend(matrix_errors)

    duplicates = [item for item, count in Counter(catalog_ids).items() if count > 1]
    for item in sorted(duplicates):
        errors.append(f"catalog: duplicate identifier: {item}")
    known = set(catalog_ids)

    seen: set[str] = set()
    counts: Counter[str] = Counter()
    for cells in rows:
        requirement, _summary, status, capability, evidence, note = cells
        if requirement in seen:
            errors.append(f"matrix: duplicate identifier: {requirement}")
        seen.add(requirement)
        if requirement not in known:
            errors.append(f"matrix: unknown identifier: {requirement}")
            continue
        if status not in STATUSES:
            errors.append(f"matrix: {requirement} has invalid status '{status}'")
            continue
        counts[status] += 1
        if status in ("supported", "partial"):
            _check_capability(capability, root, requirement, errors)
            _check_evidence(evidence, root, requirement, errors)
        if status == "partial" and note in ("", "-"):
            errors.append(f"matrix: {requirement} is partial but states no limitation")
        if status == "not-implemented" and "closing:" not in note and "deviation:" not in note:
            errors.append(
                f"matrix: {requirement} is not-implemented without closing: or deviation:"
            )
        if status == "out-of-scope" and "boundary:" not in note:
            errors.append(f"matrix: {requirement} is out-of-scope without boundary:")

    for requirement in catalog_ids:
        if requirement not in seen:
            errors.append(f"matrix: missing row for {requirement}")
    return errors, counts


def run(root: Path) -> int:
    catalog = root / CATALOG_PATH
    matrix = root / MATRIX_PATH
    try:
        catalog_text = catalog.read_text(encoding="utf-8")
    except OSError as error:
        print(f"safety traceability: cannot read {CATALOG_PATH}: {error}", file=sys.stderr)
        return 2
    try:
        matrix_text = matrix.read_text(encoding="utf-8")
    except OSError as error:
        print(f"safety traceability: cannot read {MATRIX_PATH}: {error}", file=sys.stderr)
        return 2
    errors, counts = scan(catalog_text, matrix_text, root)
    if errors:
        for message in errors:
            print(message)
        print(f"safety traceability: {len(errors)} violation(s)")
        return 1
    summary = ", ".join(f"{status} {counts[status]}" for status in STATUSES)
    print(f"safety traceability: {sum(counts.values())} rows ({summary})")
    return 0


# --- self-test -------------------------------------------------------------

_GOOD_CATALOG = (
    "# fixture | document 986 | release R24-11 | 44 pages | source sha256 00\n"
    "id\ttitle\tlevel\n"
    "RS_TEST_00001\tSupported row\ttop-level\n"
    "RS_TEST_00002\tGap row\tfunctional\n"
)

_GOOD_MATRIX = (
    "| ID | Summary | Status | Capability | Evidence | Note |\n"
    "|---|---|---|---|---|---|\n"
    "| RS_TEST_00001 | Supported | supported | `x/cap` | `tests/x/cap_test.cpp` | - |\n"
    "| RS_TEST_00002 | Gap | not-implemented | - | - | closing: some-change |\n"
)


def _expect(description: str, catalog: str, matrix: str, root: Path, needle: str) -> bool:
    errors, _counts = scan(catalog, matrix, root)
    if not errors:
        print(f"self-test FAILED: {description}: expected a violation, got none")
        return False
    if not any(needle in error for error in errors):
        print(f"self-test FAILED: {description}: no violation contained '{needle}': {errors}")
        return False
    return True


def self_test() -> int:
    with tempfile.TemporaryDirectory() as directory:
        root = Path(directory)
        (root / "openspec/specs/x/cap").mkdir(parents=True)
        (root / "openspec/specs/x/cap/spec.md").write_text("# cap\n", encoding="utf-8")
        (root / "tests/x").mkdir(parents=True)
        (root / "tests/x/cap_test.cpp").write_text("// fixture\n", encoding="utf-8")

        cases: list[tuple[str, str, str, str]] = []

        errors, counts = scan(_GOOD_CATALOG, _GOOD_MATRIX, root)
        if errors:
            print(f"self-test FAILED: valid fixture produced {errors}")
            return 1
        if counts["supported"] != 1 or counts["not-implemented"] != 1:
            print("self-test FAILED: valid fixture produced wrong status counts")
            return 1

        missing = _GOOD_MATRIX.replace(
            "| RS_TEST_00002 | Gap | not-implemented | - | - | closing: some-change |\n", ""
        )
        cases.append(("missing row", _GOOD_CATALOG, missing, "missing row for RS_TEST_00002"))

        unknown = _GOOD_MATRIX + (
            "| RS_TEST_09999 | Extra | out-of-scope | - | - | boundary: none |\n"
        )
        cases.append(("unknown id", _GOOD_CATALOG, unknown, "unknown identifier: RS_TEST_09999"))

        duplicate = _GOOD_MATRIX + (
            "| RS_TEST_00002 | Gap again | not-implemented | - | - | closing: x |\n"
        )
        cases.append(("duplicate row", _GOOD_CATALOG, duplicate, "duplicate identifier: RS_TEST_00002"))

        catalog_duplicate = _GOOD_CATALOG + "RS_TEST_00001\tAgain\ttop-level\n"
        cases.append(("duplicate catalog id", catalog_duplicate, _GOOD_MATRIX, "duplicate identifier: RS_TEST_00001"))

        bad_status = _GOOD_MATRIX.replace("| supported |", "| done |")
        cases.append(("invalid status", _GOOD_CATALOG, bad_status, "invalid status 'done'"))

        dangling_cap = _GOOD_MATRIX.replace("`x/cap`", "`x/missing`")
        cases.append(("dangling capability", _GOOD_CATALOG, dangling_cap, "capability spec is missing"))

        dangling_evidence = _GOOD_MATRIX.replace("`tests/x/cap_test.cpp`", "`tests/x/missing.cpp`")
        cases.append(("dangling evidence", _GOOD_CATALOG, dangling_evidence, "evidence path is missing"))

        partial_without_note = _GOOD_MATRIX.replace("| supported |", "| partial |")
        cases.append(("partial without limitation", _GOOD_CATALOG, partial_without_note, "states no limitation"))

        gap_without_owner = _GOOD_MATRIX.replace("closing: some-change", "-")
        cases.append(("gap without owner", _GOOD_CATALOG, gap_without_owner, "without closing: or deviation:"))

        scope_without_boundary = (
            "| ID | Summary | Status | Capability | Evidence | Note |\n"
            "|---|---|---|---|---|---|\n"
            "| RS_TEST_00001 | Supported | out-of-scope | - | - | - |\n"
            "| RS_TEST_00002 | Gap | not-implemented | - | - | closing: x |\n"
        )
        cases.append(("scope without boundary", _GOOD_CATALOG, scope_without_boundary, "without boundary:"))

        bad_provenance = _GOOD_CATALOG.replace("44 pages", "44")
        cases.append(("unpinned provenance", bad_provenance, _GOOD_MATRIX, "provenance header"))

        bad_header = _GOOD_MATRIX.replace(
            "| ID | Summary | Status | Capability | Evidence | Note |", "| ID | Status |"
        )
        cases.append(("missing table header", _GOOD_CATALOG, bad_header, "missing the | ID |"))

        failures = 0
        for description, catalog, matrix, needle in cases:
            if not _expect(description, catalog, matrix, root, needle):
                failures += 1
        if failures:
            return 1
        print(f"safety traceability self-test: {len(cases)} failing fixtures and 1 passing fixture checked")
        return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--root", default=".", help="repository root to scan (default: .)")
    parser.add_argument(
        "--self-test",
        action="store_true",
        help="run the embedded fixtures instead of scanning a tree",
    )
    args = parser.parse_args()
    if args.self_test:
        return self_test()
    return run(Path(args.root).resolve())


if __name__ == "__main__":
    sys.exit(main())
