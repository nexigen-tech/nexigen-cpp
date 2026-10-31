# Design

## Context

`examples/app/ex_app_cli_settings.cpp` self-verifies only count floors (≥10 metric batches, ≥10 chrono snapshots, ≥3 trace ids), its failure branches are unreachable from any test, and the build registers exactly one smoke test per executable via `nexigen_add_example` (`examples/CMakeLists.txt:8-14`). The example's `--help` path and its failure branches therefore have no automated coverage; the nested-span shape was verified manually once. See proposal.md for motivation.

Constraints: the example's standard options are parsed by the strict `app::parse_command_line`, which rejects unknown tokens with diagnostics and usage; the spec pins that inputs (directories and the four settings files) are prepared *before* parsing; no library code changes.

## Goals / Non-Goals

**Goals:**
- Machine-check the trace shape (parent-child edges, ≥2 span kinds) through the example's exit status.
- Exercise a real failure branch under test through a deterministic, self-contained trigger.
- Run the `--help` path under CTest without weakening the "one test per executable" contract — extended, not violated.
- Keep the normal run's behavior, runtime and artifacts byte-identical.

**Non-Goals:**
- No fault injection into library code or syscalls (the project-wide injection gap stays open).
- No JSON parser in the example — the artifact checks stay cheap string scans.
- No changes to `nexigen_add_example` (only one variant exists; a generic variant mechanism is premature).

## Decisions

1. **`--fail-demo` is pre-scanned and stripped before the standard parser.** The example already pre-scans argument tokens (`has_directory_option`); the same pass drops `--fail-demo` so `app::parse_command_line` still sees only standard options and the parser contract is untouched. Alternative rejected: an environment variable (invisible in `--help`-style discoverability) or teaching the parser a demo flag (pollutes the production CLI surface).

2. **The failure injection is one malformed settings file.** In fail-demo mode `prepare_settings()` writes invalid JSON into `metrics.json`, so the failure travels the real path the spec names — settings loading — and the example exits non-zero with a diagnostic before the workload starts. Alternatives rejected: an unwritable directory (environment-dependent, can pass accidentally), truncating an artifact (exercises verification, not the failure branches the spec lists), skipping initialization (fake branch).

3. **Trace-shape checks extend the existing cold-path line scan.** A second pass over `traces.jsonl` collects `"kind"` values and flags any non-zero `"parent_span_id"` using the same string-search style as `count_trace_ids`; no `nlohmann::json` on the example's check path. The keys are part of the sink's serialized contract, so a format change fails the example loudly instead of silently passing.

4. **The help smoke test is a plain extra `add_test`.** `examples/app/CMakeLists.txt` registers `ex_app_cli_settings_help` running `ex_app_cli_settings --help` with the same `TIMEOUT 600`/`LABELS examples` properties; the `examples` spec delta is what legitimizes `<executable>_<variant>` names alongside the per-executable test. Note the help run does create `nexigen-example-settings/`/`nexigen-example-output/` — spec'd behavior ("inputs are prepared before parsing"); the smoke asserts exit 0 and usage on stdout, not the absence of directories.

## Risks / Trade-offs

- [`--fail-demo` could be mistaken for a real option] → It never reaches the parser, is example-local, and gets documented in the README bullet next to the standard options; the usage text stays authoritative for the standard CLI.
- [String-scan checks are brittle against sink format changes] → Intended: the scan keys (`trace_id`, `parent_span_id`, `kind`) are serialized-contract fields; a change makes the example fail, which is the signal to update the check.
- [The malformed-JSON trigger couples to the settings loader's error path] → The loader's fail-fast contract ("Settings-loading failures are fail-fast", `app/application`) is itself spec'd; if its diagnostics change wording, the example still only asserts "diagnostic on stderr + non-zero exit".
- [Extra CTest entries multiply across the four build modes] → Both new entries are near-instant (`--help` exits early; fail-demo exits before the workload), so the matrix cost is negligible.
