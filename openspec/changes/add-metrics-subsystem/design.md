# Design

## Context

See proposal.md — Why. Current state: the metrics footprint exists but is empty (`include/nexigen/metrics/`, `src/metrics/`, `tests/metrics/` hold only `.gitkeep`). Reusable precedents in-tree: `core::Result`/`core::Status` and the enum serialization trio in `core/result.hpp` (constexpr `to_string`/`from_string`, `get_ptr`-based non-throwing `from_json`, templated-context `std::formatter`); `chrono::format_utc_iso8601_ns` for `YYYY-MM-DDTHH:MM:SS.nnnnnnnnnZ` rendering and `sample_utc_offset`/`utc_from_mono_ns` for UTC stamps; `logging::detail::affinity` (`pin_self`/`self_affinity` over `pthread_setaffinity_np`, uint64 core mask); `logging::detail::Fixed_writer` for bounds-checked JSON line assembly; the zero-dependency test harness (`NX_CHECK`/`run_suite`, arena `operator new` counter + `mallinfo2` delta) and the Release-only `NEXIGEN_LOGGING_STRICT_LATENCY` gate pattern.

Hard constraints unchanged: C++23 without extensions, `-fno-exceptions`/`-fno-rtti`, `JSON_NOEXCEPTION=1`, zero heap allocation on hot paths and in background loops (init-time pre-allocation allowed and expected), lock-free/wait-free hot paths with `alignas(64)` false-sharing control, and mandate 8–11 naming/serialization/formatting rules.

## Goals / Non-Goals

**Goals:**
- One-time pre-allocation at `MetricProvider` construction; zero dynamic allocation in creation, recording, harvesting, sink callbacks, and shutdown.
- Wait-free hot paths: no locks, no blocking, no cross-core CAS contention for registered threads (per-thread sharding).
- Exact accounting: every recorded sample lands in exactly one observation window; counts/sums exact under any thread interleaving at the documented capacity.
- Deterministic control plane (`collect_now`/`flush`/`startup_status`) so tests never depend on wall-clock scheduling.
- All public types on one `to_string`/JSON/`std::formatter` contract; TSan- and ASan-clean.

**Non-Goals:**
- No metric labels/attributes/tags (single-dimension named metrics only).
- No OTLP/Wire exporters, pull endpoints, or cross-process aggregation — sinks are the only export surface.
- No runtime removal or resizing of instruments; the registry is append-only for the provider's lifetime.
- No exemplars, no histograms with unbounded bucket counts, no configurable percentile lists (p50/p90/p99/p99.9 are fixed).
- No Windows/macOS affinity support (Linux `pthread_*affinity_np`, matching logging).

## Decisions

### D1 — Module shape: one STATIC target `nexigen_metrics` (alias `nexigen::metrics`)

`add_library(nexigen_metrics STATIC ...)` globbed from `src/metrics/*.cpp` (`CONFIGURE_DEPENDS`, matching chrono/logging), `target_link_libraries(nexigen_metrics PUBLIC nexigen_core nexigen_chrono PRIVATE Threads::Threads)`, plus `target_compile_definitions(nexigen_metrics PUBLIC $<$<PLATFORM_ID:Linux>:_GNU_SOURCE>)` for `cpu_set_t`/`pthread_setaffinity_np` (same reason as `nexigen_logging`; it widens the `_POSIX_C_SOURCE=200809L` already inherited from `nexigen_core`). `nexigen_chrono` is PUBLIC because `MetricBatchSnapshot` and the JSON sink carry chrono timestamps in public headers.

*Alternative considered:* header-only INTERFACE like core. Rejected: the harvest loop, sink I/O, and snapshot serialization are compiled cold paths that would bloat every consumer TU.

### D2 — Header and source layout

Public headers under `include/nexigen/metrics/`: `policy_enums.hpp`; `concepts/metric_concept.hpp` (`Metric`, `ReadableMetric`) and `concepts/metric_sink_concept.hpp` (`MetricSink`); `metric_snapshot.hpp` (`MetricIdentity`, `ScalarValue`, `StatisticsSnapshot`, `HistogramSnapshot`, `MetricRecord`, `MetricBatchSnapshot`); `meter.hpp` (`MeterPasskey`, `Meter`); `metric_provider.hpp` (`MetricProvider_config`, `MetricProvider`); `counter.hpp`, `gauge.hpp`, `descriptive_statistics.hpp`, `explicit_histogram.hpp`, `exponential_histogram.hpp`; `null_metric_sink.hpp`, `console_metric_sink.hpp`, `json_file_metric_sink.hpp` (+ its config), `composite_metric_sink.hpp`. Internal: `detail/thread_slot.hpp`, `detail/affinity.hpp`, `detail/json_writer.hpp`, `detail/welford.hpp`.

Sources: `src/metrics/affinity.cpp`, `src/metrics/json_writer.cpp`, `src/metrics/console_metric_sink.cpp`, `src/metrics/json_file_metric_sink.cpp`, `src/metrics/provider.cpp` (lifecycle, harvest loop, control plane), `src/metrics/snapshot_format.cpp` (record/batch/config `to_string`/`to_json`). All instrument classes and `Meter` creation methods are templates and stay inline in headers.

`detail/affinity.hpp` duplicates logging's 60-line `pin_self`/`self_affinity` wrapper instead of reaching into `nexigen::logging::detail` — the chrono change established that modules do not consume another module's `detail/`, and linking `nexigen_logging` from metrics would invert the dependency direction. *Alternative considered:* promote affinity to `nexigen::core`; rejected — core is INTERFACE-only and affinity is a compiled concern (a later consolidation change can revisit).

### D3 — Naming

Request-mandated spellings win over mandate 8 where they conflict: `MetricProvider_config`, `MetricBatchSnapshot`, `MeterPasskey`, `Max_threads` keep their given forms; fields and locals are `snake_case`, private members trailing `snake_case_`, types otherwise PascalCase (`Counter`, `Meter`, `ConsoleMetricSink`, `StatisticsSnapshot`).

### D4 — Policy enums and the serialization contract

Each enum is `enum class X : std::uint8_t` with an append-only value order. `to_string` is a `switch` with a `default: return "Unknown"` static-storage arm (garbage-cast safe, mirrors `core::ErrorCode`); `from_string` is chained canonical comparisons that leave the out-parameter untouched on failure. `to_json`/`from_json` copy the `result.hpp` pattern verbatim (`get_ptr<const nlohmann::json::string_t*>()`, noexcept reader). `std::formatter<Enum> : std::formatter<std::string_view>` uses a **templated format context** (libstdc++ 15 evaluates formattability against a substituted context; a hard-coded `std::format_context&` would not bind — same note as `ErrorCode`'s formatter).

Public classes/structs get member `to_string() const` + free `to_string(const T&)` + `to_json`/`from_json` where the type is a value type, and `std::formatter` delegating to `to_string()`. Instrument classes and runtime sinks render via `to_json` but expose **no** `from_json` (their reconstruction is factory- or I/O-bound); this is pinned by negative `static_assert`s.

### D5 — Concepts, passkey construction, and handle types

`concepts::Metric` requires `static constexpr` `kind`/`concurrency`/`access`/`reset` members of the right enum types plus `to_string() const`; `concepts::ReadableMetric` refines it with const-callable `read()`/`snapshot()`. Read gating is a member constraint: `auto read() const requires (Access == AccessPolicy::Readable)`. The harvester reaches `WriteOnly` instruments through `friend class MetricProvider` and a private `harvest_into(MetricRecord&) const`.

Construction: instrument constructors are private and take `const MeterPasskey&`; `MeterPasskey` has a private default constructor with `friend class Meter`; `Meter` itself is only obtainable from `MetricProvider::get_meter`. `static_assert(!std::is_constructible_v<Counter<...>, ...>)`-style pins live in the test TU. Creation returns `core::Result<T*>` (a raw pointer into the provider pool — `std::expected` cannot carry references); call sites use `(*res)->inc()` after a `has_value()` check. Handles are valid until provider destruction.

*Alternative considered:* creation handles (`Counter_ref` wrappers with forwarding methods). Rejected as an extra indirection layer on the hot path ergonomics; raw pool pointers match the "objects live in the registry" model.

### D6 — Registry layout and memory budget

`MetricProvider_config` fields: `collect_interval` (`std::chrono::nanoseconds`, 0 disables the background thread), `core_mask` (`std::uint64_t`, 0 = inherit), and capacities `max_meters=16`, `max_counters=64`, `max_gauges=64`, `max_statistics=32`, `max_explicit_histograms=16`, `max_exponential_histograms=8` (defaults, user-overridable before construction).

At construction the provider performs exactly **one** `::operator new(bytes, std::align_val_t{64})` covering every arena and table; nothing allocates afterwards. Layout (all bases `alignas(64)`):

- Per kind: a contiguous typed arena with a fixed slot stride (max size over that kind's policy instantiations) and a parallel `SlotMeta` table `{ char meter_name[64]; char meter_version[64]; char metric_name[64]; uint8 kind, concurrency, access, reset; void* obj; harvest/destroy thunk pointers }`, traversed contiguously at harvest.
- `alignas(64)` alignment of each arena base and each slot stride keeps hot instruments on their own cache lines.

Default budget (x86-64, ResetOnRead-sized strides): counters ≈ 135 KiB, gauges ≈ 135 KiB, statistics ≈ 210 KiB (33 slots × ~6.4 KiB), explicit histograms ≈ 660 KiB (33 slots × ~2 × 65 buckets), exponential histograms ≈ 1.2 MiB (33 slots × ~2 × 256 buckets), `MetricBatchSnapshot` scratch (one batch buffer sized to total instrument capacity ≈ 200 records × ~4.5 KiB ≈ 900 KiB), meta tables ≈ 40 KiB. **Total ≈ 3.3 MiB pre-allocated with defaults**; capacities scale this linearly and are configurable.

Creation = pop a free slot (bump index + occupancy check), placement-`new` the instrument, fill `SlotMeta`. Errors: `InvalidArgument` (empty/over-long/duplicate name, bad histogram parameters), `BufferFull` (kind arena exhausted), `NotInitialized` (provider liveness flag cleared). Repeated `get_meter` with the same name returns the existing handle; the same name with a different version returns `InvalidArgument`.

### D7 — Thread slots and shard sizing

`detail::thread_slot()` returns the calling thread's slot via a lazy `thread_local const std::uint8_t` initialized from a process-wide `std::atomic<std::uint32_t>` counter (`fetch_add(1, relaxed)`). The first `Max_threads = 32` threads get exclusive slots `0..31`; later threads get slot `32`, the shared fallback cell present in every multi-threaded instrument. Slots are never recycled (HIL thread sets are stable; documented). Per-thread shard arrays therefore have 33 cells.

### D8 — Counter and Gauge layout

- `Counter<SingleThreaded, ...>`: `alignas(64) std::uint64_t value_{0}` (plain, one cache line). `inc()`/`add()` are plain increments — sub-nanosecond class. **Contract:** single recording thread; reads/harvests must not run concurrently with recording unless externally synchronized (see D16).
- `Counter<MultiThreaded, ...>`: `struct alignas(64) Shard { std::atomic<std::uint64_t> value{0}; }` (exactly 64 B), `Shard shards_[33]`. Hot path: `shards_[thread_slot()].value.fetch_add(v, std::memory_order_relaxed)` — wait-free, own-cache-line RMW, no cross-core contention. Harvest: `load(relaxed)` sum (Cumulative) or `exchange(0, relaxed)` per shard (ResetOnRead — each increment is one RMW, so it lands in exactly one window).
- `Gauge<ValueType, ...>`: same shard geometry with `std::atomic<ValueType>` cells (`std::int64_t`, `double`). `add`/`sub` = `fetch_add(±v, relaxed)` on the caller's shard (`std::atomic<double>::fetch_add`, C++20, a CAS loop only on the caller's own line). `set(v)` = `fetch_add(v - aggregate_relaxed(), relaxed)` on the caller's shard: the aggregate moves to `v` as observed, with concurrent other-thread deltas contributing on top (documented semantics). Reset windows as for `Counter` (ResetOnRead zeroes via per-shard `exchange(0, relaxed)`).

Memory orders: recording `fetch_add(..., relaxed)`; harvesting `load(..., relaxed)` / `exchange(0, relaxed)`. Rationale: metric values publish no other data and need no cross-variable ordering; relaxed keeps the hot path at one instruction.

### D9 — DescriptiveStatistics: Welford cells with double-buffered slots

Welford cell `W { uint64 count; double mean, m2, min, max; }` (population `std = sqrt(m2/count)`, `0` at `count <= 1`). Per-record update is O(1): one division, two multiplies, two compares. Slots merge with the Chan parallel-merge formula: `delta = mean_b - mean_a; mean = mean_a + delta*n_b/n; m2 = m2_a + m2_b + delta^2*n_a*n_b/n`, `min`/`max` folded. `record()` rejects non-finite inputs into a relaxed `ignored_samples` counter.

`SingleThreaded`: one plain `alignas(64)` cell; same concurrency contract as `Counter` (D8).

`MultiThreaded`: `struct alignas(64) Slot { Bank bank[N]; std::atomic<uint32_t> seq; std::atomic<uint8_t> active; }` where each `Bank` is `alignas(64) W` padded to 64 B and `N = 2` for `ResetOnRead`, `N = 1` for `Cumulative`. Bank fields are `std::atomic<double>`/`std::atomic<uint64_t>` accessed **only** with relaxed loads/stores (never RMW) — on x86-64 these compile to plain `mov`, keeping the hot path allocation-free, lock-free, and TSan-visible (a plain-field seqlock would be TSan-flagged even when logically safe). The per-slot handshake (single writer per slot):

1. Producer: `seq.store(s + 1, relaxed)` (announce in-progress), apply the Welford update to `bank[active.load(relaxed)]`, `seq.store(s + 2, release)`; `s += 2`.
2. Harvester (Cumulative): `acquire` load of `seq`; retry while odd (bounded, e.g. 64 attempts, then defer the slot to the next window — no loss); read bank; re-check `seq` unchanged; merge.
3. Harvester (ResetOnRead): reach an even `seq`, then `old = active.exchange(1 - active, acq_rel)`, re-check `seq` even (an in-flight record that announced before the exchange completes into `bank[old]` and is included here — exactly once), merge `bank[old]`, plain-store zeroes into `bank[old]` (now the shadow; new producers write the other bank).

Producers are wait-free (two relaxed/release stores ≈ plain `mov` on x86); the swap never blocks; every record lands in exactly one window.

Threads without an exclusive slot (slot 32) cannot use the single-writer protocol — their `record()` increments the instrument's relaxed `dropped_samples` counter and returns. **`Max_threads = 32` is the documented exactness capacity for statistics**; the drop is visible, never silent. (Counter/Gauge/histograms stay exact past 32 threads — D8/D10.)

### D10 — ExplicitBucketHistogram

Instrument-level immutable `double bounds_[64]` + `bound_count_` (creation-validated: finite, strictly increasing, ≤ 64). Bucket `i` covers `(b_{i-1}, b_i]`, bucket 0 covers `(-Inf, b_0]`, the overflow bucket covers `(b_last, +Inf)`; lookup is binary search (≤ 6 compares). Shard banks hold `uint64 buckets[65]` + `count/sum/min/max` with the D9 slot geometry and handshake (relaxed-atomic fields, `N=2` for ResetOnRead). Non-finite samples go to `ignored_samples`. Percentiles are computed at snapshot time (cold path) by rank over merged bucket counts with linear interpolation inside the containing bucket, using observed `min` as the lower edge of bucket 0.

### D11 — ExponentialHistogram: exact O(1) mapping without `std::log`

`index(v) = floor(scale * log2(v))` for positive finite `v`, so bucket `i` covers `[base^i, base^(i+1))` with `base = 2^(2^-scale)`. Implementation per `record()`:

1. `bits = std::bit_cast<std::uint64_t>(v)`; `e = ((bits >> 52) & 0x7FF) - 1023`; mantissa fraction `m = (bits & ((1ULL << 52) - 1)) / 2^52`.
2. Candidate `k = scale*e + (scale * log2_1p_q24(m)) >> 24` — a fixed-point linear approximation of `log2(1+m)` guaranteed within ±(0.086·scale + 1) ≤ ±3 of the true index at `scale ≤ 20`.
3. Bounded correction (≤ 4 steps): compare `v` against exact boundary doubles `B(k) = 2^(k/scale) = ldexp(factor[k mod scale], floor_div(k, scale))`, where `factor[r] = 2^(r/scale)` is a per-instrument table built at creation with `std::pow` (cold path — allowed at initialization) — `ldexp` is integer exponent manipulation. Walk `k` until `B(k) <= v < B(k+1)`.

No `std::log`/`std::log2`/`std::pow` on the hot path, no allocation, deterministic, exact per the spec containment rule (pinned by boundary-sweep tests including ±1 ULP around every candidate boundary). Zeros go to a `zero_count` (ranks below all positive buckets); negative and non-finite values go to `ignored_samples`.

Bucket storage per shard bank: `uint64 buckets[256]` + `base_index` + `effective_scale` + `count/sum/min/max`, same D9 slot handshake. When an index falls outside `[base_index, base_index + 256)` the shard **coarsens**: `effective_scale` decreases by one (indices halve, buckets collapse pairwise) until the range fits — at most `scale` rebases per shard lifetime, each O(256); samples are never lost. At `effective_scale == 0` (base 2, 256 octaves) extreme-range values fold into the edge bucket — counts/sum/min/max stay exact, only percentile precision degrades (documented). Merging shards at harvest collapses to the minimum effective scale. Percentiles use bucket representative values (`B(i)` midpoints), within one bucket width of the true quantile.

### D12 — Snapshot and record shapes

All snapshot types are trivially copyable fixed-size structs with inline `char[64]` names (no `std::string`): `MetricIdentity { char meter_name[64]; char meter_version[64]; char metric_name[64]; }`; `StatisticsSnapshot { count, min, max, mean, std, ignored_samples, dropped_samples }`; `HistogramSnapshot { count, sum, min, max, ignored_samples, zero_count, effective_scale, p50/p90/p99/p999, bucket_count, buckets_lower[256], buckets_count[256] }`. `MetricRecord` = identity + kind + three policies + a union payload (`ScalarValue`/`StatisticsSnapshot`/`HistogramSnapshot`). `MetricBatchSnapshot` = collection stamp (`chrono::Timestamp` + `utc_ns`), `record_count`, and `MetricRecord records[capacity]`, kept as a pre-allocated `alignas(64)` provider member and filled by the harvest thunks. Serialization (`to_string`/`to_json`/`std::formatter`) lives in `snapshot_format.cpp` and header-inline formatters; it is cold-path by contract.

### D13 — Sinks and binding

Concept: `on_batch(const MetricBatchSnapshot&) noexcept -> core::Status` + `flush() noexcept -> core::Status`, no virtuals. Implementations:
- `NullMetricSink`: inline empty members.
- `ConsoleMetricSink { int fd = 1; }`: renders each batch into fixed stack buffers via a bounds-checked appender + `std::to_chars` (allocation-free numerics; no iostreams), emits with `::write` retrying `EINTR` and partial writes; unrecoverable errors → `SystemError`; `flush()` is a no-op success (write-through).
- `JsonFileMetricSink(JsonFileMetricSink_config{ path, append })`: opens at construction (`O_WRONLY|O_CREAT|O_CLOEXEC|O_APPEND|O_TRUNC`), writes one JSON object per batch per line through the metrics `detail::Fixed_writer` (mirroring logging's escaping policy and 4 KiB scratch discipline), stamps with `chrono::format_utc_iso8601_ns` over a sampled UTC offset; `flush()` = `::fsync` with I/O failures → `SystemError`.
- `CompositeMetricSink<Sinks...>`: `std::tuple<Sinks...>`, fan-out without short-circuit (first error wins), `get<Index>()`/`get<SinkType>()` with compile-time guards.

The provider binds a sink at construction through type-erased thunks (`Sink_binding { void* self; on_batch/flush fn pointers }`) created by a `template <concepts::MetricSink S>` constructor with a `static_assert`. *Alternative considered:* `MetricProvider<Sink>` template parameter; rejected — it would push the harvest loop into every consumer TU and couple `Meter` to the sink type; one indirect call per batch is cold-path.

### D14 — Harvester thread and control plane

`std::jthread` body: first act is `detail::pin_self(core_mask)` (mask != 0), publishing any failure into an atomic status read by `startup_status()` (release/acquire); then loop: harvest all `SlotMeta` tables into the batch buffer, `on_batch`, wait on a `std::condition_variable_any` until `steady_clock`-based deadline + `collect_interval` (fixed-rate, clamp on overrun) or stop. The mutex guards only the sleep — it is never held during harvesting or sink calls (background worker, not a hot path). `collect_now()` harvests and delivers synchronously on the calling thread; `flush()` forwards to the sink. Destructor: request stop, notify, join, one final harvest + `flush()`, then free the single allocation. All of it `noexcept` and allocation-free.

### D15 — Memory-order summary

| Component | Operation | Order | Why |
|---|---|---|---|
| Counter/Gauge shard | `fetch_add` (record) | `relaxed` | values publish nothing else |
| Counter/Gauge shard | `load` / `exchange(0)` (harvest/reset) | `relaxed` | per-RMW exactly-once, no ordering needed |
| Stats/hist slot `seq` | announce (producer) | `relaxed` store | paired with exit |
| Stats/hist slot `seq` | exit (producer) | `release` store | publishes the completed bank update |
| Stats/hist slot `seq` | handshake (harvester) | `acquire` load | observes completed updates before merging |
| Stats/hist bank fields | producer & harvester access | `relaxed` (load/store only) | single writer per slot; TSan-clean at `mov` cost |
| Slot `active` index | `exchange` (ResetOnRead swap) | `acq_rel` | both directions: producers adopt new bank, harvester finalizes old |
| Fallback cell (slot 32) | `fetch_add` / CAS `min`/`max` | `relaxed` RMW | multi-writer correctness over speed |
| Fallback cell reset | `exchange(0)` | `relaxed` | per-field exactly-once (record-level straddle documented) |
| `thread_slot` counter | `fetch_add` | `relaxed` | unique ids only |
| startup status | publish / read | `release` / `acquire` | one-shot publication |
| `ignored/dropped` counters | `fetch_add` | `relaxed` | diagnostics only |

### D16 — Tests and latency gates

Single TU `tests/metrics/metrics_test.cpp` on the project harness (copy `NX_CHECK`/`run_suite` + arena `operator new/delete` counter + `mallinfo2` delta from `tests/core/core_test.cpp`/`tests/logging/logging_test.cpp`; same `NX_SANITIZER_BUILD` detection). Suites: `policy_enums` (trio round-trips), `concepts` (positive `static_assert`s plus negative `requires`-expression detection for passkey construction and `WriteOnly::read()`), `registry` (capacity/BufferFull/InvalidArgument, zero-allocation windows), `counter`, `gauge`, `statistics`, `explicit_histogram`, `exponential_histogram` (boundary sweep: every exponent, ±1 ULP around candidate boundaries, pseudorandom doubles; coarsening invariants), `sinks` (pipe/temp-file fixtures, escaping, error paths), `provider` (interval spacing, `pthread_getaffinity_np` round-trip on the worker, `collect_now`/`flush`/shutdown drain), `serialization` (member/free `to_string`, JSON round-trips, `std::format`).

Release-only latency gates via `NEXIGEN_METRICS_STRICT_LATENCY` (same config gating as `NEXIGEN_LOGGING_STRICT_LATENCY`): median per-op over warm batched loops — `Counter` ST `inc()` ≤ 2 ns, `Counter` MT `inc()` ≤ 6 ns, `Gauge` `set`/`add` ≤ 6 ns, `DescriptiveStatistics::record` ≤ 25 ns, `ExplicitBucketHistogram::record` ≤ 15 ns, `ExponentialHistogram::record` ≤ 15 ns (headroom over the proposal targets; Debug/sanitizer builds run correctness only). Single-threaded instruments are exercised with `collect_now()` on the recording thread or with explicit release/acquire barrier protocols around harvest phases — that establishes the happens-before their plain storage requires and keeps TSan clean.

### D17 — Examples and documentation

`examples/metrics/ex_counters_and_stats.cpp`: provider + meter, ST/MT counter, both gauge types, statistics, `collect_now` + `ConsoleMetricSink`, `read()`/`snapshot()`/`to_string` output, threaded stress. `examples/metrics/ex_histograms_and_provider.cpp`: explicit + exponential histograms, `JsonFileMetricSink` + `CompositeMetricSink`, background harvesting with a `core_mask`, `ResetOnRead` windows, clean shutdown. Both wired with `nexigen_add_example` in `examples/metrics/CMakeLists.txt` (added to `examples/CMakeLists.txt`), and the README Runnable Examples section names all six executables.

## Risks / Trade-offs

- [Single-threaded instruments race the background harvester unless the user synchronizes] → documented contract (D8/D16): use `collect_now()` or establish happens-before; tests prove the barrier protocol; `MultiThreaded` is the safe default for background harvesting.
- [Exponential bucket mapping off-by-one at boundaries] → exact boundary construction via `ldexp` + per-scale factor table with bounded correction (D11); sweep tests at ±1 ULP pin the containment invariant; failure is caught in CI before release.
- [>32 recording threads degrade differently per instrument] → counters/gauges/histograms stay exact via the shared fallback cell (contention only); statistics drop visibly into `dropped_samples` — capacity is documented and tested.
- [Fallback-cell `ResetOnRead` can straddle a window at record granularity] → bounded by in-flight records at swap time; cumulative totals remain exact; documented and covered by stress tests.
- [Pre-allocated footprint ≈ 3.3 MiB with default capacities] → all capacities are `MetricProvider_config` fields; the single allocation happens once at construction; documented table (D6) lets integrators size for their platform.
- [Latency gates flake on loaded CI machines] → medians over batched warm-up loops with 2–4× headroom over targets, gates compiled out under Debug/sanitizers (logging precedent).
- [TSan cannot see through plain-field seqlocks] → banks use relaxed-atomic fields at `mov` cost (D9), so every cross-thread access is a proper atomic operation.
- [Duplicated affinity wrapper and JSON writer vs logging] → ~150 lines duplicated to keep module independence (D2); a future consolidation change can lift them into a shared home without API impact.
- [Exponential coarsening adds hot-path complexity] → bounded to ≤ `scale` O(256) rebases per shard lifetime; the loss-free invariant (bucket totals + zeros == accepted samples) is a test suite.

## Open Questions

None — all decisions above are pinned; anything that would change the specs, approach, or task breakdown is resolved here.
