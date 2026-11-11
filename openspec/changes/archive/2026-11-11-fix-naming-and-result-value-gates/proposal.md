# Proposal

## Why

Two repository lint jobs are red on committed `HEAD`, independent of any feature work: `python3 cmake/check_naming.py` reports 9 snake_case type/enum/alias names and `python3 cmake/check_result_value.py` reports 1 non-Result `.value()` receiver. They live in four files that no recent change touched (`tools/nexigen-exec/main.cpp`, `tools/nexigen-hub/main.cpp`, `tests/tracing/latency_test.cpp`, `tests/tracing/tracer_provider_test.cpp`, `tests/tracing/record_format_test.cpp`), so the failures are pure drift from the earlier one-shot renames and the newer guard allowlist.

Both jobs are required CI steps. While they are red, a genuine regression in either rule is invisible, and the pipeline can never be fully green after the complexity gate was fixed.

## What Changes

- Rename the snake_case declarations flagged by the naming guard to PascalCase:
  - `tools/nexigen-exec/main.cpp`: `Start_args` → `StartArgs`, `Stop_args` → `StopArgs`, `Status_args` → `StatusArgs`, `Start_scan` → `StartScan`, `String_field` → `StringField`;
  - `tools/nexigen-hub/main.cpp`: `Hub_options` → `HubOptions`;
  - `tests/tracing/latency_test.cpp`: `Bench_provider` → `BenchProvider` (two function-local aliases);
  - `tests/tracing/tracer_provider_test.cpp`: `Big_provider` → `BigProvider`.
- Give the one flagged test local an allowlisted receiver name (`span_keep` → `span`), which is exactly the "local ID value used by a test" case the result-value guard documents, instead of opting out with a comment.
- No public header, CLI flag, exit code, diagnostic text or test expectation changes.

## Capabilities

No capability deltas: the change is a pure rename plus one test-local variable name, with no observable behavior change (`.openspec.yaml` sets `skip_specs: true`).

## Impact

- **Code:** `tools/nexigen-exec/main.cpp`, `tools/nexigen-hub/main.cpp`, `tests/tracing/latency_test.cpp`, `tests/tracing/tracer_provider_test.cpp`, `tests/tracing/record_format_test.cpp`.
- **APIs:** none. Every renamed declaration is file-local (tool-internal structs/enum, function-local aliases). No installed header changes, so no symbol or ABI change.
- **Performance / latency / memory:** none. Identifiers only; no instruction, allocation, layout or `std::memory_order` change. Latency gates must report their previous numbers.
- **Gates:** `check_naming.py` and `check_result_value.py` become green.
- **Out of scope:** the `format-check` gate. It currently fails for 115 files because the committed tree is not idempotent under clang-format 22.1.8 (both `template <>\nstruct` and `template <class T>\nauto` spellings exist, and no `.clang-format` setting reproduces the committed bytes). That is a repository-wide formatting migration and gets its own change.
