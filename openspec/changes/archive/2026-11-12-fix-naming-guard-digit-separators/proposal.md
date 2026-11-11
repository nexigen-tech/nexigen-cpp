# Proposal

## Why

`cmake/check_naming.py` and `cmake/check_result_value.py` share a literal/comment stripper that treats every apostrophe as the start of a character literal. C++14 digit separators (`250'000`, `0xA'B`) therefore open a bogus "literal" that is only closed by the next apostrophe — potentially hundreds of lines later. Everything in between is blanked out and never checked.

The guard silently missed two existing violations, which proves the hole is real:

| File | Hidden declaration |
|---|---|
| `examples/metrics/ex_counters_and_gauges.cpp` | `class Capture_sink` — swallowed after `constexpr int per_worker = 250'000;` |
| `tests/tracing/tracer_provider_test.cpp` | `using Stress_provider = ...` |

Both gates report clean today, so any future snake_case type or `Result::value()` placed after a digit separator would pass CI unnoticed.

## What Changes

- Teach both strippers to skip an apostrophe that sits **between two hex digits** (the digit-separator rule), instead of opening a character literal. Character literals, including prefixed ones (`L'x'`, `u8'x'`) and escaped quotes, keep the existing handling.
- Rename the two declarations the fix reveals: `Capture_sink` → `CaptureSink`, `Stress_provider` → `StressProvider`.
- Add a `--self-test` mode to both guards that scans embedded fixtures and asserts the expected verdicts (a digit separator must not hide a following violation; a genuine character literal must still be ignored), and register one CTest entry so the regression stays caught.

## Capabilities

No capability deltas: guard tooling only, no system behavior change (`.openspec.yaml` sets `skip_specs: true`).

## Impact

- **Code:** `cmake/check_naming.py`, `cmake/check_result_value.py`, two declarations in an example and a tracing test, one CTest registration.
- **Behavior:** the guards become stricter — exactly the intended fix. No library, CLI or API change; the two renames are file-local.
- **Performance / latency / memory:** none; the guards run over source text in CI and on demand.
- **Risk:** a false positive if the separator rule misclassifies something. The rule is narrow (both neighbours must be hex digits) and the self-test covers `L'x'`, `'0'` and `1'000`.
