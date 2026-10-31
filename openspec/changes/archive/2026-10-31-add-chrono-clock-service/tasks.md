# Tasks

## 1. Clock_id and domain declaration

- [x] 1.1 Implement `nexigen::chrono::Clock_id` (enum: `monotonic_raw`, `boottime`, `realtime`) with `to_string`/`from_string` in `include/nexigen/chrono/clock_id.hpp`; verify a unit test round-trips all three values and rejects an unknown string
- [x] 1.2 Add `to_json`/`from_json` (under `JSON_NOEXCEPTION=1`) and a `std::formatter` specialization for `Clock_id`; verify JSON round-trip and `std::format("{}", Clock_id::monotonic_raw) == to_string(...)` tests pass
- [x] 1.3 Add the compile-time domain declaration (a `static constexpr Clock_id domain = Clock_id::monotonic_raw`-style trait or concept) with Doxygen comments; verify `constexpr` evaluation and a no-allocation/no-lock query under an armed allocation arena

## 2. Chrono_snapshot value type

- [x] 2.1 Implement `nexigen::chrono::Chrono_snapshot` (`chrono::Timestamp stamp` + `std::int64_t realtime_offset_ns`) with `static_assert` for trivial copyability and 16-byte size in `include/nexigen/chrono/chrono_snapshot.hpp`; verify size/trivial-copy asserts and field round-trip tests
- [x] 2.2 Add `to_string` (member + free), `to_json`/`from_json`, and `std::formatter` for `Chrono_snapshot`; verify JSON round-trip, negative-offset preservation, and formatter-equals-to_string tests

## 3. ChronoSink concept and sinks

- [x] 3.1 Implement `nexigen::chrono::concepts::ChronoSink` (`on_snapshot`/`flush`) in `include/nexigen/chrono/concepts/chrono_sink_concept.hpp`; verify the concept is satisfied by all four sinks and rejected by a type with a wrong or missing member
- [x] 3.2 Implement `NullChronoSink` with diagnostics; verify no-op and diagnostics-contract tests (to_string/JSON/formatter)
- [x] 3.3 Implement `ConsoleChronoSink` writing to a configured descriptor; verify render/write to a pipe, unrecoverable-write-error reporting, and diagnostics tests
- [x] 3.4 Implement `JsonFileChronoSink` and `JsonFileChronoSink_config` (path + append) with JSON-lines output and raw `::write`; verify one-line-per-snapshot, append order, open/write failure surfacing, flush persistence, and config JSON round-trip tests
- [x] 3.5 Implement `CompositeChronoSink<Sinks...>`; verify fan-out, first-failure aggregation, typed/indexed access, and diagnostics-contract tests

## 4. ChronoProvider

- [x] 4.1 Implement `nexigen::chrono::detail::affinity` (`pin_self`/`self_affinity`); verify pin/read-back tests on a dedicated thread and SystemError on an invalid mask
- [x] 4.2 Implement `ChronoProvider_config` (`collect_interval`, `core_mask`) with serialization and `ChronoProvider` construction that binds a sink and pre-allocates; verify config JSON round-trip, construction success, and allocation-free steady-state sampling
- [x] 4.3 Implement the background snapshot worker (self-pin first act, `collect_interval` loop, zero interval disables, `startup_status()` reports invalid mask); verify period spacing, affinity read-back, zero-interval, and invalid-mask-visible tests
- [x] 4.4 Implement the control plane (`snapshot_now()`, `flush()`, `startup_status()`) and shutdown drain; verify manual-snapshot determinism, flush-failure propagation, and clean shutdown under sanitizers

## 5. Chrono_settings loader

- [x] 5.1 Implement `Chrono_settings` and `load_chrono_settings` (explicit global/local paths, precedence defaults → global → local → environment); verify precedence, empty-path, and missing-source tests
- [x] 5.2 Implement best-effort diagnostics and the apply-to-`ChronoProvider_config` helper with `NEXIGEN_CHRONO_COLLECT_INTERVAL_NS` / `NEXIGEN_CHRONO_CORE_MASK` overrides; verify malformed/invalid-value tests and apply-mapping tests

## 6. Integration and verification

- [x] 6.1 Add a runnable `ex_chrono_snapshot` example that builds a `JsonFileChronoSink`, runs a `ChronoProvider`, emits snapshots, flushes, and exits 0; verify it produces a parseable snapshot file
- [x] 6.2 Build and pass 100% of unit tests under Debug; verify `ctest` under Debug passes
- [x] 6.3 Build and pass 100% of unit tests under Release (`-O3 -march=native -flto`) with latency gates; verify the Release build and latency gates pass
- [x] 6.4 Build and pass 100% of unit tests under ThreadSanitizer (`-DNEXIGEN_ENABLE_TSAN=ON`) and AddressSanitizer (`-DNEXIGEN_ENABLE_ASAN=ON`); verify both sanitizer builds report no races or leaks

## Workflow follow-up

- Archive the change after the project's review requirements are satisfied.
- Verify the archived result (specs synced into `openspec/specs/chrono/`).
