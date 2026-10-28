# Design

## Context

See proposal.md — Why. Current state: `nexigen::metrics` is fully scaffolded (instruments, sinks, provider, tests) but (a) the module does not compile — `detail::thread_slot(MetricKind)` is called without its argument at 6 sites; (b) `CompositeMetricSink` requires default-constructible members (`composite_metric_sink.hpp:33-34`), so `CompositeMetricSink<ConsoleMetricSink, JsonFileMetricSink, NullMetricSink>` cannot be built because `JsonFileMetricSink` is config-constructed and non-copyable (`json_file_metric_sink.hpp:50-56`); (c) `examples/metrics/` does not exist (no `.gitkeep` placeholder either — the directory is created fresh). Established precedents: the `nexigen_add_example(name source target)` helper in `examples/CMakeLists.txt` (links target + `Threads::Threads`, registers a CTest smoke test with `TIMEOUT 600` and `LABELS "examples"`); the `examples/core`/`examples/logging` layout; and the ST-vs-background-harvester contract from the metrics design (D8/D16): single-threaded instruments must not race a background harvester. The main spec `openspec/specs/examples/spec.md` already carries a six-executable catalogue staged by the in-flight `add-metrics-subsystem` change; this change supersedes that text (user decision).

## Goals / Non-Goals

**Goals:**
- Four runnable examples that build and exit 0 in Debug, Release, TSan, and ASan, with TSan-clean concurrency patterns (no plain-storage instrument races a background harvester).
- Deterministic self-verification inside each example (value/structure checks), never wall-clock timing assertions.
- The two API enablers only: compiling `thread_slot()` call sites, and composite construction from configured members.
- Coherent spec deltas: the `examples` catalogue text cannot contradict `add-metrics-subsystem`'s staged wording after both changes land.

**Non-Goals:**
- Fixing the remaining known metrics defects found during verification (exponential ST merge frame `0U` at `exponential_histogram.hpp:286`, `to_json` consuming `ResetOnRead` statistics windows at `descriptive_statistics.hpp:209`) — those belong to `add-metrics-subsystem` and are unreachable from these examples (see D4). (The `ResetOnRead` swap-handshake defect originally listed here is now in scope as D9 — the examples' exactly-once window checks cannot pass without it.)
- Latency benchmarks, new instruments, API redesign beyond D3 and D9, pull/OTLP exporters, Windows/macOS.

## Decisions

### D1 — Four-example decomposition (supersedes the planned two)

One concern per TU: policies and access/reset semantics (`ex_counters_and_gauges`), windows and moments (`ex_descriptive_statistics`), histogram families (`ex_histograms`), and the end-to-end pipeline (`ex_metric_provider_and_sinks`). Each TU compiles in isolation, keeps sanitizer builds fast, and maps 1:1 to a `metrics/examples` requirement. *Alternative considered:* the two-executable split staged by `add-metrics-subsystem` (`ex_counters_and_stats`/`ex_histograms_and_provider`) — rejected by user decision; its tasks 13.1–13.3 are retired separately.

### D2 — Enabler 1: `thread_slot()` call-site fix

Pass each instrument's `MetricKind` at the call sites: `thread_slot(MetricKind::Counter)` (`counter.hpp:84`), `MetricKind::Gauge` (`gauge.hpp:81,85`), `MetricKind::DescriptiveStatistics` (`descriptive_statistics.hpp:146`), `MetricKind::ExplicitHistogram` (`explicit_histogram.hpp:180`), `MetricKind::ExponentialHistogram` (`exponential_histogram.hpp:237`). This matches the per-kind slot index spaces documented at `thread_slot.hpp:9-13` (`slot_kind_index` maps each kind to its own counter). Memory model unchanged: per-kind `std::atomic<std::uint32_t>` counter `fetch_add(1, relaxed)` (unique ids only, `thread_slot.hpp:60`), lazy `thread_local const std::uint8_t` slot; slots 0..31 exclusive, `fallback_slot = 32`, per-kind shard arrays stay `shard_count = 33` cells of `alignas(64)` 64-byte records. *Alternative considered:* a default `MetricKind` argument — rejected: it would silently collapse every instrument into one kind's slot space and over-subscribe shard arrays.

### D3 — Enabler 2: composite construction from configured members

Add a member-injection constructor `explicit CompositeMetricSink(Sinks... members)` that move-constructs `std::tuple<Sinks...> members_`, alongside the existing default construction for default-constructible packs (relax the `static_assert` at `composite_metric_sink.hpp:33-34` to cover only the default ctor). Because `JsonFileMetricSink` owns a file descriptor and deletes copy, give it a `noexcept` move constructor that transfers the fd and leaves the source closed (`fd = -1`); `ConsoleMetricSink`/`NullMetricSink` are trivially movable. Fan-out (`for_each` without short-circuit), first-error aggregation, `get<Index>()`/`get<SinkType>()`, and the diagnostics trio are untouched. *Alternative considered:* in-place construction from per-member config tuples — rejected as an intrusive API for one caller; a `make_composite` factory — unnecessary sugar.

### D4 — TSan-safe harvesting strategy per example

Single-threaded instruments race a background harvester unless externally synchronized (metrics design D8/D16), so examples pair modes deliberately:

- `ex_counters_and_gauges`, `ex_descriptive_statistics`, `ex_histograms`: `MetricProvider_config::collect_interval = 0` (no background thread); harvests happen via `collect_now()` on the recording thread after workers join. Where live `read()` runs concurrently with producers, the instrument is `MultiThreaded` (the seq handshake and relaxed-atomic banks are the designed cross-thread surface).
- `ex_metric_provider_and_sinks`: background harvesting with `collect_interval = 50 ms`, and **every** instrument is `MultiThreaded` — producers `fetch_add(..., relaxed)` on their own `alignas(64)` shard, the harvester folds with `load(relaxed)` / `exchange(0, relaxed)` per shard; statistics/histograms use the existing `seq` handshake (`relaxed` announce, `release` exit, `acquire` harvest). No plain-storage instrument is ever harvested concurrently.

No new concurrent components are introduced; all layout and `std::memory_order` choices are the existing ones from the metrics design D7–D9/D15.

### D5 — Self-verification and exit codes

Each example prints diagnostics *and* asserts its own invariants on the cold path (exact totals, `5,0,3`-style delta windows, percentile ordering `p50 <= p90 <= p99 <= p99.9`, JSONL parse-back), returning non-zero on the first mismatch. Recording loops call only allocation-free instrument APIs; printing, `std::format`, `nlohmann::json` parsing, and file reads happen outside those loops (mandates 4/9–11 apply to library hot paths; example main() is cold-path code and may allocate).

### D6 — CMake and CTest wiring

`examples/metrics/CMakeLists.txt` calls `nexigen_add_example(<name> <source>.cpp nexigen::metrics)` per executable and `examples/CMakeLists.txt` gains `add_subdirectory(metrics)`. The helper already links `Threads::Threads` and registers the CTest entry (`TIMEOUT 600`, `LABELS "examples"`); `nexigen_options` (warnings/optimizations/sanitizers) applies transitively via `nexigen_metrics` → `nexigen_core` → `nexigen_options`, exactly as for the core/logging examples — satisfying the request's linking intent without changing the shared helper. *Alternative considered:* adding an explicit `target_link_libraries(... nexigen_options)` — redundant, and editing the shared helper would churn existing examples.

### D7 — Robust output path and CI-safe affinity

`ex_metric_provider_and_sinks` creates `/tmp/nexigen-examples-metrics/` itself (cold-path `std::filesystem::create_directories`) and derives `core_mask` from the process's allowed CPU set (query current affinity, select the lowest permitted CPU) instead of hardcoding a core, so `pthread_setaffinity_np` succeeds in containers and CI runners; `startup_status()` is checked and reported. The JSONL file name is unique per run to keep repeated `ctest` invocations append-safe (`JsonFileMetricSink_config{path, append}`).

### D8 — Spec organization

Example *behavior* (what each executable demonstrates) is a new `metrics/examples` capability — the metrics-domain half; the catalogue-level contract (names, exit status, CTest coverage, build-mode matrix, README) stays in the existing `examples` capability via the two MODIFIED requirements. This honors the request's "delta specs under `specs/metrics/`" without duplicating requirements that `examples` already owns.

### D9 — Enabler 3: `ResetOnRead` swap-handshake fix (scope addition, user-approved)

The double-bank handshake in `DescriptiveStatistics::take_slot` currently spins ≤64 times for an even `seq` and then unconditionally `load_state()` + `clear()`s the taken bank. Racing a multi-step `Stats_bank::apply()` — which ends in `count.store(seen + 1)` — that clear *tears* the bank: the late store resurrects already-counted samples into the cleared bank (observed window sums up to 1,022,319 for 1,000,000 records) or loses the in-flight one (sums of 999,998/999,999). This violates the `metrics/descriptive-statistics` scenario "records racing a swap land exactly once".

The fix adopts the protocol the metrics design D9 already prescribes: after `active.exchange(...)`, load the bank under a **stable even `seq`** (read, then re-check `seq` unchanged) before clearing; if the bounded spin exhausts, touch nothing and return empty — the bank keeps its content and is read at a later swap ("deferred to the next window — no loss"). Two ordering hazards surfaced under `-O3` and are closed explicitly: the producer's announce is a `seq_cst` store (a full barrier, `xchg` on x86) so the bank update can never become visible before the announcement (a relaxed announce let `apply()`'s loads bypass the store buffer and tear the window — reproduced 8/10 at `-O3`), and the consumer pins its bank reads inside the checked window with an `acq_rel` fence. Cost: one locked op per record — the same class the sharded `Counter`/`Gauge` hot paths already pay with `fetch_add`. The consumer keeps its bounded spin and `acquire` loads. Pinned by a concurrent exactly-once regression in the test TU and the example's live-poll drain (two consecutive empty windows, since a racing record may legitimately land one window later).

### D10 — Enabler 4: exponential coarsening loss fix (scope addition, user-approved)

`Exponential_bank::coarsen()` collapses buckets pairwise into a fresh window but **discards** every bucket whose collapsed position falls outside `[0, 256)` — proven lossy with the deterministic sequence `record(2.0); record(2.2); record(1.0)` at scale 10 (count=3, bucketed=1). A low-side value re-anchoring the window below its base is the trigger. This violates the `metrics/exponential-histogram` scenario "Wide ranges coarsen without losing samples — bucket totals + zero_count == accepted samples" and blocks the example's totals check.

The fix folds out-of-window buckets into the edge buckets during collapse (`position < 0` → slot 0, `position ≥ 256` → slot 255) — the same edge-folding `merge_bank` and the `scale <= 0` path of `apply_to_bank` already use. Producer hot path and memory orders unchanged; the collapse stays O(256). Pinned by a deterministic low-side re-anchor regression in the test TU. (The exponential ST merge-frame bug at `exponential_histogram.hpp:286` remains out of scope per Non-Goals — the examples use the MT path only.)

### D11 — Enabler 5: registration-vs-harvest publication fix (scope addition, user-approved)

The background harvester starts in the `MetricProvider` constructor, but `Meter::create_*` published its `Slot_meta` through `reserve_slot` with plain stores — `occupied = true` included, *before* the instrument was placement-new'd — while `collect_now()` read those fields with plain loads under `harvest_mutex_`, which registration never took (TSan: 23 reports in the pipeline example; a general `metric-provider` defect — any `create_*` during background collection was racy).

The fix wraps each `Meter::create_*` body in `std::lock_guard<std::mutex> lock(provider_->harvest_mutex_)` (`Meter` is a `friend` of `MetricProvider`; `<mutex>` already included). Construction, `identity_` binding, parameter installation (`install_bounds`), and slot publication then happen-before the next harvest observes `occupied` — both sides serialize on the mutex `collect_now()` already holds across traversal and sink delivery. Cold path only: creation is explicitly allowed to synchronize (it is initialization-time code); no instrument hot path and no harvest thunk changes. `get_meter` stays lock-free — the harvester never reads the meter table (identity is copied into `SlotMeta` at creation). Pinned by a regression suite that creates instruments in a tight loop while the background harvester runs (TSan-clean required).

## Risks / Trade-offs

- [Two active changes define the examples catalogue] → this change's MODIFIED block supersedes `add-metrics-subsystem`'s staged wording; retire its tasks 13.1–13.3 and `examples` delta via `/opsx-update` before it archives. If it archives first, this change's delta lands on top at archive time either way.
- [Latent metrics bugs surface in example output] → D4 confines examples to MT paths and `collect_now()`; the known ST-only defects (`exponential_histogram.hpp:286`) are unreachable here. Any matrix failure blocks the gate and gets fixed before archive.
- [Sink move semantics could double-close the fd] → move leaves the source closed (`fd = -1`); pinned by a composite-construction unit test in `tests/metrics/metrics_test.cpp`.
- [Interval/affinity behavior under loaded CI] → scenarios assert only that batches are non-decreasing and `startup_status()` succeeds; no timing thresholds. Examples never assert on latency.
- [JSONL read-back brings nlohmann into examples] → cold-path only with `JSON_NOEXCEPTION=1`, consistent with the serialization mandates; no hot-path usage.

## Migration Plan

Additive only. Order: enablers (D2, D3, D9–D11) → examples + wiring (D1, D6, D7) → README → matrix gate. Rollback is a directory revert; the behavior changes to existing code are the composite constructor addition (default construction preserved), the swap-handshake fix (D9), the coarsening fold (D10), and registration serialization (D11) — all pure correctness, same APIs and hot-path memory orders.

## Open Questions

None — the two material questions (supersession scope, compile-fix ownership) were resolved with the user before planning.
