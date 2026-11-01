# Design

## Context

See `proposal.md` — Why. Coverage is measured by `cmake/coverage_report.py`;
the CI coverage job already runs the `coverage` target and a `Result::value()`
guard. The Clang job builds with `build/clang/compile_commands.json`, which is
the right database for clang-tidy because GCC-only warning flags are absent.

## Goals / Non-Goals

**Goals:**

- Exceed the 95% overall coverage gate with meaningful fault-path tests.
- Enforce the gate in CI.
- Run a focused clang-tidy profile over production sources without adding
  external runtime dependencies.

**Non-Goals:**

- No coverage-guided fuzzing infrastructure (the deterministic corpus remains).
- No clang-tidy run over tests/examples (they use intentionally different
  patterns).
- No `clang-tidy --fix` in CI; findings are fixed in reviewable commits.

## Decisions

### D1: Target remaining failure branches, not synthetic line hits

The added tests exercise real contracts:

- Streaming: replace a tailer path, use a directory as a stream (open succeeds,
  read fails), and verify stat/read diagnostics.
- HTTP: percent-encoded stream IDs, replay gaps from a bounded ring, live gaps
  from an over-long line, and a failed initial sync.
- Discovery: standalone instances in running/stopped/unknown states, attempt
  name edge cases, and combined app→suite state.
- Runner: settings write failures, stream-file preparation failures, completed
  records with a live process group, and hostile metadata/status JSON.
- Suite: metadata write failure, invalid logging settings fallback, and a live
  supervisor stop.

Alternatives considered: lowering the gate to 94.9% (rejected: the documented
target is 95%) and mocking syscalls (rejected: no dependency and no fault
injection framework).

### D2: Clang-tidy profile with documented exclusions

The profile enables `bugprone-*`, `performance-*`, `portability-*`, and
`clang-analyzer-*`, with warnings as errors. Exclusions:

- `bugprone-easily-swappable-parameters` — string_view APIs are intentional.
- `bugprone-unused-return-value` — the project idiom is
  `static_cast<void>(status)` for best-effort control-plane results.
- `bugprone-signed-char-misuse`, `bugprone-branch-clone`,
  `bugprone-tagged-union-member-count` — intentional ASCII parsing and compact
  hot-path layouts.
- `performance-unnecessary-value-param` — `std::stop_token`/`string_view` are
  cheap values.
- `portability-avoid-pragma-once` — the project standardizes on `#pragma once`.
- `clang-analyzer-optin.*` — opt-in analyzers flag intentional alignment padding.

CI runs:
```bash
git ls-files 'src/*.cpp' | xargs -n 1 -P 2 clang-tidy -p build/clang -quiet
```
`HeaderFilterRegex` covers `include/` and `src/`, so header findings surface
through the source TUs.

Alternatives considered: enabling every check (rejected: 500+ noisy findings,
mostly pragma-once and parsing idioms) and not running clang-tidy at all
(rejected: the user selected it for P4).

### D3: Fix findings in production code, not by suppressions

Resolved findings:

- enum types sized with `std::uint8_t`;
- `parse_hex16` casts operands before widening;
- explicit `Meter**`→`void*` conversion for `memset`;
- `states.reserve()` in discovery;
- optional guards in `Async_worker::run`/`publish_sync`;
- `Producer` drop-summary casts per operand;
- two documented `NOLINTNEXTLINE` exceptions where a `string_view` is known to
  be NUL-terminated by construction.

## Risks / Trade-offs

- **95% gate is tight (95.1%).** → Added tests target real branches; the margin
  is small but deterministic. Future coverage drops fail CI as intended.
- **clang-tidy version drift.** → The CI container installs the distribution
  `clang-tidy` alongside the chosen Clang; the profile avoids version-specific
  checks.
- **Header findings duplicate across TUs.** → `-quiet` keeps logs readable and
  the first failure is actionable.
- **Enum size changes could affect ABI.** → These enums are internal to `.cpp`
  translation units; no public type changes.

## Migration Plan

No migration. Revert is a normal revert; no persisted data or API changes.

## Open Questions

None.
