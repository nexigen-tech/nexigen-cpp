# design: refactor-high-complexity-functions

## Context

Static analysis found 56 functions above the project complexity target of 15. The highest-priority production functions are concentrated in five files:

- `include/nexigen/core/thread_management.hpp` — `apply_thread_spec_self` (28), `parse_thread_spec_json` (23)
- `src/hub/http_server.cpp` — `HttpServer::Impl::start` (25), `read_client` (17), `run` (16)
- `src/tracing/record_format.cpp` — `from_json` overloads up to 40 (notably `SpanRecord` and `TracerProviderConfig`)
- `src/metrics/snapshot_format.cpp` — `from_json` for `MetricRecord` (22) and `MetricBatchSnapshot`
- `src/runner/runner.cpp` — `supervise_child` (17), `Runner::start` (large)
- `src/runner/app_suite.cpp` — `start_suite`, `stop_suite`, `suite_status`, `run_suite_supervisor`/`advance_app`

All these functions are cold-path-only and have no public hot-path callers, so refactoring can be done without touching producer/consumer latency contracts.

## Approach

The refactor is strictly internal: no public header signatures change, no on-disk JSON contracts change, and no observable behavior changes. Each target function is decomposed into smaller helpers using the patterns already present in the codebase (small `Status`/`bool` helpers, lambda factories, namespace-local free functions).

### core/thread-management

- Introduce namespace-local helpers in `thread_management_detail`:
  - `apply_name`, `apply_affinity`, `apply_scheduler` — each updates `ThreadApplyReport` and returns nothing.
  - `restore_applied_attributes` — performs rollback in reverse order.
- In `parse_thread_spec_json`, replace inline field parsing with `parse_name`, `parse_cpus`, `parse_mode`, `parse_scheduler` helpers that operate on a staging `ThreadSpec`.
- Keep `apply_thread_spec_self` as the public entry point; it becomes a short orchestrator.

### hub/http-api

- Split `start` into:
  - `parse_listen` (already exists; keep)
  - `bind_listen` — returns the bound `listen_fd` or an error
  - `create_epoll_set` — creates `epoll_fd` and `event_fd`
  - `register_poll_sources` — adds listen fd, eventfd, and inotify to epoll
  - `initial_sync` — calls `streaming.sync(discovery.tree())`
- Split `read_client` into:
  - `drain_input` — loops `recv` until `EAGAIN`/`EINTR`/`peer_closed`
  - `process_requests` — extracts complete `"\r\n\r\n"`-delimited requests and calls `handle_request`
- Replace the deeply nested `run` loop with a `ServerState` stepper or explicit helper sequence:
  - `wait_events`
  - `dispatch_event(event)`
  - `run_timers`
  - `streaming.poll()`
  - `dispatch_live`
  - `drain_client_outputs`

### tracing/record-format

- Keep the existing `LocationIntern` table and hex helpers.
- Add namespace-local helpers:
  - `from_json_header(const nlohmann::json&, SpanRecord&)`
  - `from_json_name(const nlohmann::json&, SpanRecord&)`
  - `from_json_location(const nlohmann::json&, SpanRecord&)`
  - `from_json_attributes(const nlohmann::json&, SpanRecord&)`
  - `from_json_worker_thread` and numeric readers for `TracerProviderConfig`
- Replace manual `find`/`get_ptr` chains with small `read_*` lambdas where the type is unambiguous, while preserving the all-or-nothing and non-throwing semantics.

### metrics/snapshot-format

- Add namespace-local helpers:
  - `read_name_field`, `read_number`, `read_unsigned`, `read_bool`
  - `from_json_payload` that switches on `MetricKind` and dispatches to scalar/statistics/state/histogram helpers
  - `from_json_record_header` for identity and enums
  - `from_json_records_array` for `MetricBatchSnapshot`
- Keep the union discipline explicit: only the payload member selected by `kind` is deserialized.

### runner/process-supervision

- Introduce a small `ProcessState` value type or status enum only if it simplifies `supervise_child`; otherwise use plain helpers.
- Helpers:
  - `wait_child_blocking(pid, status)` — reaps with `waitpid(..., 0)`.
  - `wait_child_deadline(pid, status, deadline_ns, result)` — polls, escalates `SIGTERM` → grace → `SIGKILL`.
  - `send_process_signal(pid, signal, result)` — wraps `kill(-pid, signal)`.
- In `Runner::start`, group existing steps into:
  - `validate_inputs`
  - `prepare_layout`
  - `open_log_fds`
  - `build_argv`
  - `fork_supervisor`
  - `read_start_report`
- In `Runner::stop`, reuse the existing small helpers (`resolve_stop_state`, `send_instance_signal`, `wait_for_exit_or_settle`, `finalize_stop`) and keep them unchanged.

### runner/app-suite

- Add an `AppStateMachine` helper struct that holds a reference to a `RuntimeApp` and provides:
  - `on_exit_consumed(RuntimeSuite&, const ExitMetadata&)`
  - `on_restart_timer(RuntimeSuite&, std::uint64_t now)`
  - `on_launch_failed(RuntimeSuite&)`
- Decompose `start_suite` into:
  - `preflight_thread_specs`
  - `check_suite_conflict`
  - `create_suite_dir`
  - `fork_suite_supervisor`
- Decompose `stop_suite` into:
  - `write_stop_request`
  - `compute_suite_grace`
  - `wait_for_suite_exit`
  - `finalize_suite_stop`
- Decompose `suite_status` into:
  - `load_suite_metadata`
  - `derive_suite_state`
  - `merge_app_status`

## Key Decisions

1. **No public API changes.** All helpers are file-local or in an anonymous namespace. Headers stay unchanged so downstream consumers do not rebuild.
2. **No behavior changes.** Every refactor is a pure decomposition; control flow, error codes, signal sequences, file writes, and JSON outputs remain identical.
3. **No hot-path touches.** All modified files are cold-path-only; latency gates and hot headers are unaffected.
4. **Lizard ≤ 15 target.** Each extracted helper is designed to stay below the complexity threshold.
5. **Tests are the oracle.** Existing tests must pass without modification; new tests are added only if a new helper is complex enough to warrant direct unit testing.

## Open Questions

- None. The scope is limited to decomposition and the success criteria are clear.

## Risks and Mitigations

| Risk | Mitigation |
|------|------------|
| Introducing subtle behavior drift during decomposition | Run the full test matrix and compare lizard output; keep commits small and reviewable |
| New helpers increasing header compile time | All helpers are in `.cpp` files or namespace-local in headers; no new inline templates |
| Coverage gates dropping because code moves | Verify coverage for runner, hub, tracing, and metrics after the refactor; add targeted tests if needed |
| State-machine refactor in app_suite changing restart timing | Keep the poll loop structure and timing constants identical; only split state transitions into functions |
