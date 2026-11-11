# Design

## Context

See `proposal.md` — Why. Two facts, both verified against clang-format 22.1.8 with the repository style:

- `BreakStringLiterals: false` prevents the formatter from *creating* a split. It does not merge literals that are already split, and it does not prevent moving the whole literal to its own line when the surrounding expression does not fit.
- The bulk reformat introduced non-whitespace changes in exactly two files: `tests/logging/log_daemon_test.cpp` (macro expansion, already accepted) and `tests/metrics/prometheus_render_test.cpp` (six literal splits). Every other adjacent-literal site in the tree predates that commit and is a deliberate wrapped message.

## Goals / Non-Goals

**Goals:**

- Long expectations stay a single greppable string, now and for future tests.
- `format-check` stays green and idempotent under the new policy.
- Proof that the rejoin is not a semantic edit.

**Non-Goals:**

- Joining the 133 pre-existing adjacent literals across examples and headers; they are intentional formatting of long messages.
- Changing `ColumnLimit` or any other style option.
- Reformatting unrelated files.

## Decisions

### D1 — Policy change plus targeted rejoin, not a mechanical "join everything"

A blanket merge would rewrite 133 intentional multi-line messages. Instead the option fixes the root cause for new code, and the six known artefacts from `838d1b0` are rejoined by hand. The proposal records the distinction so a future reader does not "finish the job" and reformat every wrapped message.

### D2 — Rejoin by merging raw literal bodies, then reformat

Each pair `"A"` `"B"` becomes `"AB"` with the escape sequences untouched, which is exactly what the formatter had split. clang-format then re-indents the statement; no other byte changes. Verification compares the concatenation of every literal body in the file before and after (1605 bytes both sides), which is stronger than eyeballing the diff.

### D3 — Prove the option actually does the work

Negative control: with the option removed, `clang-format --dry-run --Werror` on the test file fails again with `code should be clang-formatted` at the rejoined literal. With the option restored it passes — so `format-check` greenness depends on the policy, not on luck.

## Risks / Trade-offs

- **Very long literals hurt line-based review** → only literal-bearing lines exceed the limit, and they are exactly the lines a reader wants unbroken.
- **Another formatter version changes how `BreakStringLiterals: false` interacts with indentation** → `format-check` runs in CI with the pinned 22.1.8; the idempotency check catches drift locally.
- **Someone re-joins deliberate messages later** → the proposal and design both name the 133-site boundary.
