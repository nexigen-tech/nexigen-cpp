# Proposal

## Why

The project now treats `AUTOSAR_FO_RS_Safety` (document 986, release R24-11,
62 requirements) as its safety checklist, but coverage is currently anecdotal:
the exploration that started this work found an open recovery loop, a
watchdog that exists only as an enum value, and two subsystems (E2E, PER)
that are simply absent. Without a single, machine-checked mapping from each
`RS_*` requirement to a capability and its evidence, every later safety claim
is unverifiable, and the follow-up changes cannot state precisely what they
close or what they deliberately deviate from.

The matrix must land first because it is the acceptance frame for the whole
safety programme: it names the three follow-up changes, fixes the deviation
vocabulary, and pins the source document so the mapping cannot silently drift
when AUTOSAR publishes the next release.

## What Changes

- Add a pinned requirement catalog, `docs/safety/rs-safety-r24-11.tsv`:
  one row per requirement (`id`, `title`, `level`), extracted once from the
  PDF, with the document identity (ID 986, R24-11, 44 pages) and the PDF
  SHA-256 recorded in the file header.
- Add the living matrix, `docs/safety/traceability.md`: every catalog row with
  `status` (`supported`, `partial`, `not-implemented`, `out-of-scope`), the
  owning OpenSpec capability, concrete evidence (spec path and/or test path),
  and a limitation or deviation note where applicable.
- Add `cmake/check_safety_traceability.py`, a static gate that cross-checks
  catalog and matrix: exact 1:1 coverage, no unknown or duplicate ids, valid
  statuses, mandatory evidence for `supported`/`partial`, a limitation note for
  `partial`, and a named closing change or accepted deviation for
  `not-implemented`.
- Wire the gate into CI next to the existing `check_naming.py` /
  `check_result_value.py` steps and into the `scripts/validate.sh` static-gate
  tier.
- Publish the matrix in the documentation site (mkdocs nav) and link it from
  `docs/architecture.md`.
- Record the programme map: this change (phase 0) plus the three follow-up
  changes `close-phm-safety-loop`, `add-integrity-primitives`, and
  `add-e2e-protection`, including which requirement clusters each one owns.

Explicitly out of scope for the programme (and therefore permanently declared
as deviations in the matrix unless a later decision reopens them): AUTOSAR
Classic Platform BSW requirements (`RS_SAF_311xx`, `312xx`, `313xx`), UCM
(`RS_UCM_*`), and PER (`RS_PER_*`, `RS_SAF_10040`).

## Capabilities

### New Capabilities

- `quality/safety-traceability`: the repository keeps a validated mapping from
  every `AUTOSAR_FO_RS_Safety` requirement to a capability, evidence, and
  status, and a static check fails the build when the mapping drifts.

### Modified Capabilities

None. This change adds process evidence only; no runtime behaviour changes.

## Impact

- **Code**: no library or tool code changes. One new static checker,
  `cmake/check_safety_traceability.py`, runs in the cold CI path only.
- **Performance**: zero runtime impact — no code is linked into any target, so
  there is no latency contribution and no allocation. The checker runs once per
  CI job and reads two text files.
- **Memory**: no runtime memory. The checker holds both files in memory
  (well under 1 MiB).
- **Docs and CI**: `docs/safety/`, `mkdocs.yml`, `docs/architecture.md`,
  `.github/workflows/ci.yml`, `scripts/validate.sh`.
- **Dependencies**: none. The checker uses the Python 3 standard library only,
  matching the existing `cmake/check_*.py` gates.
- **Compatibility**: additive. No manifest, API, or wire format changes.
