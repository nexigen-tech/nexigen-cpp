# proposal: refactor-high-complexity-functions

## Why

Static analysis of the repository found 56 functions with cyclomatic complexity above 15, with several production functions reaching 22–40. Overly complex functions are harder to review, test, and modify safely; they also increase the risk of regressions during the next feature cycle. The highest-priority offenders cluster in thread management, the HTTP server, tracing/metrics JSON deserialization, and process supervision. This change refactors those functions into smaller, single-purpose helpers and state machines while preserving every observable behavior and on-disk contract.

## What Changes

- **core/thread-management**: decompose `apply_thread_spec_self` and `parse_thread_spec_json` into small field parsers and per-attribute applicators (`apply_name`, `apply_affinity`, `apply_scheduler`). No change to `ThreadSpec`, `ThreadApplyReport`, or the JSON/schema contract.
- **hub/http-api**: split `HttpServer::Impl::start` into `bind_listen` + `register_epoll` + initial sync; extract `read_client` body framing into `read_until_headers` and `handle_one_request`; simplify `run` by replacing the nested event dispatch with an explicit `ServerState` stepper.
- **tracing/record-format**: replace the monolithic `from_json` for `SpanRecord` and `TracerProviderConfig` with per-substructure helpers (`from_json(..., SpanRecordHeader&)`, `from_json(..., SpanRecordAttributes&)`, `from_json(..., SpanRecordLocation&)`). Use `nlohmann::json::get_to` / `value` where safe under `JSON_NOEXCEPTION=1`.
- **metrics/snapshot-format**: split `MetricRecord` and `MetricBatchSnapshot` `from_json` into helpers per payload kind (`ScalarValue`, `StatisticsSnapshot`, `HistogramSnapshot`, `StateValue`) and a record-header reader. Keep the union-discipline logic explicit and exception-free.
- **runner/process-supervision**: extract a `ProcessState` value type and helpers (`send_signal_and_update`, `wait_child_with_deadline`, `reap_child_status`) from `supervise_child`, `Runner::start`, and `Runner::stop`. No change to the instance directory layout, the exec handshake, or the `exit.json` attribution rules.
- **runner/app-suite**: introduce an explicit `AppStateMachine` helper around `RuntimeApp` transitions, factor `stop_suite` into stop-request, grace-wait, finalize, and fallback-write steps, and simplify `suite_status` by extracting metadata/state loading and per-app status merging.

## Capabilities

- **Modified Capabilities**
  - `core/thread-management` — internal organization only; all requirements remain.
  - `hub/http-api` — internal server structure only; endpoint behavior unchanged.
  - `runner/process-supervision` — internal lifecycle helpers; file contracts and signal rules unchanged.
  - `runner/app-suite` — internal state-machine helpers; suite lifecycle and JSON outputs unchanged.

- **New Capabilities**
  - `tracing/record-format` — covers the cold-path JSON (de)serialization bodies in `src/tracing/record_format.cpp` and the non-throwing, all-or-nothing deserialization contract.
  - `metrics/snapshot-format` — covers the cold-path JSON (de)serialization bodies in `src/metrics/snapshot_format.cpp` and the union-disciplined snapshot contract.

## Success Criteria

- All existing tests pass in Debug, Release, ASan, TSan, and Clang configurations.
- Line coverage for `runner`, `hub`, `tracing`, and `metrics` does not drop below current module gates.
- Post-refactoring lizard complexity for every touched function is ≤ 15; the aggregate average complexity does not increase.
- No public header gains or loses functions; all public signatures remain unchanged.
