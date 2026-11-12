# Tasks

## 1. Publisher fast path

- [x] 1.1 Add `Publisher<T, H>::send_copy(const T&) noexcept` in `include/nexigen/com/publisher.hpp` delegating to `iox2::Publisher::send_copy` and mapping errors through `detail::map_error`; verify `tests/com/com_test.cpp` publishes with it and a connected subscriber receives the exact payload bytes
- [x] 1.2 Extract a private `record_delivery(std::uint64_t delivered) noexcept` shared by `send()` and `send_copy()` (published count, peak recipients, queued/lost counters) with the existing relaxed memory orders; verify `to_string()` reports the new published total and the existing `send()` tests keep passing
- [x] 1.3 Add telemetry tests for the new path: a `lost_samples()` snapshot taken before and after one overflowing `send_copy()` grows, and a call on a not-started or slot-starved publisher returns an error without blocking; verify the assertions pass in `nexigen_com_tests`
- [x] 1.4 Document `send_copy` in the `publisher.hpp` header contract note (single transport call, non-blocking discard, telemetry parity); verify the comment matches the implementation and the diagnostics accessors

## 2. Trace sink

- [x] 2.1 Replace the `loan_uninit`/`write_payload`/`send` chain in `Iceoryx2TraceSink::on_span` with `publisher_->send_copy(frame_)`, keeping the `sequence` increment inside `map_into_frame` and the `lost_samples()` snapshot strictly before the call; verify round-trip, overflow/gap, and burst-sequence tests in `nexigen_tracing_com_tests`
- [x] 2.2 Confirm the sink still publishes with zero heap allocations and reports the same published/dropped/hash diagnostics after the switch; verify `test_allocation_free`, the diagnostics test, and the strict `on_span` latency gate (median ≤400 ns) pass, and record the new median next to the previous 210–220 ns
- [x] 2.3 Verify a dropped publication still leaves an observable sequence gap and the subscriber-queue eviction still increments `dropped()`; verify with the existing overflow and gap scenarios

## 3. Logging sink

- [x] 3.1 Replace the publish chain in `logging::detail::Iceoryx2Sink::handle` with `publisher_->send_copy(frame_)`, preserving the "transport out of samples drops silently" behavior and the notifier-once-per-batch rule; verify the logging com round-trip and drop tests pass
- [x] 3.2 Verify the logging hot path stays allocation-free and the existing logging latency gate is unchanged or better; verify with the logging com tests and the strict latency output

## 4. Metrics sink

- [x] 4.1 Replace the publish chain in `Iceoryx2MetricSink::on_batch` with `publisher_->send_copy(frame_)` in the record loop, keeping the `published` counter and the single notification after the batch; verify batch delivery, saturation-drop, and notify-once tests pass
- [x] 4.2 Verify the metrics hot path stays allocation-free and its existing latency gate is unchanged or better; verify with the metrics com tests and the strict latency output

## 5. Benchmark comparison

- [x] 5.1 Add an `(a2) send_copy` branch to `tests/com/latency_test.cpp` next to `(a) direct loan/send`, using the same frame, calibration, warmup, overhead subtraction, and best-of-3 passes; verify the output prints both medians and percentiles
- [x] 5.2 Extend the existing `mallinfo2` allocation window to cover both `(a)` and `(a2)`; verify the reported heap delta is zero
- [x] 5.3 Record the before/after medians for `on_span` and for both transport paths, and confirm whether the measured gain is at least the ~10 ns threshold from the design before keeping the sink changes

## 6. Integration verification

- [x] 6.1 Build and run 100% of the tests under Debug and Release (`-O3 -march=native -flto` with strict latency gates), ThreadSanitizer (`-DNEXIGEN_ENABLE_TSAN=ON`), and AddressSanitizer (`-DNEXIGEN_ENABLE_ASAN=ON`) with `scripts/build.sh build-com build-release-com build-tsan-com build-asan-com` (the script defaults to `-j 64`); verify every suite passes
- [x] 6.2 Build with `-DNEXIGEN_ENABLE_COM=OFF` (Clang `-Werror` tree) and verify the core, tracing, logging, and metrics suites pass with no iceoryx2 references in the new path
- [x] 6.3 Run `nexigen_complexity_gate`, the clang-format check, and `openspec validate --strict`; verify all are clean
- [x] 6.4 Run `graphify update .` after the code changes and confirm the graph rebuild completes

## Workflow follow-up

- Verify the change and archive it after the review requirements are satisfied.
- Push the resulting commits.
