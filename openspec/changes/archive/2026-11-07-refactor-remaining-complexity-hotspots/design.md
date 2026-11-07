# Design

## Context

See `proposal.md` — Why. The five hotspots are all behaviour-preserving
refactors. `hub/discovery` and `logging/rotation` run on the hub control loop
and the logging consumer thread respectively; neither is on a producer hot
path. The two CLI entry points are cold start/stop/status code. The two test
functions are test-only.

The project mandates `std::expected` / `core::Status` errors (no exceptions),
fixed scratch buffers for `open_segment`, and byte-identical CLI output.

## Goals / Non-Goals

**Goals:**

- Bring every targeted function to CCN ≤ 15.
- Preserve public headers, on-disk contracts, signal sequences, output text,
  exit codes, timing gates, and test assertions exactly.
- Keep `open_segment` allocation-free and on its fixed scratch buffers.
- Keep the refactors local: no new public symbols, no new dependencies.

**Non-Goals:**

- No behaviour, API, or contract changes.
- No new tests beyond splitting the existing oversized test bodies; coverage
  must stay equal or better.
- Do not touch unrelated lizard findings (examples, other tests) in this
  change.

## Decisions

### 1. `Discovery::scan_suite` — file-local helpers

`scan_suite` is a private member already. To avoid changing the public header,
the decomposition uses file-local helpers in `src/hub/discovery.cpp` that take
the data they need:

- `load_suite_document(suite_dir, metadata) -> bool` — read + parse
  `suite.json`.
- `apply_suite_streams(root, suite_dir, metadata, out)` — fill
  `suite_id`/`suite_dir`/`artifacts_path`/`runner_log` with the existing
  containment checks.
- `apply_suite_outcome(suite_dir, out) -> bool have_status` — read
  `suite-state.json` / `suite-exit.json`, apply restarts and reason.
- `upsert_app(out, app_index, app_name) -> std::size_t` — dedupe apps.
- `build_suite_instance(root, app_meta, have_status, status) -> HubInstance` —
  containment, `instance_state`, recorded-state override, `scan_attempts`.
- `derive_app_states(out)` and `derive_unknown_suite_state(out)` — the two
  `combine_app_state` passes.

`scan_attempts` and `instance_state` stay as they are; helpers receive `root`
so they can call them. The helper signatures use references and return
existing model types, so no observable change.

### 2. `open_segment` — phase helpers on the same scratch buffers

Split into:

- `validate_segment_request(base_dir, file_name) -> bool`.
- `ensure_directory(const char* path) -> core::Status` — one `mkdirat` call
  with the existing `EEXIST` handling.
- `build_directory(kind, custom_ns, utc_ns, base_dir, scratch) -> core::Status`
  — fill `scratch.dir`, including the rotation label.
- `build_file_path(scratch, effective_name, effective_len) -> bool`.
- `open_append(const char* path, int& fd_out) -> core::Status`.

`segment_name`, `period_label`, and `put` are unchanged. All buffers stay
caller-owned; no allocation is introduced.

### 3. `nexigen-run` CLI — per-verb option structs and shared target resolution

- Add `struct Start_args`, `Stop_args`, `Status_args` and parsers
  `parse_start_args`, `parse_stop_args`, `parse_status_args` returning an
  `int` sentinel (`0` = parsed, otherwise the exit code already reported).
- Extract the duplicated suite-target block used by `cmd_stop`/`cmd_status`
  into `resolve_suite_target(config, suite_dir, target, log_target) -> int`.
- Keep `cli_ok`, `cli_fail`, `suite_target_options_conflict`, and every existing
  `fprintf` string verbatim. `cmd_*` becomes: parse → resolve → dispatch →
  report.
- `start` keeps its `--` command-mode and per-option `take_value` semantics.

### 4. `nexigen-hub` `main` — options struct + setup helpers

- Add `struct Hub_options { run_dirs, listen, ring_records, ring_bytes,
  max_line_bytes }`.
- `parse_hub_args(argc, argv, options) -> int` (0 = ok, 2 = usage).
- `classify_listen_host(listen) -> std::string` (the existing bracket/colon
  logic) and `warn_if_non_loopback(host)`.
- `run_hub(options) -> int` for signal install, discovery, streaming, server
  start, and run.
- `main` becomes `parse → run_hub`.

Usage strings, warning text, and exit codes are unchanged.

### 5. Oversized tests — split into focused sub-functions

Tracing `test_provider` (CCN 50, ~550 lines) becomes a thin runner calling
file-local `static void` sub-functions, each owning one thematic block:

- `provider_lifecycle`, `provider_pool_exhaustion`, `provider_serialization`,
  `provider_idle_wakeups`, `provider_affinity`, `provider_pin_failure`,
  `provider_stress_fifo`, `provider_thread_pool`, `provider_drop_counters`,
  `provider_rate_budget`, `provider_flush`, `provider_shutdown`.

Metrics `test_exponential_histogram_mt` (CCN 37) becomes:

- `exp_hist_containment`, `exp_hist_empty`, `exp_hist_log_uniform`,
  `exp_hist_sharded`, `exp_hist_windowed`, `exp_hist_fallback`.

The parent functions call them in the original order, so assertions, printed
lines, and coverage are unchanged. Sub-functions are `static` in an anonymous
namespace to avoid external symbols.

## Risks / Trade-offs

- [Helper boundaries change stack frames and could shift timing-sensitive
  assertions] → Tests use generous thresholds; production helpers are cold.
  Run the full matrix and compare printed idle-wakeup numbers.
- [CLI byte-identity regressions] → Keep all message strings and exit-code
  paths unchanged; the existing process-supervision CLI assertions act as the
  contract.
- [`open_segment` buffer reuse could be reordered] → Keep `scratch.file`
  reused only in the same phases (base path, then final path) and preserve the
  `NUL` termination of every `put`.
- [Lizard may still report the file-local helpers if one grows] → Keep each
  helper single-purpose and verify with lizard before/after.

## Migration Plan

No data or API migration. Land as one commit; rollback is a revert of the
touched files. No configuration changes.

## Open Questions

None.
