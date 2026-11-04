# Tasks

## 1. `core::sink_pack` — fan-out scaffold (additive)

- [x] 1.1 Create `include/nexigen/core/sink_pack.hpp` (`namespace nexigen::core::sink_pack`): template class `Fanout<Sinks...>` with members copied **verbatim** from `composite_metric_sink.hpp` — `std::tuple<Sinks...> members_`, `static constexpr std::size_t size`, `find_index<T>()`, `exactly_one<T>()` (reuse the exact static_assert text), index/type `get`, `for_each` (`std::apply` comma-fold), `requires std::is_default_constructible_v<std::tuple<Sinks...>>` default ctor, member-injection move ctor. Add free `combine(core::Status, core::Status)` (first failure wins). Header comment references this change and the `settings_io`/`fixed_writer` placement precedent (named namespace, not `core::detail`).
- [x] 1.2 Extend `tests/core/core_test.cpp` with a `sink_pack` section: index/type access identity, `size`, pack-order iteration probe, `combine` first-failure matrix, empty-pack behavior, duplicate-type compile-time probe (`static_assert(!... requires-expressions)` pattern), member-injection with a non-default-constructible member (stub class), reference-identity probe `&get<0>() == &get<FirstType>()`.
- [x] 1.3 Compile `nexigen_core_tests` + run the core suite (Debug); add the new section name to the suite listing if the CMake test name filter needs it.

## 2. `core::sink_io` — fd plumbing (additive)

- [x] 2.1 Create `include/nexigen/core/sink_io.hpp` (`namespace nexigen::core::sink_io`): `open_output(std::string_view path, bool append) noexcept -> int` (O_WRONLY | O_CREAT | O_CLOEXEC, `append ? O_APPEND : O_TRUNC`, mode 0644; one `std::string` copy inside), `write_all(int fd, const char*, std::size_t) noexcept -> core::Status` (loop copied verbatim from `json_file_trace_sink.hpp`, `SystemError` on failure), `fsync_fd(int) noexcept -> core::Status`. Header comment mirrors `sink_io` cold-path scope and the EINTR/partial-write contract.
- [x] 2.2 Extend `tests/core/core_test.cpp` with a `sink_io` section: partial-write completion through a non-blocking pipe (write larger than pipe buffer while a reader drains), in-order byte integrity, failure mapping via a pipe whose read end was closed (expected `SystemError`), `open_output` flag parity (F_GETFD close-on-exec bit, stat mode 0644 default umask, append vs truncate against pre-existing `hello\n`), `fsync_fd` success/failure mapping. EINTR trigger via timer-signal probe (`sigaction` without `SA_RESTART` firing during a blocking pipe write) only if stable locally in this step's session — otherwise leave the documented fallback (design Risks) with a task 8.x re-check.
- [x] 2.3 Compile + run core suite (Debug).

## 3. Composite fan-out migration (chrono / metrics / tracing)

- [x] 3.1 `include/nexigen/metrics/composite_metric_sink.hpp`: inherit `sink_pack::Fanout<MetricSinks...>`; delete the local `find_index`/`exactly_one`/`get`/`for_each`/`size`/`members_`/`combine` copies; keep `on_batch` (fold via `sink_pack::combine`), `to_json` (keep `CompositeMetricSink<Sinks...>::size` expression — resolves through the base), per-module `static_assert(concepts::MetricSink<CompositeMetricSink<>>)` and constructor delegating to the base.
- [x] 3.2 Same migration for `include/nexigen/tracing/composite_trace_sink.hpp` (payload `on_batch(SpanRecord const&)` — keep existing per-module surfaces).
- [x] 3.3 Same migration for `include/nexigen/chrono/composite_chrono_sink.hpp` (payload `on_snapshot`).
- [x] 3.4 Compile all three module test targets and run `ctest` suites for metrics, tracing, chrono (Debug). Existing composite assertions (`metrics_test.cpp:1242–1243` reference identity, `tracing_test.cpp:2042` `to_json`, logging examples docs untouched) must pass unmodified.

## 4. File-sink migration (metrics / tracing ×2 / chrono)

- [x] 4.1 `json_file_metric_sink.hpp`: ctor opens via `sink_io::open_output(config.path, config.append)`; private `write_all` deleted, body `sink_io::write_all(fd_, scratch_, bytes)` + existing bool→`SystemError` mapping at both call sites; `flush()` via `sink_io::fsync_fd`. Public shape untouched.
- [x] 4.2 `json_file_chrono_sink.hpp`: identical swap (no-arg `write_all()` → `sink_io::write_all(fd_, scratch_, len_)`; `flush()` → `fsync_fd`).
- [x] 4.3 `json_file_trace_sink.hpp`: `open_for_window` → `sink_io::open_output(name, true)` (drops the `NOLINT(bugprone-suspicious-stringview-data-usage)` guard); private `write_all(std::size_t) noexcept -> core::Status` becomes a one-line delegate to `sink_io::write_all`. Rotation semantics untouched.
- [x] 4.4 `chrome_trace_sink.hpp`: `start()` open → `sink_io::open_output(config_.path, false)`; `write_bytes` body → bool wrapper over `sink_io::write_all` (map documented in design D3).
- [x] 4.5 Compile tracing/metrics/chrono suites and run them (Debug).

## 5. Settings vocabulary consolidation

- [x] 5.1 Extend `include/nexigen/core/settings_io.hpp` with `read_uint`, `sink_field`, `find_sink`, `read_sink_string`, `read_sink_bool`, `read_sink_uint` — signatures and diagnostics copied verbatim (`expected an integer in range` / `sink.<key>` / `expected a JSON object` / `expected a string` / `expected a boolean` / `expected a non-negative integer`); include `<nlohmann/json.hpp>` guard pragma already established by loaders. Header note lists the change id and mirrors the "one implementation" precedent phrasing.
- [x] 5.2 `src/chrono/chrono_settings.cpp`: delete local `FileRead`, `read_settings_file`, `add_diagnostic`, `unsigned_value`, `parse_u64`, `read_uint`, `sink_field`, `find_sink`, `read_sink_string`, `read_sink_bool`; add the using-declaration block (same as `metrics_settings.cpp:41–45` plus the new helpers); keep env-core-mask and merge-layer logic untouched.
- [x] 5.3 `src/metrics/metrics_settings.cpp` and `src/tracing/tracing_settings.cpp`: delete local `read_uint`/`sink_field`/`find_sink`/`read_sink_*` definitions; switch using-declarations to the shared ones. Diagnostic strings regression: chrono/metrics/tracing settings suites must pass unchanged (they pin exact texts).
- [x] 5.4 Compile + run settings suites (`chrono_test`, `metrics_test`, `tracing_test`, plus `app_test` which exercises loader diagnostics through `nexigen::app`).

## 6. `nexigen-run` CLI report helpers

- [x] 6.1 Add file-local helpers in `tools/nexigen-run/main.cpp` anonymous namespace: `cli_fail(command, mode, target, diagnostic, stderr_message)` emitting the exact current 3-line tail and returning `exit_failure`; `cli_ok(command, mode, target, verb)` for the `started:`/`stopped:`/target-line success tails; `suite_target_options_conflict(...)` for the duplicated mutual-exclusivity block in `cmd_stop`/`cmd_status`.
- [x] 6.2 Rewire call sites: all failure tails in `cmd_start`/`cmd_stop`/`cmd_status` and the 4 simple success tails go through helpers; two paths stay bespoke by byte-identity necessity (the `suite metadata unreadable` tail prints the target as the stderr detail; the `cmd_status` suite-ok tail prints a listing tail) — documented in the session summary. Byte identity: stderr text, `runner.jsonl` events, exit codes.
- [x] 6.3 Build `nexigen-run` and run runner/hub CLI suites (`runner_test`, `hub_test`, process-supervision CLI assertions) — outputs byte-identical for the touched binary. NOTE (pre-existing, unrelated): `nexigen_hub_cli_tests` fails on the `[::1] must not warn` loopback check — reproduced standalone with untouched hub sources (`nexigen-hub --listen "[::1]"` warns non-loopback itself); outside this change's scope.

## 7. Verification matrix (project rules)

- [x] 7.1 Debug: build `build` and run `ctest --test-dir build --output-on-failure` — 100% pass.
- [x] 7.2 Release: configure/build `build-release` (`-O3 -march=native -flto`); full `ctest --test-dir build-release` pass (35/35). Latency gates: the ns-class gates (`NEXIGEN_STRICT_LATENCY_TESTS=ON`, 20 ns budget) are ENVIRONMENT-bounded here — reproduced deterministically at ~130 ns median in the shared container, consistent with the option's own "shared CI runners and containers cannot guarantee the 20 ns budget" comment and with the pre-existing user configuration; gates restored OFF per the documented environment setting (see session notes).
- [x] 7.3 ThreadSanitizer: configure/build/run `build-tsan` (`-DNEXIGEN_ENABLE_TSAN=ON`) full pass (35/35).
- [x] 7.4 AddressSanitizer: configure/build/run `build-asan` (`-DNEXIGEN_ENABLE_ASAN=ON`) full pass (35/35).
- [x] 7.5 clang-tidy check target with the project profile (`.clang-tidy` bugprone/performance/portability set) passes on all touched headers and TUs; verify no new "second bare `detail::`" ambiguity in TUs that `using namespace nexigen::core` (compile examples, `examples/core`, `ex_metric_provider_and_sinks`, `ex_nested_and_cross_thread_spans`, `ex_stopwatch_histogram` — the composite consumers).
- [x] 7.6 Coverage: run the `coverage` target (`-DNEXIGEN_ENABLE_COVERAGE=ON`) and confirm the touched-file coverage does not drop below the current 95.2% line gate baseline.

## 8. Wrap-up

- [x] 8.1 `grep` audit: no private write-loop/`FileRead`/`add_diagnostic` copies remain in the four sinks and three loaders; `sink_pack`/`sink_io` are the only implementations; add one-line notes to `README.md` architecture list for the two new core headers (section listing core building blocks).
- [x] 8.2 Re-check the EINTR probe decision from task 2.2 in a fresh environment; finalize the loop contract comment in `sink_io.hpp` (retry-on-interrupt without data loss) — fallback path per design Risks if the probe was not testable.
- [x] 8.3 Record deferred follow-ups in `TODO.md` ( CompositeLogger migration; cross-reference the existing items for HttpServer::Impl split / supervisor decomposition / affinity unification) so nothing fades after archive.

## 9. Post-verification improvements (apply-session review, task 4)

- [x] 9.1 Negative-compile probe automated in CI: `tests/core/neg/sink_pack_negative.cpp` + `tests/core/negative_compile.sh`, registered as `nexigen_sink_pack_negative_compile` CTest entry (run the compiler, require failure with `static assertion failed`); `tests/core/core_test.cpp` recipe comment replaced by the pointer to the automated test.
- [x] 9.2 Literal diagnostic-text pins: `tests/chrono/chrono_test.cpp` (`expected an integer in range` + field/source, sink `expected a string`/`expected a boolean`/`expected a JSON object`); `tests/app/app_test.cpp` new sink-vocabulary pins through the metrics/tracing loaders (`expected a string`, `expected a boolean`, `expected a JSON object`, `expected a non-negative integer` with mirrored fields).
- [x] 9.3 Spec wording (S3.1) aligned with the actual probe technique: partial-write scenario now describes the blocking pipe + throttled reader + interval timer (real EINTR), not "non-blocking pipe".
- [x] 9.4 design.md D5 reworded: the plain-message `cli_fail_plain` overload was intentionally not built — both no-`Status` paths stay bespoke by byte-identity (recorded on task 6.2).
