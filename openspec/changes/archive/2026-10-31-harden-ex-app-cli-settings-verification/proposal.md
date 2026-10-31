# Proposal

## Why

The verification of `extend-ex-app-cli-settings-workload` left two warnings and one suggestion: the example's `--help` exit path is exercised by no automated test, none of its failure branches is ever injected (they exist only as code), and its self-verification enforces only a count of trace ids while the nested-span shape (parent-child edges, span kinds) was verified by hand. Closing these gaps turns the example's behavior contract into machine-checked behavior instead of a one-time manual inspection.

## What Changes

- The example gains a **`--fail-demo` mode** (pre-scanned and stripped before the standard command-line parser, so the parser contract is untouched): it deliberately writes one malformed settings file, so initialization fails through the real settings-loading path, prints a diagnostic to stderr and exits non-zero — exercising the "Failures are reported" branch for automated verification.
- The example's **end-of-run self-verification is strengthened**: besides the existing floors (≥10 metric batches, ≥10 chrono snapshots, ≥3 trace ids) it must also assert at least one parent-child span edge and at least two distinct span kinds in `traces.jsonl`, failing the run when the trace shape is missing.
- The build registers an additional **argument-driven smoke test** `ex_app_cli_settings_help` that runs the example with `--help` (exit 0, usage printed, no infrastructure created), alongside the existing per-executable smoke test.

No breaking changes: the standard command-line options, the settings/output file names and the exit-code contract of a normal run are unchanged.

## Capabilities

### New Capabilities

(none)

### Modified Capabilities

- `app/application`: the "Runnable command-line settings example" requirement gains the `--fail-demo` failure-injection behavior and strengthens the "Several traces with nested spans" scenario so the trace shape is enforced by the example's own exit status.
- `examples`: the "CTest smoke coverage for every example" requirement must allow and require argument-driven smoke variants (currently it mandates one test per executable named after the executable, which a help smoke test would sit against).

## Impact

- **Affected code:** `examples/app/ex_app_cli_settings.cpp` (arg pre-scan, fail-demo injection, extended artifact checks) and `examples/app/CMakeLists.txt` (one extra `add_test` entry). No library headers or sources change.
- **Performance:** no hot-path impact — the extended checks run after shutdown on the cold path and scan the artifact files once; `--fail-demo` short-circuits before the workload, so it adds no runtime to a normal run.
- **Latency (ns):** zero change to library latency budgets and to the recording paths; the example's post-run checks are not a latency surface.
- **Memory allocation:** unchanged on hot paths; the extended trace-shape check reuses the existing cold-path line scan and may allocate only in post-run verification (allowed and expected).
- **Test/CI time:** one extra near-instant CTest entry (`--help` exits before any workload) and one `--fail-demo` verification run that fails before the 15-second workload; normal-run time is unchanged.
