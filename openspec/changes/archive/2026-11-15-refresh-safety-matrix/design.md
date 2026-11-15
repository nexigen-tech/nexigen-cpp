# Design

## Context

See `proposal.md` — Why. The matrix is machine-checked for structure (one row
per pinned requirement, valid status, resolving links); the *content* of a note
is reviewed, not generated. That is exactly why stale notes survive: the gate
cannot tell "arrives with X" from a real limitation.

## Goals / Non-Goals

**Goals:**

- Make every row's status match the evidence that now exists.
- Keep the em-deferred audit honest about what is implemented.

**Non-Goals:**

- Changing any status without implementation evidence (e.g. leaving
  `RS_PHM_00115/00116` partial until SM/EM are actually supervised).
- Rewriting the matrix's structure or the gate.

## Decisions

**D1 — Only promote a row when its implementation and evidence exist.** The
gate already enforces links; promotion additionally requires the scenario-level
evidence added by the prior changes (`phm/supervision` fork/join tests for
10030). Rows whose mechanism is detect-only keep `partial` with a corrected
note.

**D2 — The em-deferred audit is refreshed, not deleted.** The document's value
is the list of consciously deferred work; updating the baseline keeps that list
credible. Items that moved to implemented are recorded as such instead of being
silently removed, so a reader can see the history of the decision.

**D3 — `skip_specs: true`.** No requirement changes: the matrix contract is
already specified in `quality/safety-traceability`, and this change edits data,
not behavior. Inventing a requirement to avoid the marker would be worse.

## Risks / Trade-offs

- **A promoted status could be wrong.** → Promotion is limited to rows whose
  scenarios and tests landed in an archived change; the matrix's evidence links
  point at those tests.
- **The em-deferred audit can drift again.** → The next EM audit should be part
  of the change that touches EM behavior, not a standalone doc pass.

## Migration Plan

None. Documentation-only; rollback is the previous file contents.
