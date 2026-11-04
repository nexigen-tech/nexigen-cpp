# Proposal

## Why

The codebase-analysis pass (Nov 2026) identified three copy-groups of sink plumbing and one CLI copy-group, ~630 duplicated lines total. The project already established the pattern of collapsing such copies into `nexigen::core` plumbing (`core::fixed_writer.hpp` — "one implementation instead of three near-identical copies", `core::settings_io.hpp` — "three near-identical copy groups collapse into one"; TODO P3-28 removed 59 serialization delegates). This change applies the same treatment to the remaining copies found:

1. **Composite fan-out scaffolding ×3** — `composite_chrono_sink.hpp` / `composite_metric_sink.hpp` / `composite_trace_sink.hpp` contain a structurally identical variadic pack (tuple storage, `find_index`/`exactly_one`, `get<I>`/`get<T>`, `for_each`, `combine`, `size`; ~100 lines per header, ~40-line matching windows per pair).
2. **File-sink fd plumbing ×4** — `json_file_metric_sink.hpp`, `json_file_trace_sink.hpp`, `chrome_trace_sink.hpp`, `json_file_chrono_sink.hpp` each carry a private `::write` loop with EINTR/partial-write handling (and their own `::open` flag sets, plus a duplicated `fsync` flush in metric/chrono).
3. **Settings helper remnants ×3** — `chrono_settings.cpp` still keeps private copies of the five helpers that metrics/tracing already delegate to `core::settings_io.hpp`, and all three loaders define identical `read_uint` / `sink_field` / `find_sink` / `read_sink_string` / `read_sink_bool` helpers (~85 lines chrono-only remainder, ~45 lines per loader after that).
4. **`nexigen-run` CLI report tails** — `cmd_start` / `cmd_stop` / `cmd_status` (`tools/nexigen-run/main.cpp:176–631`) repeat the same logging+stderr/stdout report blocks (~12 call sites) and the same `--config`/`--suite-dir` mutual-exclusivity validation.

Why now: the copies have already started diverging (`read_sink_bool` vs `read_sink_uint` naming, bool vs `core::Status` write-loop returns), so the risk of a partial fix being applied to only one copy grows with every edit.

## What Changes

- Add `nexigen::core::sink_pack` (`include/nexigen/core/sink_pack.hpp`): variadic, virtual-free `Fanout<Sinks...>` scaffolding — tuple member storage, compile-time `find_index`/`exactly_one`, index- and type-based `get`, ordered `for_each`, first-failure `combine` (`core::Status`), static `size`, default-constructibility-constrained ctor.
- Add `nexigen::core::sink_io` (`include/nexigen/core/sink_io.hpp`): shared cold-path file-write plumbing — `open_json_sink(path, append)` (O_WRONLY | O_CREAT | O_CLOEXEC, append ? O_APPEND : O_TRUNC, 0644), `write_all(fd, data, bytes)` with the EINTR/partial-write retry loop returning `core::Status`, `fsync_fd(fd)`.
- Extend `nexigen::core::settings_io` with the loader trio helpers (`read_uint`, `sink_field`, `find_sink`, `read_sink_string`, `read_sink_bool`, `read_sink_uint`) keeping byte-identical diagnostic messages.
- Migrate the three composite sink headers to inherit `sink_pack::Fanout<Sinks...>`; each header keeps only its payload method (`on_snapshot` / `on_batch` / per-module name), header `to_json` line, and its own `concepts::*` static_assert.
- Migrate the four file-backed sinks to `sink_io` (delete their private write loops / open flag literals / duplicate flush); metric and chrono `write_all` call-sites map the shared `Status` back to their existing `SystemError` reporting — externally observable behavior is byte-identical.
- Migrate `src/chrono/chrono_settings.cpp` to `core::settings_io` (delete its five private copies) and slim the metrics/tracing loaders by the hoisted helpers.
- Add file-local report helpers in `tools/nexigen-run/main.cpp` (`cli_fail` / `cli_ok`, shared suite-option validation) and rewire the ~12 report call sites in `cmd_start`/`cmd_stop`/`cmd_status`.
- Deferred on purpose (follow-up changes): `logging/composite_logger.hpp` migration (stateful `set_level` tree — separate change), `HttpServer::Impl` split, `run_instance_supervisor` decomposition, affinity.cpp unification (documented module-isolation trade-off).

## Capabilities

### New Capabilities
- `core/sink-plumbing`: the shared, exception-free fan-out and file-write contracts that observability sinks must go through — variadic `Fanout` member access/iteration/first-failure semantics, and the shared open/write/fsync fd plumbing with its EINTR/partial-write retry and flag parity. Internal plumbing, not hot-path API-by-usage: mirrors the `core::fixed_writer.hpp` placement rationale.

### Modified Capabilities
<!-- none — every touched module keeps its existing specified behavior; the
     change adds implementation-level sharing only. Verified against
     specs/chrono/chrono-settings, metrics/metric-sinks, tracing/trace-sinks,
     runner/process-supervision, runner/logging: no requirement text changes.
-->

## Impact

- **Code**: `include/nexigen/core/{sink_pack,sink_io}.hpp` (new), `include/nexigen/core/settings_io.hpp` (+~45 loc), the three composite headers (−~110 loc each), the four file-sink headers (−~50 loc total), `src/{chrono,metrics,tracing}/*_settings.cpp` (−~175 loc), `tools/nexigen-run/main.cpp` (−~120 loc). Net ≈ −570 lines.
- **API**: no breaking changes. `CompositeMetricSink` / `CompositeTraceSink` / `CompositeChronoSink` keep their `get<I>`/`get<T>`/`size` surface and constructors (tests/examples call them unchanged); file sinks keep their public constructors, `flush()`, `to_string`, `to_json`; CLI output and `runner.jsonl` events are byte-identical.
- **Performance / latency / allocation impact**: zero on hot paths. `sink_pack::Fanout` is compile-time only (same tuple `std::apply` codegen); `sink_io` functions execute only in cold paths (sink construction, rotation, flush). `sink_io::open` takes `std::string_view` and does one `std::string` copy per open — identical to the copies metric/chrono already do today, and rotation-frequency cold for the trace sink. No new heap allocations in any record/batch/snapshot write path (JSON still stages through `core::FixedWriter`, no `nlohmann::json` on hot paths).
- **Tests**: no existing test changes expected (behavior identical); new unit scenarios for `Fanout` and `sink_io` extend `tests/core/core_test.cpp`; full Debug / Release / TSan / ASan / coverage matrix re-run per project rules.
- **Docs**: `README.md` architecture section and Doxygen gain the two new core headers via the existing header-glob; no behavioral documentation updates required.
