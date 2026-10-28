# Design

## Context

See proposal.md — Why. Current state: the tracing footprint exists but is empty (`include/nexigen/tracing/`, `src/tracing/`, `tests/tracing/`, `examples/tracing/` hold only `.gitkeep`). Reusable precedents in-tree: `core::SpscRingBuffer` (wait-free SPSC with per-end cache-line cells), `core::Result`/`core::Status` and the enum serialization trio pattern in `core/result.hpp` / `logging/log_level.hpp` / `metrics/policy_enums.hpp`; `chrono::Timestamp` and the single-writer calibrated-TSC `chrono::Stamp_source` (a fresh `clock_gettime` alone measures ~20-25 ns and would eat the whole budget — same reason logging shards one `Stamp_source` per slot); `logging::detail::Producer` (per-thread `thread_local` slot registration over ≤ 32 SPSC shards, drop counters `Dropped_stats`, window throttle with drop summary); `logging::detail::Async_worker` (self-pinning `std::jthread` consumer, idle backoff, flush handshake); `logging::detail::affinity` / `metrics::detail::affinity` (`pin_self`/`self_affinity` over `pthread_setaffinity_np`); the metrics provider's one-block pre-allocation and passkey factory; the zero-dependency test harness (`NX_CHECK`/`run_suite`, arena `operator new` counter, `mallinfo2` delta, `NX_SANITIZER_BUILD` detection) and the Release-only `NEXIGEN_*_STRICT_LATENCY` gate pattern; the `nexigen_add_example` CMake function and the ten-entry README catalogue this change extends.

Hard constraints unchanged: C++23 without extensions, `-fno-exceptions`/`-fno-rtti`, `JSON_NOEXCEPTION=1`, zero heap allocation on hot paths and in background loops (init-time pre-allocation allowed and expected), lock-free/wait-free hot paths with `alignas(64)` false-sharing control, and mandates 8–11 naming/serialization/formatting rules.

## Goals / Non-Goals

**Goals:**
- One-time pre-allocation inside `TracerProvider`; zero dynamic allocation in tracer creation, span start/update/end, shard drain, and sink delivery.
- Wait-free producer path: no locks, no blocking, no cross-thread CAS for registered threads (exclusive per-thread SPSC shards); full span lifecycle (`start_span` + destruction enqueue) ≤ 20 ns in Release.
- Disabled/dropped short-circuit at one relaxed atomic load (~1 ns) with no TLS, id, or timestamp work.
- Correct causal linkage: automatic intra-thread parent chains, cross-thread linking through an explicit `SpanContext`, visible drops (`buffer_full`, `rate_limited`, `slot_exhausted`).
- Sinks that produce valid JSONL and a Perfetto-loadable Chrome trace file, all rendering cold-side on the consumer thread.
- All public types on the `to_string`/`nlohmann::json`/`std::formatter` contract; TSan- and ASan-clean.

**Non-Goals:**
- No span events/logs inside spans, no links (OTel-style multiple parents), no continuous sampling policy engines — the sampling vocabulary is the two-value `SamplingDecision` applied at start.
- No OTLP/Wire exporters, no cross-process trace propagation (no W3C `traceparent` headers) — `SpanContext` crosses threads and queues within one process only.
- No runtime resizing of the shard pool or tracer pool; capacities are compile-time template parameters set at instantiation.
- No Windows/macOS affinity support (Linux `pthread_setaffinity_np`, matching logging/metrics).
- No removal or change of any existing module API; the only existing-spec edits are the `examples` catalogue text (8 → 10 executables) and README wording.

## Decisions

### D1 — Module shape: one STATIC target `nexigen_tracing` (alias `nexigen::tracing`)

`add_library(nexigen_tracing STATIC ...)` globbed from `src/tracing/*.cpp` (`CONFIGURE_DEPENDS`, matching chrono/logging/metrics), `target_link_libraries(nexigen_tracing PUBLIC nexigen_core nexigen_chrono PRIVATE Threads::Threads)`, plus `target_compile_definitions(nexigen_tracing PUBLIC $<$<PLATFORM_ID:Linux>:_GNU_SOURCE>)` for `cpu_set_t`/`pthread_setaffinity_np`/`SYS_gettid` (it widens the inherited `_POSIX_C_SOURCE`). `nexigen_chrono` is PUBLIC because `SpanRecord` carries `chrono::Timestamp` and the provider exposes `Stamp_source` calibration in a public header. No new third-party dependency; `nlohmann::json` arrives transitively through `nexigen_core` with `JSON_NOEXCEPTION=1`.

*Alternative considered:* header-only INTERFACE like core. Rejected: the consumer loop, sink I/O, rotation, and record serialization are compiled cold paths that would bloat every consumer TU (same reasoning as logging/metrics).

### D2 — Header and source layout

Public headers under `include/nexigen/tracing/`: `span_enums.hpp` (`SpanKind`, `SpanStatus`, `SamplingDecision` + the serialization trio); `span_context.hpp` (`TraceId`, `SpanId`, `SpanContext`); `span_record.hpp` (`SpanAttributeType`, `SpanAttribute`, `SpanRecord`); `concepts/tracer_concept.hpp` (`concepts::Tracer`, `concepts::TraceSink`, `NullTracer`); `scoped_span.hpp` (`ScopedSpan`); `tracer.hpp` (`SpanPasskey`, `Tracer`); `tracer_provider.hpp` (`TracerProvider_config`, `Dropped_stats`, `TracerProvider`); `null_trace_sink.hpp`, `console_trace_sink.hpp`, `json_file_trace_sink.hpp` (+ `JsonFileTraceSink_config` with `RotationPolicy`), `chrome_trace_sink.hpp` (+ `ChromeTraceSink_config`), `composite_trace_sink.hpp`. Internal: `detail/id_generator.hpp`, `detail/active_span_stack.hpp`, `detail/affinity.hpp`, `detail/json_writer.hpp`. (The sharding state deliberately has no separate header: D5 places the whole pool inline in the provider object, so the shard ring array, per-slot stamps, per-slot `Shard_state` cells, and the registration/exhausted/control/start/flush cells are members of `TracerProvider` itself — not a standalone `detail/shard_pool.hpp`.)

Sources: `src/tracing/affinity.cpp` (`pin_self`/`self_affinity`), `src/tracing/json_writer.cpp` (bounded fixed writer + escaping, mirroring logging's policy), `src/tracing/provider.cpp` (lifecycle, consumer loop, flush handshake, control plane), `src/tracing/record_format.cpp` (id/context/attribute/record `to_string`/`to_json`/`from_json`), `src/tracing/sink_format.cpp` (sink/config serialization). All templated hot paths (provider `finish_span`, `Tracer::start_span`, `ScopedSpan` updates) stay inline in headers.

`detail/affinity.hpp` duplicates logging's/metrics' ~60-line wrapper (module-independence rule: modules never consume another module's `detail/`; a future consolidation change can lift it into a shared home).

### D3 — Naming

Request-mandated spellings win over mandate 8 where they conflict: `MaxAttributes` (inline attribute capacity, value 4) and the `Max_threads` template parameter (value 32) keep their given forms; `SpanContext`, `ScopedSpan`, `TracerProvider`, `SpscRingBuffer` reuse keep theirs. Everything else: PascalCase types/enums/concepts, `snake_case` functions/variables, trailing-underscore private fields, aggressive `noexcept`/`constexpr`/`[[nodiscard]]`.

### D4 — Enums and the serialization contract

`SpanKind`, `SpanStatus`, `SamplingDecision`, `SpanAttributeType`, and `RotationPolicy` are `enum class ... : std::uint8_t` with append-only value order. `to_string` is a `switch` with a `default: return "Unknown"` static-storage arm (garbage-cast safe, mirrors `core::ErrorCode`); `from_string` is chained canonical comparisons leaving the out-parameter untouched on failure. `to_json`/`from_json` copy the `result.hpp` pattern verbatim (`get_ptr<const nlohmann::json::string_t*>()`, noexcept reader). `std::formatter<Enum> : std::formatter<std::string_view>` uses a **templated format context** (libstdc++ 15 evaluates formattability against a substituted context — same note as `ErrorCode`/`LogLevel`).

Public classes/structs get member `to_string() const` + free `to_string(const T&)` + `std::formatter` delegating to `to_string()`. Value types (`SpanContext`, `SpanAttribute`, `SpanRecord`, configs, `Dropped_stats`) additionally get full `to_json`/`from_json`; factory- or lifetime-bound types (`ScopedSpan`, `Tracer`, `TracerProvider`, live sinks, composite sinks, null objects) get `to_json` and deliberately **no** `from_json`, pinned by negative `static_assert`s in the test TU.

### D5 — Exact memory layout (mandated)

**`SpanAttribute` — 48 bytes, trivially copyable, `alignas(8)`:**

```
off  0   char          key[16]        inline bounded key (≤ 15 chars + NUL)
off 16   SpanAttributeType type      1 B tag + 7 B padding
off 24   union {                     24 B, 8-aligned
             std::int64_t  i64;
             std::uint64_t u64;
             double        f64;
             bool          b;
             struct { char text[23]; std::uint8_t length; } inline_text; // 24 B
             struct { const char* data; std::uint32_t length; } view;     // 12 B
         } value
sizeof(SpanAttribute) == 48, pinned by static_assert
```

String values ≤ 23 bytes are copied into `inline_text` (always safe); longer values are stored as `view` — a zero-copy `string_view` over storage the caller guarantees outlives the span's drain (literals/program-static tables; same lifetime-contract class as `LogRecord`'s `fmt` pointer). The key never exceeds 15 characters (longer keys are rejected whole, never truncated).

**`SpanRecord` — 320 bytes = 5 cache lines, trivially copyable, `alignas(64)`:**

```
off   0   TraceId        trace_id            8 B (strong std::uint64_t)
off   8   SpanId         span_id             8 B
off  16   SpanId         parent_span_id      8 B (zero = root)
off  24   SpanKind       kind                1 B
off  25   SpanStatus     status              1 B (default Unset)
off  26   std::uint8_t  flags               1 B (bit0: ended via explicit end())
off  27   std::uint8_t  attribute_count     1 B (≤ MaxAttributes = 4)
off  28   std::uint32_t thread_id           4 B (cached SYS_gettid)
off  32   chrono::Timestamp start            8 B (monotonic ns)
off  40   chrono::Timestamp end              8 B (monotonic ns)
off  48   const char*    file_name           8 B (std::source_location static storage)
off  56   const char*    function_name       8 B (static storage)
off  64   std::uint32_t  line                 4 B
off  68   std::uint32_t  column               4 B
off  72   char           name[48]            48 B span name (≤ 47 chars + NUL)
off 120   SpanAttribute  attributes[MaxAttributes = 4]   4 × 48 = 192 B
off 312   (8 B tail padding to alignas(64))
sizeof(SpanRecord) == 320 && alignof(SpanRecord) == 64 && trivially_copyable,
all pinned by static_assert
```

**`TracerProvider<ShardCapacity = 1024, Max_threads = 32>` — all storage inline in the object (one allocation when the provider is created):**

```
alignas(64) core::SpscRingBuffer<SpanRecord, ShardCapacity> shards_[Max_threads]
std::array<chrono::Stamp_source, Max_threads> stamps_          // 64 B each, single-writer per slot
alignas(64) Shard_state state_[Max_threads]                    // owner-written drop/throttle cell, 64 B
alignas(64) Registration_cell registration_                    // next_slot + active, 64 B
alignas(64) Exhausted_cell exhausted_                           // slot_exhausted counter, 64 B
alignas(64) Control_cell control_                               // enabled + sampling byte, 64 B
alignas(64) Start_cell start_                                   // worker launch/pin snapshot, 64 B
alignas(64) Flush_cell flush_                                   // drain-barrier handshake, 64 B
TracerSlot tracers_[config.max_tracers] + Tracer arena
```

The provider class itself is `alignas(64)`; because `alignof(TracerProvider) == 64 > __STDCPP_DEFAULT_NEW_ALIGNMENT__`, plain `new`/`make_unique` already goes through C++17 aligned allocation — every member lands cache-aligned without a bespoke arena. Documented construction rule: the object is ~10.5 MiB with defaults, so it lives in static storage or on the heap, never on the stack (pinned by a `static_assert`-style comment + README/test discipline). Default memory budget: shards 32 × 1024 × 320 B = **10 MiB**, stamps 32 × 64 B = 2 KiB, shard states 2 KiB, tracer pool 16 × ~128 B ≈ 2 KiB; `ShardCapacity` and `Max_threads` scale the shard block linearly. Zero allocation happens after construction returns.

### D6 — Identifier generation: non-zero thread-local xorshift

`detail::id_generator` keeps a `thread_local std::uint64_t state_`, seeded once per thread on first use via splitmix64 over an **injective `(tid, counter)` pairing** — the cached thread id shifted into the high 32 bits, the process-wide counter's relaxed `fetch_add(1, relaxed)` value masked into the low 32 bits, then XOR `0x9E3779B97F4A7C15` — and forced nonzero (`| 1`; splitmix64 is a bijection, so disjoint seed inputs stay disjoint seeds). Generation is one xorshift64* round (~1 ns, register work only) — no lock, no allocation, no cross-thread atomic; distinctness across threads follows from disjoint seeds. *Implementation note (correction of the original draft): the first pass combined the inputs as `tid ^ counter ^ const`, which collides whenever `tid1 ^ tid2 == counter1 ^ counter2` — with consecutive TIDs and small counters that happens constantly (16k duplicate ids reproduced in a 32-thread stress run). The packed pairing removes that collision class — distinct `(tid, counter)` pairs can no longer merge — while keeping every D6 property: wait-free generation, one relaxed fetch_add per seeding on the cold path only, forced-nonzero outputs, and disjoint cross-thread seeds. Shipped behavior: see `detail/id_generator.hpp` `seed_input()`.* `TraceId` and `SpanId` are strong trivially copyable wrappers over `std::uint64_t` (default = zero sentinel "unset", explicit constructor, defaulted `<=>`); both render as zero-padded 16-character lowercase hex (`std::to_chars`, base 16) in `to_string`/JSON. The generator is the *only* id source; `TraceId` is generated at root-span start and inherited otherwise.

### D7 — Parent tracking: fixed-depth thread-local stack with index truncation

`detail::active_span_stack` is a `thread_local` fixed array of `Active_frame { TraceId trace_id; SpanId span_id; }` with `max_span_depth = 16` and a plain `std::uint8_t depth_`. A **sampled** span pushes its own frame and remembers its index; an explicit-parent child pushes *its own* (child) frame, so subsequent same-thread spans link to it. On end (or destruction), the stack truncates to that index — removing the frame and any frames above it. This makes explicit `end()` while children are still active well-defined (their frames are discarded early; their later truncation clamps to a no-op) instead of corrupting LIFO. Over-deep spans (depth == 16) record as bounded roots — fresh `TraceId`, zero parent, no push — and dropped/disabled spans never touch the stack at all, which is what keeps the short-circuit path at one atomic load.

### D8 — ScopedSpan and the ≤ 20 ns hot path

`ScopedSpan` is non-copyable, non-movable, non-default-constructible (constructed only by `Tracer` through `SpanPasskey`, like `MeterPasskey`); returned by value from `start_span` under C++17 guaranteed elision. Members: `SpanRecord record_` (filled inline), `TracerProvider<...>* owner_` (null = inactive/dropped guard), `std::int8_t slot_` (shard index), `std::uint8_t stack_index_`, `bool ended_`. The provider pointer carries everything `end()` needs; `finish_span` is an inline provider method so the destructor has no indirect call.

Hot path in order (each step's cost accounted against the 20 ns budget):

1. **control short-circuit**: one relaxed load of `control_.state` (bit0 enabled, bits 1-2 = `SamplingDecision`); disabled or `Drop` → return an inactive guard (`owner_ = nullptr`) — no ids, no stamp, no stack. ≈ 1 ns.
2. **shard slot**: `thread_local` 4-entry map keyed by provider address (logging `Producer` pattern); first call per (thread, provider) claims a slot with one relaxed `fetch_add` and publishes the high-water mark with a release `fetch_max` CAS loop (cold). Steady state: TLS only. ≈ 1 ns.
3. **thread id**: `thread_local` cached `SYS_gettid` (syscall once per thread). ≈ 0 ns steady.
4. **ids + parent**: stack top lookup (`TraceId` inherit or generate, `SpanId` generate xorshift, explicit parent override). ≈ 2 ns.
5. **start stamp**: `stamps_[slot].sample()` — per-shard single-writer `chrono::Stamp_source` (calibrated TSC, one `__rdtsc` + fixed-point multiply; the reason a `clock_gettime`-per-span design cannot fit the budget). ≈ 5-7 ns.
6. **record fill**: fixed-offset stores into `record_` (name `memcpy` ≤ 47, `source_location` static pointers, kind, tid; `attribute_count = 0`, `status = Unset`). ≈ 3 ns.
7. **stack push**: one TLS store + index. ≈ 1 ns.

`set_attribute`/`set_status`: branch on `owner_`/`ended_`, then fixed-offset store into `record_.attributes_[n]` / status byte; `std::uint8_t` count bump. No allocation, no atomic, no lock.

`end()`/destruction (once; `end()` is idempotent and the destructor checks `ended_`):

1. branch on `owner_`/`ended_` ≈ 0 ns; 2. end stamp via the same `Stamp_source` ≈ 5-7 ns; 3. `shards_[slot].try_push(record_)` — the 320 B copy plus one release store (no CAS; full ring → owner-side `buffer_full` bump, never blocking) ≈ 4-5 ns; 4. stack truncation ≈ 1 ns.

Two TSC samples dominate the budget; the Release gate (D15) asserts the median with headroom over the same warm-loop methodology logging/metrics use.

### D9 — Runtime control cell

One `alignas(64)` cell, `std::atomic<std::uint8_t> state_` encoding bit0 `enabled`, bits 1-2 `SamplingDecision`, remaining bits reserved. Span start performs exactly one **relaxed** load plus shifts/compares. `set_enabled`/`set_sampling` compose the byte and **relaxed**-store it (no data is published alongside — atomicity is the only requirement; visibility is guaranteed for subsequent loads). This is the tracing analogue of `logging::detail::Level_control`.

### D10 — Shard pool and consumer thread

The consumer is one `std::jthread` owned by the provider (declared **last** among members so it joins before the pool/sink die — logging `Async_worker` order). Loop body: self-pin via `detail::pin_self(core_mask)` (mask ≠ 0; failures captured into `start_` cell, never stopping the pipeline); round-robin drain across the `active` high-water mark with a 16-record-per-shard per-sweep slice (one flooded shard cannot starve others, logging `Producer::pop` pattern); each record delivered through a type-erased `Sink_binding { void* self; on_span/flush fn pointers }` created by the templated constructor (metrics D13 pattern — no virtuals, no RTTI); idle backoff after 64 failed sweeps → 200 µs sleeps (bounded wake latency, no busy core burn). The sink must outlive the provider (documented contract).

`flush()` is the logging-style control-plane barrier: `requested.fetch_add(1, release)` by the caller (the release edge publishes everything it enqueued before asking), the worker publishes `drained` with a release store after sink flush, the caller acquire-loads with sleep + bounded escape hatch. Destruction: request stop, join (final drain of every shard), one final sink flush + stop — no accepted record is lost.

### D11 — Sinks

- `NullTraceSink`: empty inline bodies; `on_span` discards without reading.
- `ConsoleTraceSink { int fd = 1; }`: one human-readable line per span through fixed stack buffers + `std::to_chars` (no iostreams, no per-record allocation), `::write` with `EINTR`/partial-write retry; failures → `SystemError` without abort; `flush` is a no-op success.
- `JsonFileTraceSink` + `JsonFileTraceSink_config { std::string path; RotationPolicy rotation; std::uint64_t max_spans_per_window; std::uint64_t window_ns; }`: one JSON object per span per line via the tracing `detail::Fixed_writer` (logging's escaping policy); UTC ISO-8601 stamps through `chrono::format_utc_iso8601_ns` over a per-window sampled UTC offset; hourly/daily rotation computed from the incoming span's UTC window (file closed and reopened as `<prefix>.<YYYYMMDDTHH>` / `<prefix>.<YYYYMMDD>`, `None` keeps one file); output rate limiting is consumer-thread plain state (single consumer — no atomics needed): budget per window, drops counted, and a drop-summary JSON line emitted at the window boundary before further records; `flush` = `::fsync` surfacing `SystemError`.
- `ChromeTraceSink` + `ChromeTraceSink_config { std::string path; }`: writes `{"traceEvents":[` at start, one complete-event object per span per line with commas, `]}` at stop. Event: `"name"` (span name), `"cat"` (span kind canonical name — the record deliberately carries no tracer scope, per the proposal's pinned field list), `"ph":"X"`, `"ts"`/`"dur"` integer microseconds (`(start − base) / 1000`, `(end − start) / 1000`; `base` is the first drained span's start, consumer-thread plain state), `"pid"` (cached `SYS_getpid`), `"tid"` (record thread id), `"args"` carrying `trace_id`/`span_id`/`parent_span_id` as 16-char hex plus typed attribute values. The whole file parses as one JSON object; Perfetto (`ui.perfetto.dev`) renders the timeline.
- `CompositeTraceSink<Sinks...>`: `std::tuple<Sinks...>` fan-out without short-circuit (first error wins after all members attempted), aggregated `flush`, `get<Index>()`/`get<SinkType>()` with compile-time guards — metrics D13 pattern verbatim.

### D12 — Tracer registry

`get_tracer(scope_name)` scans the pre-allocated tracer pool under a `std::mutex` (control plane — mutexes are banned on hot paths only); repeat name → the same handle; new name → claim a slot (`BufferFull` when exhausted), placement-construct a `Tracer { provider*, name ptr into slot storage }`. `Tracer::start_span(name, kind, const SpanContext* parent = nullptr, std::source_location loc = current())` is inline and performs the D8 pipeline. `SpanPasskey` (private ctor, `friend class Tracer`) pins factory-only construction; `static_assert(!std::is_constructible_v<...>)` pins live in the test TU.

### D13 — Memory-order summary

| Component | Operation | Order | Why |
|---|---|---|---|
| control cell `state_` | load (span start) | `relaxed` | no data published alongside; atomicity suffices |
| control cell `state_` | store (set_enabled/set_sampling) | `relaxed` | subsequent loads see it; no ordering payload |
| registration `next_slot` | `fetch_add` (first call per thread) | `relaxed` | unique slot ids only |
| registration `active` | `fetch_max` (cold CAS loop) / consumer load | `release` / `acquire` | publish slot existence before the drain loop may observe it |
| SPSC `tail`/`head` | store / load (inside `core::SpscRingBuffer`) | `release` / `acquire` | publish the 320 B slot write / observe it before pop |
| shard `buffer_full` / `rate_limited` | owner store / aggregate load | `relaxed` | third-thread snapshot diagnostics, never synchronization |
| `slot_exhausted` | `fetch_add` | `relaxed` | misconfiguration diagnostics only |
| `start_` cell (launched/started/code/mask) | store / load | `release` / `acquire` | one-shot worker startup publication |
| flush handshake `requested`/`drained` | `fetch_add` / store / load | `release` / `release` / `acquire` | caller publishes enqueued records before requesting; observer sees durability |
| liveness flag | store (dtor) / load (handle use) | `release` / `acquire` | handle-after-death detection (`NotInitialized`) |
| id generator process counter | `fetch_add` (seed mix) | `relaxed` | distinct seeds only |
| drop/throttle window fields, `base_`, sink state | plain | — | single-writer (owner thread / consumer thread) by construction |

### D14 — Tests and latency gates

Single TU `tests/tracing/tracing_test.cpp` on the project harness (copy `NX_CHECK`/`run_suite`, the arena `operator new/delete` counter, the `mallinfo2` delta counter, and `NX_SANITIZER_BUILD` detection). Suites mapped 1:1 to the spec deltas: `span_enums` (trio round-trips, hostile JSON, width/align), `ids_and_context` (nonzero/distinct/wait-free, cross-thread handoff equality, hex round-trips), `attributes` (every value type, capacity bound, key bound, inline vs view strings), `span_record` (compile-time pins, lossless copies, JSON round-trip), `parent_hierarchy` (nested/sibling/explicit-override/root/overflow-truncation), `scoped_span` (exactly-once enqueue, end idempotence, post-end rejections, full-shard drop, allocation-free windows), `cross_thread` (context handoff through a queue, child linkage, local re-parenting), `concepts` (positive `static_assert`s for `Tracer`/`TraceSink` over every implementation, negative probes, null-type zero-work timing vs an empty baseline), `sinks` (pipe/temp-file fixtures: console content, JSONL parse-per-line, ISO-8601 shape, escaping, rotation filenames, rate-limit drop summary, Chrome object parse + first-event `ts == 0` + field coverage, composite fan-out/no-starvation, error paths), `provider` (get_tracer dedup/`BufferFull`, 32-thread exclusive shards + 33rd-thread `slot_exhausted`, `pthread_getaffinity_np` mask round-trip, invalid-mask `SystemError` startup snapshot, control flips under concurrency, `dropped()` per-cause counts, sampling ≠ drops, shutdown drain, flush barrier, dead-handle `NotInitialized`), `serialization` (member/free agreement, `std::format` equality, negative `from_json` pins).

Release-only gates via `NEXIGEN_TRACING_STRICT_LATENCY` (same config gating as logging/metrics): full span lifecycle (`start_span` + destruction enqueue) median ≤ 20 ns; disabled/dropped `start_span` + destruction median ≤ 2 ns; zero-allocation windows on both paths (Debug/sanitizer builds run correctness only). Latency methodology: warm batched loops, median per-op, `chrono::Stamp_source` for measurement — identical to the logging/metrics gates.

### D15 — Examples, CMake, and README

`examples/tracing/CMakeLists.txt` registers `ex_nested_and_cross_thread_spans` and `ex_perfetto_chrome_trace` through the existing `nexigen_add_example` function (link `nexigen_tracing Threads::Threads`, `TIMEOUT 600`, `LABELS examples`); `examples/CMakeLists.txt` gains `add_subdirectory(tracing)`. `tests/CMakeLists.txt` registers `nexigen_tracing_tests` with the Release-only latency define. The README's Runnable Examples section grows from eight to ten entries with one-line descriptions each, and the build/run list adds the two `examples/tracing/` paths. The first example prints the drained timeline and self-verifies every parent-child edge (non-zero exit on mismatch); the second writes the Chrome trace, prints its path plus the `ui.perfetto.dev` instructions, and exits non-zero if the file is missing or malformed.

## Risks / Trade-offs

- [Two TSC samples consume ~10-14 ns of the 20 ns budget] → per-shard single-writer `Stamp_source` (D8) keeps samples at one `__rdtsc` + fixed-point multiply; gates run as medians over warm loops with Release-only compilation (D14); on non-x86 targets the budget degrades to the platform's — documented, matching logging's stance.
- [320 B × 1024 × 32 = 10 MiB pre-allocated footprint] → the whole pool is one inline block scaled linearly by the `ShardCapacity`/`Max_threads` template parameters; the budget table (D5) lets integrators size for their platform; the provider must not be stack-allocated (documented + `alignof`-guaranteed aligned heap placement).
- [Long `string_view` attribute values dangle if callers violate the static-storage contract] → values ≤ 23 bytes always copy inline; longer views follow the same documented lifetime class as `LogRecord`'s `fmt` literal pointers (D5), and the contract is stated on `set_attribute` itself.
- [Explicit `end()` while children are active would corrupt a naive LIFO pop] → stack-index truncation semantics (D7): each frame remembers its index and end truncates to it; children's later truncations clamp to no-ops; covered by dedicated tests.
- [Threads beyond `Max_threads` drop instead of sharing a fallback cell] → chosen deliberately for SPSC exclusivity (logging model, not the metrics fallback model): drops are counted in `slot_exhausted` and the capacity is documented; real-time thread sets are stable.
- [Chrome output truncates timestamps to integer microseconds] → the JSONL sink retains full nanosecond stamps; the Chrome `dur` derives from the ns record fields so the loss is display-resolution only, documented (D11).
- [Rate limiting and rotation depend on UTC offset sampling] → the offset is sampled once per window transition on the consumer thread (cold), mirroring logging's file rotation; a failed sample falls back to the previous offset (stale-but-valid).
- [Gate flake on loaded CI machines] → medians over batched warm-up loops, Release-only compilation, 20 ns target with the same methodology that keeps the logging/metrics gates stable.
- [TSan on the thread-local stack and owner-written counters] → the stack is per-thread by construction; shared counters are atomics with explicit relaxed ordering (D13); the full TSan/ASan matrix gate is a first-class task.

## Migration Plan

Additive only: a new library target, tests, examples, and two README lines; no existing API or spec requirement changes except the `examples` catalogue text (8 → 10). Rollback is removing the `nexigen_tracing` target and the two example entries — nothing else depends on tracing.

## Open Questions

None — all decisions above are pinned; anything that would change the specs, approach, or task breakdown is resolved here.
