# tasks: refactor-high-complexity-functions

## Analysis / preparation

- [x] Re-run `lizard` on the six target files and record baseline complexities.
- [x] Confirm all existing tests pass on the baseline commit (Debug, Release, ASan, TSan, Clang).
  - Debug, ASan, TSan, Clang: 100% passed.
  - Release (`build/release`) fails pre-existing latency gates (logging 20 ns, metrics 15 ns, diagnostics 20 ns) on this shared runner; portable Release (`build/portable` with `NEXIGEN_STRICT_LATENCY_TESTS=OFF`) passes 100%.
- [x] Record baseline line-coverage percentages for runner, hub, tracing, and metrics.
  - runner 93.0%, hub 95.2%, tracing 96.4%, metrics 96.3%.

## core/thread-management

- [x] Extract field parsers (`parse_name`, `parse_cpus`, `parse_mode`, `parse_scheduler`) from `parse_thread_spec_json`.
- [x] Extract attribute applicators (`apply_name`, `apply_affinity`, `apply_scheduler`) and rollback helper from `apply_thread_spec_self`.
- [x] Verify `include/nexigen/core/thread_management.hpp` compiles and thread-management tests pass.
- [x] Verify no function in the file exceeds complexity 15.

## hub/http-api

- [x] Split `HttpServer::Impl::start` into `bind_listen`, `create_epoll_set`, `register_poll_sources`, and `initial_sync`.
- [x] Layer `read_client` into `drain_input` and `process_requests`.
- [x] Restructure `run` into explicit step helpers or a `ServerState` stepper.
- [x] Run hub tests and the HTTP fuzzer.
- [x] Verify no function in `src/hub/http_server.cpp` exceeds complexity 15.

## tracing/record-format

- [x] Add substructure helpers for `SpanRecord` (`from_json_header`, `from_json_name`, `from_json_location`, `from_json_attributes`).
- [x] Add numeric/type helpers for `TracerProviderConfig` and simplify `DroppedStats` parsing.
- [x] Replace verbose `find`/`get_ptr` chains with guarded `get_to`/`value` where safe.
- [x] Run tracing tests and round-trip serialization tests.
- [x] Verify no function in `src/tracing/record_format.cpp` exceeds complexity 15.

## metrics/snapshot-format

- [x] Add generic numeric readers (`read_number`, `read_unsigned`, `read_bool`) for snapshot fields.
- [x] Add `from_json_payload` dispatcher and per-kind payload helpers.
- [x] Add `from_json_record_header` and `from_json_records_array` helpers.
- [x] Simplify `MetricProviderConfig::from_json` with type-checked numeric readers.
- [x] Run metrics tests and round-trip serialization tests.
- [x] Verify no function in `src/metrics/snapshot_format.cpp` exceeds complexity 15.

## runner/process-supervision

- [x] Extract `wait_child_blocking`, `wait_child_deadline`, and `send_process_signal` helpers from `supervise_child`.
- [x] Split `Runner::start` into validation, layout, log-fd, argv, fork, and report-read phases.
- [x] Keep `Runner::stop` helpers unchanged; verify they compose correctly after `supervise_child` refactor.
- [x] Run runner process-supervision tests.
- [x] Verify no function in `src/runner/runner.cpp` exceeds complexity 15.

## runner/app-suite

- [x] Introduce `AppStateMachine` helper and move `advance_app` transitions into explicit methods (`advance_suite_apps`, `supervisor_tick`).
- [x] Split `start_suite` into preflight, conflict check, directory creation, and supervisor fork helpers.
- [x] Split `stop_suite` into stop-request, grace computation, wait, and finalize helpers.
- [x] Split `suite_status` into metadata/state loading and per-app merge helpers.
- [x] Run app-suite tests.
- [x] Verify no function in `src/runner/app_suite.cpp` exceeds complexity 15.

## Verification and gating

- [x] Run `lizard` on all six target files and confirm no touched function exceeds complexity 15.
  - Note: `CpuSet` class in `thread_management.hpp` reports a pre-existing lizard class-complexity warning (CCN 21); no individual function introduced or modified by this change exceeds 15.
- [x] Run the full test matrix: Debug, Release (portable), ASan, TSan, Clang.
  - Debug: 36/36 passed.
  - Portable Release: 36/36 passed.
  - ASan: 36/36 passed.
  - TSan: 36/36 passed.
  - Clang: 36/36 passed.
- [x] Run the coverage target and confirm runner ≥ 90%, hub ≥ 90%, tracing and metrics do not drop.
  - runner 93.0%, hub 95.2%, tracing 96.5% (baseline 96.4%), metrics 96.6% (baseline 96.3%).
- [x] Run `clang-format` check and fix any formatting issues.
- [x] Review that no public header signature changed.
