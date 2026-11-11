# Design

## Context

See `proposal.md` — Why. Constraints that shape the split:

- Examples are compiled and run as CTest smoke tests, each in its own working directory with `NEXIGEN_RUN_DIR` set, so they may rely on cwd-relative paths and on process-lifetime objects (sinks flush when destroyed).
- Several examples end with `expect(...)` self-checks and programmatic verification of the JSONL they just wrote; those assertions must keep running in the same order.
- `main` is the entry point a reader copies; the split must not turn it into a dispatcher table.

## Goals / Non-Goals

**Goals:**

- Every example ≤ CCN 15 measured by lizard, including `main`.
- `main` reads as a short, numbered narrative that mirrors the step comments.
- Byte-identical program output and exit codes; identical CTest registrations.

**Non-Goals:**

- Sharing helpers across examples (single-file self-containment wins).
- Reordering demonstrated API calls or changing any message.
- Making examples participate in the complexity gate (`examples/` stays excluded).

## Decisions

### D1 — Extract steps as free functions in the file's anonymous namespace

Each example already keeps helpers in an anonymous namespace above `main`. The steps join them as `step_<verb>(...)`, taking only what they need (`sink&`, `config`, `path`). Alternative: a `struct Demo { void step1(); ... }` — rejected because it hides the lifetime boundaries that matter here (loggers flush on destruction) and adds boilerplate to a teaching file.

### D2 — Preserve lifetime boundaries exactly

Where the current `main` opens a scope so a sink flushes before the file is read back, the step function ends at the same boundary. The step returns after its scope closes, and the next step reads the file. No object outlives or dies earlier than before.

### D3 — `main` stays a numbered narrative

The body becomes `step_prepare()`, `step_emit()`, `step_read_back()` … with the existing `// --- ... ---` comments promoted to function-level comments. A step returns `int` only where the original `main` had an early-exit path; otherwise `void`.

### D4 — Verification is output equality, not code review

Before touching the files, capture `ctest -L examples -V` (21 tests). After the split, capture it again and diff. Examples with measured durations (stopwatch, latency-style prints) may differ in numeric fields; those are inspected individually and accepted only when the differing tokens are timings, not messages or ordering. Exit codes and `expect()` assertions must match exactly.

## Risks / Trade-offs

- **A sink flushes later than before** → the read-back step would see a short file; keeping the original scope boundaries (D2) and the byte-identical output check catch it immediately.
- **A step needs too many parameters** → if a step grows past five parameters, keep it inline and extract a smaller inner piece instead of threading state.
- **Output diff noise from timings** → compare the transcripts and classify each differing line; accept only timing tokens.
- **The `app_cli_settings` example writes and reads settings files in cwd** → the split must keep the `prepare_settings`/`--help` sequencing; the CTest `ex_app_cli_settings_help` variant covers the early-exit path.
