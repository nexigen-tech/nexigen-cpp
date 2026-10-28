# Proposal

## Why

`nexigen` measures aggregates (`metrics`) and logs events (`logging`), but nothing attributes causality: which span produced which child, on which thread, taking how long, with what attributes. Real-time HIL platforms need distributed-timeline visibility that never perturbs the simulation loop — span begin/end must cost tens of nanoseconds, never allocate, never lock. Building tracing now reuses the proven per-thread `SPSC` sharding, `chrono` stamping and serialization contracts, and closes the last observability pillar before downstream consumers adopt their own ad-hoc tracing.

## What Changes

- Introduce the `nexigen::tracing` enum catalogue — `SpanKind` (`Internal`, `Server`, `Client`, `Producer`, `Consumer`), `SpanStatus` (`Unset`, `Ok`, `Error`), `SamplingDecision` (`Drop`, `RecordAndSample`) — with `constexpr` `to_string`/`from_string`, non-throwing `to_json`/`from_json` under `JSON_NOEXCEPTION=1`, and `std::formatter` specializations, and extend the module-wide serialization contract to every public tracing type (member + free `to_string`, non-throwing `nlohmann::json` where the type is a value type, `std::formatter`).
- Add trace identifiers and propagation: `TraceId`/`SpanId` from a non-zero 64-bit wait-free `thread_local` xorshift generator; `SpanContext` — a trivially copyable `{trace_id, span_id, sampled}` link passed across threads/queues; automatic intra-thread parent tracking through a fixed-depth `thread_local` active-span stack with explicit-parent override.
- Add `SpanAttribute` — a fixed-size trivially copyable tagged union (`std::int64_t`, `std::uint64_t`, `double`, `bool`, bounded inline string) with `MaxAttributes` inline attributes per span — and `SpanRecord`: a cache-line-aligned trivially copyable record with ids, parent, kind, status, cached `SYS_gettid`, start/end `nexigen::chrono` timestamps, `std::source_location`, span name, and the inline attribute array.
- Add `concepts::Tracer` / `concepts::TraceSink` and `NullTracer`/`NullTraceSink` whose disabled path compiles down to zero instructions.
- Add the sink family: `ConsoleTraceSink`, `JsonFileTraceSink` (JSONL + UTC ISO-8601, hourly/daily rotation, rate-limiting with a drop summary), `ChromeTraceSink` (Chrome Trace Event / Perfetto-compatible JSON loadable at `ui.perfetto.dev`), and variadic `CompositeTraceSink<Sinks...>` fan-out.
- Add the factory hierarchy and wait-free sharding: `TracerProvider` owns a pre-allocated pool of per-thread `SpscRingBuffer<SpanRecord, ShardCapacity>` shards (`Max_threads = 32`) plus a `std::jthread` background consumer pinned via `core_mask` (`pthread_setaffinity_np`); `Tracer` is created exclusively by `TracerProvider::get_tracer(scope_name)`; `ScopedSpan` is the RAII guard recording start on construction, `set_attribute`/`set_status` `noexcept`, and pushing into the caller's private shard in ≤ 20 ns with zero heap allocations and zero locks on destruction or `end()`. Runtime enable/disable and sampling short-circuit through relaxed atomics (~1 ns when disabled/dropped); drops are visible (`buffer_full`, `rate_limited`, `slot_exhausted`).
- Add `tests/tracing/tracing_test.cpp` (concept `static_assert`s, automatic and cross-thread parent-child, attribute packing, affinity pinning, drop counters, sink output validity, full `to_string`/`nlohmann::json`/`std::formatter` coverage, Release gate asserting `start_span` + `end` enqueue ≤ 20 ns), two runnable examples (`ex_nested_and_cross_thread_spans`, `ex_perfetto_chrome_trace`), CMake/CTest wiring, and the `examples` catalogue growth from eight to ten executables.

## Capabilities

### New Capabilities

- `tracing/span-enums`: span kind/status/sampling enum catalogue plus the module-wide `to_string`/`from_string`, `nlohmann::json`, and `std::format` contract for every public tracing type.
- `tracing/span-context`: trace/span identifiers, the propagable `SpanContext`, `thread_local` parent tracking, bounded `SpanAttribute` union, and the `SpanRecord` shape.
- `tracing/scoped-span`: the RAII span lifecycle — wait-free attribute/status updates, the ≤ 20 ns shard enqueue, sampling/enable short-circuit, and drop visibility.
- `tracing/tracer-concepts`: `Tracer`/`TraceSink` structural concepts, `NullTracer`/`NullTraceSink` zero-instruction disabled path.
- `tracing/trace-sinks`: console, rotating rate-limited JSONL, Chrome Trace Event (Perfetto), and composite sink implementations.
- `tracing/tracer-provider`: provider/tracer factory hierarchy, pre-allocated per-thread SPSC shard pool, background consumer with CPU affinity, runtime control, and shutdown drain.
- `tracing/examples`: the two runnable tracing executables and their observable behavior.

### Modified Capabilities

- `examples`: the runnable example catalogue expands from eight executables to ten, adding `ex_nested_and_cross_thread_spans` and `ex_perfetto_chrome_trace` under `examples/tracing/`, and the README invocation guide names all ten.

## Impact

- **New public headers**: `include/nexigen/tracing/` — `span_enums.hpp`, `span_context.hpp`, `span_record.hpp`, `concepts/tracer_concept.hpp`, `tracer.hpp`, `tracer_provider.hpp`, `scoped_span.hpp`, `null_trace_sink.hpp`, `console_trace_sink.hpp`, `json_file_trace_sink.hpp` (+ its config), `chrome_trace_sink.hpp`, `composite_trace_sink.hpp`, plus `detail/` helpers (shard pool, id generation, fixed JSON writer reuse).
- **New sources**: `src/tracing/` for cold paths — provider lifecycle/consumer loop, sink I/O and rotation, record serialization. All templated hot paths stay inline in headers.
- **New build target**: `nexigen_tracing` (`nexigen::tracing`) linking `nexigen_core` and `nexigen_chrono` publicly, `Threads::Threads` privately, with `_GNU_SOURCE` for affinity and `SYS_gettid`.
- **New tests/examples**: `tests/tracing/tracing_test.cpp` registered as `nexigen_tracing_tests` with a Release-only ≤ 20 ns enqueue gate; `examples/tracing/` added to `examples/CMakeLists.txt` and the README.
- **Performance**: `ScopedSpan` destruction costs ≤ 20 ns (Release gate with headroom); the disabled path costs one relaxed atomic load (~1 ns); span push is wait-free (one `SPSC` `try_push` on the caller's own shard — 1–2 loads + 1 store, no CAS). Recording never allocates, never locks, never formats: names, `source_location` and attributes are packed raw and rendered cold-side by the consumer/sink.
- **Memory allocations**: zero heap allocation during span recording, attribute updates, and background consumption; the shard pool is pre-allocated once at `TracerProvider` construction (one aligned block sized by `Max_threads × ShardCapacity × sizeof(SpanRecord)`; capacity fields are user-overridable before construction). `std::string`/`nlohmann::json` stay cold-path (sink rendering, diagnostics, rotation bookkeeping).
- **Dependencies**: no new third-party dependency; reuses `nlohmann::json` via `nexigen_core`, the `SpscRingBuffer` from `nexigen_core`, `nexigen::chrono` stamping/formatting, and the `pthread`/`sched` surface already used by `logging` and `metrics`.
- **Compatibility**: additive; no existing public API or spec requirement is removed. The `examples` catalogue text changes only to include the two new executables.
