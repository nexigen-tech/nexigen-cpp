# Proposal

## Why

The safety programme's phases 0–3 are archived, but the traceability matrix still
defers several gaps to changes that have already landed ("arrives with
close-phm-safety-loop", "arrives with add-e2e-protection"). Some rows therefore
understate coverage, and `docs/em-deferred.md` still carries a 2026-11-10 audit
that lists dependencies and Function Group configuration as unimplemented even
though both exist. An audit trail that lags reality is worse than no trail: it
hides what is done and what is actually left.

This is a documentation-only change. No behavior, API, or manifest changes.

## What Changes

- Re-evaluate the matrix rows that referenced now-archived work:
  - `RS_SAF_10030` (safe program execution): fork/join is implemented, so the
    row moves from `partial` to `supported`.
  - `RS_SAF_10037` (prevent unintended alteration): drop the stale "arrives
    with add-e2e-protection" note; E2E detects alteration, prevention remains
    OS isolation — stays `partial`.
  - `RS_CM_00403` (response deadline): E2E status is available but deadline
    supervision stays application-level — stays `partial`, note corrected.
  - `RS_SAF_00001` (abstract): its derived-row list now names only the rows that
    are still partial.
- Refresh `docs/em-deferred.md`:
  - Move `REQ_EM_001` (dependencies) and `REQ_EM_005` (Function Group
    configuration) to implemented; the audit currently predates them.
  - Record that `REQ_EM_010` is partially met by executable and manifest
    SHA-256 verification; signatures and a security log remain open.
- Re-run the traceability gate and OpenSpec validation.

## Capabilities

### New Capabilities

None.

### Modified Capabilities

None. The change only corrects documentation content; the
`quality/safety-traceability` contract (one row per requirement, validated
links) is unchanged. `skip_specs: true` records that deliberately.

## Impact

- **Code**: none.
- **Performance**: none.
- **Docs**: `docs/safety/traceability.md`, `docs/em-deferred.md`.
- **Compatibility**: none.
