# Design

## Context

See `proposal.md` — Why. The relevant current state:

- The project already keeps 115 capabilities under `openspec/specs/`, but none
  of them reference `AUTOSAR_FO_RS_Safety`.
- Static gates already live in `cmake/check_*.py` (Python 3, standard library
  only, `--root` and `--self-test` flags) and run from CI plus
  `scripts/validate.sh`.
- Manifests are versioned and strictly validated (`SuiteManifest::version`,
  "must be 1"), so later safety changes add optional sections rather than a
  new document format. This change does not touch them.
- The source document is a 44-page PDF; the requirement set is small (62
  identifiers) and stable within release R24-11.

## Goals / Non-Goals

**Goals:**

- One canonical, pinned list of requirement identifiers.
- One human-reviewable matrix with a status per requirement and verifiable links.
- A gate that catches structural drift early and needs no build or network.
- A clear home for the programme's declared deviations.

**Non-Goals:**

- Proving that a `supported` row is semantically true; the gate checks structure
  and links, review checks the claim.
- Reproducing AUTOSAR requirement text (licence); rows carry our own summary.
- Covering the component SWS documents (PHM/SM/EM/…); the catalog is exactly
  `AUTOSAR_FO_RS_Safety` R24-11.
- Any runtime code, manifest field, or API.

## Decisions

**D1 — Two files: a pinned catalog and a living matrix.** The catalog
(`docs/safety/rs-safety-r24-11.tsv`) is the immutable set of identifiers; the
matrix (`docs/safety/traceability.md`) is the evolving status view. A single
file cannot detect its own omissions: a deleted row would simply disappear.
Splitting them makes "missing row" a first-class, checkable failure.
*Alternative rejected:* encoding the id list as a constant inside the checker —
the data would hide in code and become invisible to reviewers.

**D2 — TSV for the catalog, a Markdown table for the matrix.** The catalog is
data (never edited after extraction, diffed only when the AUTOSAR release
changes), so TSV gives unambiguous parsing and a clean diff. The matrix is a
review artifact, so a Markdown pipe table renders on GitHub and in the mkdocs
site and is readable in a pull request.
*Alternatives rejected:* YAML/JSON for the matrix (accurate but opaque in
review), CSV for the catalog (quoting rules for titles that contain commas).

**D3 — Four statuses with per-status obligations.** `supported` (mechanism
exists, evidence linked), `partial` (mechanism exists with a named limitation),
`not-implemented` (gap with a named closing change or accepted-deviation
reason), `out-of-scope` (outside the product boundary, with the boundary
stated). Binary supported/not-supported would erase the difference between
"we chose not to build this" and "we have not built this yet", which is exactly
the distinction an auditor asks about.

**D4 — Abstract rows never claim `supported` on their own.** The 7 top-level
and 16 functional requirements are satisfied only through their derived
technical requirements. Their rows carry `partial` (with a note naming the
derived rows) or `not-implemented`, never `supported`, unless every derived row
is `supported`. The gate cannot evaluate this; it is a review rule recorded in
the matrix's legend so the convention is visible.

**D5 — The gate is a stdlib-only script in `cmake/`, not a CTest binary.** It
follows `cmake/check_naming.py`: `--root`, `--self-test`, human-readable
findings, non-zero exit. CI runs it beside the existing `check_*.py` steps, and
`scripts/validate.sh` runs it through `timed` (a full-tree invariant, not a
working-set-filtered gate). A CTest target would force a configure and build
before the cheapest check in the repository can run.
*Alternative rejected:* a unit test in `tests/` — it would need the build and a
test framework dependency to read two text files.

**D6 — Links are verified by existence, not by a second registry.** The gate
extracts backticked repo-relative tokens containing `/` from the `capability`
and `evidence` cells and requires each to exist; a capability token must resolve
to `openspec/specs/<token>/spec.md`. This keeps a single source of truth (the
filesystem) and makes stale links fail loudly.

**D7 — `--self-test` fixtures live inside the gate.** Small inline catalog and
matrix strings exercise every failure mode (missing row, unknown id, duplicate
id, bad status, dangling path, missing limitation). This gives the gate its own
regression coverage without a build, matching `check_naming.py`.

**D8 — Row summaries are ours, not AUTOSAR's.** Each row carries a one-line
paraphrase and the identifier. AUTOSAR permits informational reproduction but
not redistribution of substantial text; identifiers plus short titles are
enough for traceability and keep the repository clean of licensed prose.

## Risks / Trade-offs

- **A green gate can be mistaken for real coverage.** → The matrix legend says
  the gate checks structure, not truth; `supported` requires an executable test
  path in `evidence`, and review challenges rows without one.
- **The matrix rots after the first sprint.** → Convention: a change that closes
  a gap edits the matrix in the same change; the gate's "supported needs
  evidence" rule makes a stale row visible as soon as the referenced test moves.
- **AUTOSAR publishes R25-11 and the catalog ages.** → The header pins the
  release and the PDF SHA-256; a new release is a new catalog file plus a
  deliberate change, never an in-place edit.
- **Gate and matrix formats drift apart.** → Both the parser and the fixtures
  are in one file; the self-test fails when the table grammar changes without
  the parser.
- **The catalogue duplicates content the PDF already has.** → Only identifiers,
  titles, and levels are copied; that is the minimum needed to detect a missing
  requirement. It is ~62 short lines.

## Migration Plan

Additive and reversible: delete `docs/safety/`, the gate, and the two gate
invocations to roll back. No manifest, API, or wire-format migration.

## Open Questions

- Once AUTOSAR folds the extracted technical requirements into the component
  SWS documents (the source document says this is the plan), should the catalog
  grow to reference those SWS identifiers? This does not change the matrix
  structure or the gate, so it can be decided with the next release.
