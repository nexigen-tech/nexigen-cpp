# Tasks

## 1. Pinned catalog

- [x] 1.1 Extract the 62 requirement identifiers, titles, and levels from `AUTOSAR_FO_RS_Safety` R24-11 into `docs/safety/rs-safety-r24-11.tsv`; verify the row count is 62 and `cut -f1 docs/safety/rs-safety-r24-11.tsv | tail -n +3 | sort | uniq -d` prints nothing.
- [x] 1.2 Add the provenance header (document 986, release R24-11, 44 pages, source PDF SHA-256); verify the recorded hash equals `sha256sum /home/v86e9/AUTOSAR/AUTOSAR_FO_RS_Safety.pdf` and the page count equals `pdfinfo` output.

## 2. Traceability matrix

- [x] 2.1 Write the matrix legend — the four statuses, their obligations, the "abstract rows are never `supported` alone" rule, and where row summaries come from — in `docs/safety/traceability.md`; verify the legend names all four statuses and the D4 rule.
- [x] 2.2 Add the programme map section naming this change and the three follow-ups (`close-phm-safety-loop`, `add-integrity-primitives`, `add-e2e-protection`) with the requirement clusters each owns; verify every follow-up name also appears in at least one row note.
- [x] 2.3 Fill the 23 abstract rows (7 TLSR + 16 FSR) as `partial` with a note naming their derived requirements, except where every derived row is `supported`; verify no abstract row is `supported` while a derived row is `not-implemented`.
- [x] 2.4 Fill the technical rows already covered by the platform (HM, PHM, SM, EM, OSI, CM) with `supported` or `partial`, an existing `openspec/specs/<path>/spec.md` in `capability`, an existing test or spec path in `evidence`, and the limitation text for every `partial`; verify the gate accepts each row.
- [x] 2.5 Fill the remaining technical rows (E2E, watchdog, checkpoint identity, integrity, time sync) as `not-implemented` with the closing change named; verify no `not-implemented` row is left without a closing change.
- [x] 2.6 Add the declared-deviation rows — 8 Classic Platform (`RS_SAF_311xx`–`313xx`), 4 UCM, 3 PER (`RS_PER_00008`, `RS_PER_00009`, `RS_SAF_10040`) — as `out-of-scope` with the product-boundary rationale; verify the parser counts exactly those 15 rows and every one has a non-empty rationale.

## 3. Static gate

- [x] 3.1 Implement `cmake/check_safety_traceability.py` with `--root` (stdlib only) parsing the TSV and the Markdown table, enforcing 1:1 ids, the status vocabulary, per-status required fields, and link existence; verify `python3 cmake/check_safety_traceability.py` exits 0 on the completed tree and prints the per-status row counts.
- [x] 3.2 Add `--self-test` with inline fixtures for a missing row, an unknown id, a duplicate id, an invalid status, a dangling capability path, a dangling evidence path, and a `partial` row without a limitation; verify `python3 cmake/check_safety_traceability.py --self-test` exits 0 and each fixture asserts a non-zero scan result.
- [x] 3.3 Document the gate's exit codes and table grammar in the script docstring; verify the docstring names both files, both exit codes, and the column order the parser expects.

## 4. Wiring

- [x] 4.1 Add the traceability step to `.github/workflows/ci.yml` beside `check_naming.py`; verify the workflow file still parses and the added `run:` line executes the same command locally with exit 0.
- [x] 4.2 Add the gate to the `scripts/validate.sh` static-gate block through `timed`; verify `scripts/validate.sh --tier module` prints the `safety_traceability` line with `rc=0`.
- [x] 4.3 Add the matrix to the mkdocs navigation and link it from `docs/architecture.md`; verify the nav path resolves to `docs/safety/traceability.md` and `mkdocs build --strict` succeeds when mkdocs is installed (otherwise verify the nav target file exists).

## 5. Integration verification

- [x] 5.1 Run `openspec validate --changes --strict` and `openspec validate --specs`; verify both exit 0.
- [x] 5.2 Run the three static gates in CI order (`check_naming.py`, `check_result_value.py`, `check_safety_traceability.py`); verify all three exit 0 from a clean checkout without a build directory.
- [x] 5.3 Reconcile the matrix against the source document: verify the status counts sum to 62, the 15 deviation rows match the declared clusters, and the summary printed by the gate matches the matrix.

## Workflow follow-up

- Archive the change after review.
- Verify the archived capability `quality/safety-traceability` exists under `openspec/specs/`.
