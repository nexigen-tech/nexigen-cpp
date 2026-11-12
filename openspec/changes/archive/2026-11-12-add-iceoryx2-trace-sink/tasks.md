# Tasks

## 1. Wire type

- [x] 1.1 Add `include/nexigen/tracing/detail/fnv1a_64.hpp` with the pinned FNV-1a 64 function (offset basis `0xcbf29ce484222325`, prime `0x100000001b3`, no trailing NUL); verify unit tests pin published check vectors and prove two independent evaluations of the same text agree
- [x] 1.2 Add `include/nexigen/tracing/span_frame.hpp` with the 384-byte `SpanFrame` (span fields, source hashes, `app_id`, `pid`, `sequence`, `publish_ts_ns`, `version`, reserved tail), `static_assert` layout pins for size/alignment/every field offset, `com::concepts::Message` satisfaction, and the `to_string`/`formatter`/JSON trio; verify the pins compile and the JSON round-trip test passes
- [x] 1.3 Add `src/tracing/span_frame_format.cpp` for the cold-path JSON body; verify the tracing test TU links without pulling the body into consumer translation units

## 2. Sink configuration

- [x] 2.1 Add `include/nexigen/tracing/iceoryx2_trace_sink_config.hpp` with `service_name`, `app_id`, `subscriber_max_buffer_depth`, startup validation, and the `to_string`/`formatter`/JSON trio (body in `src/tracing/`); verify validation rejects an empty/oversized service name and an empty/oversized application code, a valid config round-trips through JSON, and formatter output equals `to_string()`

## 3. Sink implementation

- [x] 3.1 Add `include/nexigen/tracing/iceoryx2_trace_sink.hpp` implementing `concepts::TraceSink` (`start`/`on_span`/`flush`/`stop`): map the record to a `SpanFrame` with source hashes, fill `app_id`, cached `pid`, monotonic `sequence`, `publish_ts_ns` (`Timestamp::now()` with 0 fallback) and `version`, loan, write the frame in place, send; verify the sink↔subscriber round-trip test recovers every field
- [x] 3.2 Add the worker-confined 64-entry memo cache (`pointer → hash`, 1 KB) with hit/miss counters and null handling; verify tests that repeated locations reuse the hash (hit counter grows), alternating more locations than the cache still yields correct hashes, and a null location produces hash 0 without caching
- [x] 3.3 Enforce the overflow contract: `DiscardData`, count dropped frames, return success for a dropped frame, and request the configured subscriber buffer depth at service creation; verify that a saturated queue increases the dropped counter without blocking and that a deeper buffer absorbs a burst
- [x] 3.4 Verify the drain contract: sequences from one publisher are consecutive, a forced drop leaves an observable gap, no event service is created, and `flush()` succeeds after sent frames; verify with the sequence-gap test and an assertion that the sink exposes no notifier
- [x] 3.5 Verify allocation freedom and the worker-only contract: run the armed-allocation-counter test across `on_span` after `start()`, and assert the sink state (including the hash cache and counters) is only mutated by the calling provider worker thread as documented; verify the test passes under TSan
- [x] 3.6 Add sink diagnostics: `to_string`, `std::formatter`, and `to_json` covering service, application code, published, dropped, and hash hit/miss counters, with deliberately no `from_json`; verify the formatter matches `to_string`, the JSON names every counter, and the absence of `from_json` is compile-time pinned
- [x] 3.7 Add the `on_span` latency harness (rdtsc calibration, WARMUP, N = 10 000, median/p99/p99.9/max, producer and worker pinned, best-of-3 strict, overhead subtraction) and the 400 ns median gate compiled only under `NEXIGEN_STRICT_LATENCY_TESTS` in Release/RelWithDebInfo; verify the strict run passes the gate and non-strict runs print numbers without asserting timing

## 4. Build integration

- [x] 4.1 Add the `nexigen_tracing_com` INTERFACE target (links `nexigen_tracing` + `nexigen_com`) and the `nexigen_tracing_com_tests` CTest target inside `if(NEXIGEN_ENABLE_COM)`, mirroring `nexigen_logging_com`/`nexigen_metrics_com`, with the strict-latency compile definition wired like `nexigen_tracing_tests`; verify `cmake` configures and the suite runs with `-DNEXIGEN_ENABLE_COM=ON`
- [x] 4.2 Verify the core stays iceoryx-free: configure with `-DNEXIGEN_ENABLE_COM=OFF` and confirm `nexigen_tracing` and its existing tests build and pass without the transport headers or iceoryx2; record the command in the PR notes

## 5. Documentation

- [x] 5.1 Document the sink in the README `nexigen::tracing` API section: the 384-byte `SpanFrame` contract, inline identity and `version`, the memo-cache and latency budget, the `NEXIGEN_ENABLE_COM` gate, interval draining, and drop/sequence semantics; verify the documented CMake option and target names match the build files

## 6. Integration verification

- [x] 6.1 Run the full matrix with the change in place — Debug, Release, TSan, ASan, and a Clang `-Werror` build — and confirm 100% of tests pass, including the new `nexigen_tracing_com_tests`
- [x] 6.2 Confirm the hot path is untouched: diff `SpanRecord`, `ScopedSpan`, `Tracer`, and `TracerProvider` to show no changed inline work and no new headers included from producer paths; verify the existing 20 ns lifecycle gate still passes in Release
- [x] 6.3 Run `openspec validate add-iceoryx2-trace-sink --strict` and confirm the change validates cleanly
- [x] 6.4 Run `graphify update .` after the code lands and confirm the knowledge graph is refreshed

## Workflow follow-up

- Archive the change after review, then open `add-trace-daemon` (subscriber, interval drain, RAM flight recorder, dump trigger) against the now-stable wire contract.
