# Tasks

Precondition for this whole change: the companion mechanical change moving `logging::LogLevel` to `nexigen::core` is landed first (tasks below build on `core::LogLevel`).

## 1. Vocabulary base (core + chrono mechanical)

- [x] 1.1 Append `ResourceExhausted` (value 8) to `core::ErrorCode` and `EnumTraits` in `include/nexigen/core/result.hpp`; update the name-pin static_assert row count in `tests/core/result_test.cpp`. Verify: the enum-traits unit test names `ResourceExhausted` and round-trips it (`to_string`/`from_string`/`to_json`/`from_json`/`std::format`).
- [x] 1.2 Apply the rename sweep from design D6: new headers `provider.hpp`, `worker.hpp`, `provider_config.hpp`, `provider_kind.hpp`, renamed sink headers (`null_sink.hpp`, `console_sink.hpp`, `json_file_sink.hpp`, `composite_sink.hpp`), `concepts/` renames; update `CMakeLists.txt` source lists and every include site (app, examples, tests). Verify: full configure + ninja build compiles with only renamed (no duplicated) entities.
- [x] 1.3 Delete `clock_log_level.hpp` and switch `detail::LogBinding`, the (future) worker log hook, and `detail::ChronoLoggerBridge` in `app/infrastructure.hpp` onto `core::LogLevel`; port the affected chrono test cases. Verify: `tests/chrono/chrono_test.cpp` compiles and the worker-logger cases pass linked against `core::LogLevel`.

## 2. Concepts and configuration

- [x] 2.1 Add `ProviderKind` (`daemon`, `manual`) in `provider_kind.hpp` with the full conversion set (constexpr `to_string`/`from_string` via `core::EnumTraits`, ADL `to_json`/`from_json` under `JSON_NOEXCEPTION=1`, `std::formatter`). Verify: unit cases covering canonical names, garbage-cast rendering `Unknown`, non-throwing JSON rejection, formatter equality.
- [x] 2.2 Split `concepts/stamper_concept.hpp` (`sample() noexcept -> Timestamp`) and `concepts/provider_concept.hpp` (nested `Stamper`, `acquire()`, `utc_offset()`, `utc_now_ns()`, `snapshot_now()`, `flush()`, `to_string()` per design D1). Verify: compile-time static_asserts in tests for the real type + a deterministic fake provider.
- [x] 2.3 Extend `ProviderConfig` with `kind` and port its serialization set; extend `settings.hpp`/`settings.cpp` merging (file-only kind, unknown value keeps previous + diagnostic, manual+worker-attributes ignored-field diagnostic) and `apply_to` transferring kind. Verify: updated settings cases in `tests/chrono/chrono_test.cpp` (default daemon, local-over-global, invalid kind, env override untouched, config JSON round-trip incl. kind, `to_string`/formatter equality).

## 3. Thin Provider (authority + issuer)

- [x] 3.1 Rebuild the provider class per design D2: fixed `alignas(64)` bank template `<std::size_t MaxSources = 64>`, `next_free_`/`prepared_` atomics with the documented memory orders, cold `snapshot_mutex_` only around snapshot delivery, `prepare()`/`acquire()`, `utc_offset()`/`utc_now_ns()`, `snapshot_now()`/`flush()`, flush-only destructor; move non-inline bodies to `src/chrono/provider.cpp` and delete the worker machinery from the provider. Verify: `chrono_test` provider section covers construction-delivers-nothing, immediate usability of `snapshot_now()`/`utc_offset()`, and the flush-only destructor.
- [x] 3.2 Issuance tests: `acquire()` before `prepare()` reports `NotInitialized`; after `prepare()` each acquire yields a distinct source with its own non-decreasing series; full bank + one more reports `ResourceExhausted`; issuing under an armed allocation arena allocates nothing. Verify: dedicated cases pass in Debug.
- [x] 3.3 Snapshot correctness: failures from sampling or the sink propagate and leave the provider usable; two-thread manual `snapshot_now()` against a recording sink never interleaves partial snapshots. Verify: Debug + the TSan configuration task below report no race.

## 4. ChronoWorker (periodic daemon wrapper)

- [x] 4.1 Carve the loop into `chrono/worker.hpp` + `src/chrono/worker.cpp` per design D3: bind `Provider&`, carry the fixed-rate `wait_until` loop and no-backlog-chase, thread-spec application with `Required`/best-effort semantics and `alignas(64)` startup cells and their memory orders, `start()`/`start(logger)` idempotence/`Conflict` contract, `startup_status()`/`worker_thread_report()`/`worker_mask()`, destructor stop → join → final snapshot via provider → flush. Verify: worker unit cases for every moved scenario pass.
- [x] 4.2 Port the daemon parity matrix from the old provider requirements into worker tests: period spacing within tolerance, zero-interval no-op (no thread, no attributes), manual-during-daemon determinism, best-effort visible without stopping, `Required` failure joins and fails `start()`, CPU 63+ support, empty CPU set, logger startup/shutdown diagnostics and double-bind `Conflict`. Verify: all parity cases pass in Debug.
- [x] 4.3 Worker diagnostics (`to_string()`, free helper, `std::formatter` rendering the same text) and the TSan-clean reading contract for report/mask while running. Verify: rendering cases equal and TSan run below reports no race.

## 5. Downstream compile parity

- [x] 5.1 Update `app/infrastructure.hpp` to construct `chrono::Provider` + `chrono::ChronoWorker` in the daemon shape (worker start via the `core::LogLevel` bridge, zero-interval no-op preserved) and destroy in worker → provider → sink order. Verify: `examples/app/*` and `examples/suite/*` compile and their lifecycle smoke checks still pass.
- [x] 5.2 Rework `examples/chrono/ex_chrono_snapshot.cpp` onto the new names (thin provider + worker) and confirm output still yields the JSON-lines series `{stamp_ns, realtime_offset_ns}`. Verify: the example runs and its output parses.
- [x] 5.3 Sweep remaining references to renamed symbols (docs comments, `suite` examples, metric/tracer infra immunities) and confirm zero hits for `ChronoProvider`, `ChronoSink`, `JsonFileChronoSink`, `ClockLogLevel` outside archive/history contexts. Verify: `grep -r "Chrono\(Provider\|Sink\)" include src examples tests openspec/specs` is empty (archives may reference history).

## 6. Full gate

- [x] 6.1 Debug build: CMake configure + ninja, 100% of unit tests pass (`ctest` over the test set), including the new provider/worker/settings cases.
- [x] 6.2 Release build `-O3 -march=native -flto`: 100% tests pass with latency gates unchanged (no regression in the recorded enqueue/stamp budgets; issued-stamper `sample()` call sites keep the same instruction count).
- [x] 6.3 ThreadSanitizer config (`-DNEXIGEN_ENABLE_TSAN=ON`): worker state reads, logger binding, and the two-thread manual snapshot test are race-free.
- [x] 6.4 AddressSanitizer config (`-DNEXIGEN_ENABLE_ASAN=ON`): no leaks/races in provider/worker teardown ordering (worker → provider → sink).
- [x] 6.5 `openspec validate "add-clock-authority-core" --strict` passes and the change directory carries all deltas (`chrono/provider`, `chrono/worker`, removed `chrono/chrono-provider`, `chrono/chrono-settings`, `core/result`).

## Workflow follow-up

- Archive the change after the project's review requirements are satisfied (the archive step copies `chrono/provider` + `chrono/worker` capabilities and retires `chrono/chrono-provider`).
- Sequence the follow-on changes: phase C (logging migration onto `concepts::Provider`, bank allocation for its producer slots), then metrics/tracing, then Infrastructure `kind` selection; the free `sample_utc_offset()` retirement goes with the last consuming module's migration.
