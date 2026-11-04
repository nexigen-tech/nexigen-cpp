# Tasks

## 1. Build + output fixes (P2-16, P2-17)

- [x] 1.1 In `cmake/Optimizations.cmake` (design D1): replace the target-level
  INTERPROCEDURAL_OPTIMIZATION on `nexigen_options` with
  `set_property(GLOBAL PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)`
  after `check_ipo_supported`; keep the honest status message. Verify:
  Release build Links with lto plugins (`readelf` string table or build log
  `-flto` flags in compile_commands.json for real targets); Debug unaffected.
- [x] 1.2 In `src/chrono/chrono_provider.cpp`, `src/metrics/snapshot_format.cpp`,
  `src/tracing/record_format.cpp`: render
  `core_mask=0x<std::format("{:x}", ...)>` (hex digits, no leading
  formatting bug: mask 10 → `0xa`). Verify: unit checks for the three
  to_string renders (values 0, 10, 0xFFFFFFFF00000000) show `0xa` / shifted
  value — add small asserts where a suite already builds config to_string.

## 2. Sink overflow observability (P2-18; spec deltas metric-sinks + trace-sinks)

- [x] 2.1 In `include/nexigen/metrics/json_file_metric_sink.hpp`: add
  `std::size_t discarded_batches_` (plain member; the sink is
  consumer-thread-confined), incremented once when `writer.ok()` is false
  before the `BufferFull` return; extend `to_string()` with
  `discarded_batches=`. Verify: unit test — a batch over the scratch
  (large names) returns `BufferFull`, increments exactly once, writes no
  partial file bytes; `to_string()()` shows the count.
- [x] 2.2 In `src/tracing/sink_format.cpp` + the trace sink's state (find the
  owner object): same pattern for discarded span lines (`discarded_lines=` in
  `JsonFileTraceSink::to_string`). Verify: tracing test with an attribute
  payload over 8 KiB scratch — line discarded, counter +1, no partial write.

## 3. Gauge/StateGauge honesty (P2-20)

- [x] 3.1 In `include/nexigen/metrics/gauge.hpp` (design D4): replace the
  check-then-act `set` in `GaugeStorage<MultiThreaded,...>` with shard-0
  assignment + zeroing the other shards and the fallback (a new
  `assign(ValueType)` primitive on the sharded scalar). Verify: metrics test
  races set against add (2 writers, 2 madders) — totals converge into
  `{target + adds-after}` with no lost-add panic, TSan-clean.
- [x] 3.2 In `include/nexigen/metrics/state_gauge.hpp` (+ state_cpp): header
  comment stops claiming wait-free readers ("seqlock: readers retry a torn
  window (bounded spin), never observe torn bytes"); `read()` docs state the
  thread_local buffer lifetime (valid until the next `read()` on this
  thread). Verify: wording review + existing suite stays green.

## 4. LoggerOptions honesty (P2-19; spec delta logger-options)

- [x] 4.1 Remove the three dead quota fields
  (`repeat_suppression_max_count`, `max_bytes_per_sec_per_shard`,
  `max_total_bytes_per_sec`) from `SpamProtectionConfig`
  (`logger_options.hpp`), its `to_string`, `to_json`/`from_json`, and the
  `logging_settings.cpp` keys (loader diagnostics for legacy keys remain by
  the generic unknown-key path). Verify: logger-options/logging-settings
  suites updated and green; runner generated logging.json writers drop the
  keys (renderer from options serialization — checked by runner tests).
- [x] 4.2 Add `flush_cpu_affinity` to `LoggerOptions::to_string`. Verify:
  logger-options test asserts the field name appears.

## 5. Composite parity (P2-22)

- [x] 5.1 In `include/nexigen/logging/composite_logger.hpp` `flush()`:
  return the FIRST member error (fold keeps flushing everyone; first error
  wins). Verify: logging test drives a two-sink composite where the first
  member errors — flush returns that error, the second member still flushed.

## 6. App/runner hardening (P2-23)

- [x] 6.1 In `include/nexigen/app/infrastructure.hpp`: `SignalWatcher::start`
  returns `Conflict` when `active()`/thread joinable and installs nothing;
  fold the ×4 `fail(...)` emplace idiom into one helper. Verify: app test
  double-start → `Conflict`, no fd growth (`/proc/self/fd` count after two
  calls), and existing app suite green.
- [x] 6.2 In `src/runner/runner.cpp` + `src/runner/app_suite.cpp` child
  blocks: ignore SIGPIPE around the pre-exec handshake write
  (`save = signal(SIGPIPE, SIG_IGN)` → write → restore), both launch paths.
  Verify: build + runner suites green (no BUS/PIPE crashes on early-closed
  pipe; the window is timing-guarded in the exec-failure tests).

## 7. Shared detail unification (P3-24, P3-25)

- [x] 7.1 Create `include/nexigen/core/detail/fixed_writer.hpp` (design D7
  superset) and alias the three module `detail/json_writer.hpp` headers to
  it. Verify: full build green; grep shows no duplicated writer bodies left
  in the module headers.
- [x] 7.2 Create `include/nexigen/core/detail/settings_io.hpp` with
  `read_settings_file` / `parse_u64` / `settings_add_diagnostic`; the three
  module settings units (logging/metrics/tracing) drop their local copies
  and include the shared header. Verify: settings suites green; diff shows
  one definition each.

## 8. Repo + verification matrix

- [x] 8.1 `.gitignore`: add `build-*/` (P3-26) plus the `.nexigen-run/`
  runtime residue. Verified: `git check-ignore` matches the existing build
  trees; untracked leftovers reduced to real repo artifacts.
- [x] 8.2 Full Debug + Release (`-DNEXIGEN_BUILD_TESTS=ON`) suites green
  (100%); targeted suites compile clean under Clang `-Werror`. Verified:
  Debug 35/35; Release 34/35 with the single failure being the
  strict-latency gate (environment-bound, ~130 ns on the pristine HEAD
  baseline too — see the P1 change's 8.1 note; CI disables this gate);
  Release regenerated with the REAL LTO wiring now (105 `-flto` rules in
  build.ninja; a dedicated fresh LTO build also links and runs 14/15 with
  the same environmental-gate failure, all other suites green); Clang 21
  `-Werror` targeted builds clean (one -Wunused-lambda-capture in
  emplace_checked caught by Clang and fixed).
- [x] 8.3 TSan build (logging/metrics/tracing/app/runner/core) — zero race
  reports across all six suites, including the new gauge set-vs-add race
  probe.
- [x] 8.4 ASan build full suite green.
- [x] 8.5 `openspec validate --strict --changes` clean for this change.
