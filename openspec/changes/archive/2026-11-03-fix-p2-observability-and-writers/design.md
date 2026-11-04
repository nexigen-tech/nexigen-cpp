# Design

## Context

Eight small fixes from the analysis (P2-16…P2-23 minus resolved items,
P3-24/25/26) plus one re-verified correction of the analysis itself
(P2-19): `max_producer_threads` / `spsc_ring_buffer_capacity` are NOT
unwired — `logging::check_capacity` guards them against the compile-time
geometry in both `FileLogger::initialize` and `ConsoleLogger::initialize`
(`file_logger.hpp:378`, `console_logger.hpp:208`). Only the three
byte/quota fields (`repeat_suppression_max_count`,
`max_bytes_per_sec_per_shard`, `max_total_bytes_per_sec`) are dead: they
are stored into `SpamProtection` atomics by my P1 field-atomic work and
read nowhere (the hot path checks `enable_repeat_suppression`,
`repeat_suppression_window_ms`, `max_messages_per_sec_per_shard`,
`bypass_min_level` only).

The remaining confirmed defects: LTO set on the INTERFACE
`nexigen_options` target that compiles nothing (`Optimizations.cmake:35–42`);
`core_mask=0x<decimal>` renders in three `to_string`s; sink JSON-scratch
overflow discards whole batches/lines with no counter
(`json_file_metric_sink.hpp:192–195`, `sink_format.cpp:207–211`);
`Gauge<MultiThreaded>::set` computes a delta from `observe()` then adds it
(check-then-act — concurrent adds between the two steps survive and skew
the result); `StateGauge` header/docs say wait-free readers while the
reader is a seqlock retry, and `read()` returns a string_view into a
thread_local buffer without lifetime words; `CompositeLogger::flush`
returns the LAST member error while the trace-composite contract (and
spec trade-sinks:102) promises the FIRST; `SignalWatcher::start` has no
double-start guard although `app/application` already requires `Conflict`
(`app/application` req scenario "the second call returns Conflict");
`Infrastructure::initialize` repeats the same four-line
`fail(status ? InvalidArgument : status)` idiom four times; the runner's
pre-exec handshake writes are the only IPC writes without SIGPIPE
shielding (runner.cpp:617–620 and the suite child equivalent); three JSON
`FixedWriter`s and three settings-io helper groups are near-identical
copies; `.gitignore` lacks `build-*/`.

Constraints inherited: C++23, `-fno-exceptions/-fno-rtti`, `-Werror`
GCC/Clang, zero hot-path interactions (no allocation, no locks).

## Goals / Non-Goals

**Goals:** every loss visible, every claim honest, every copy unified
where duplication breeds drift, build property real; the P3-24/25
dedup needed to land before any further sink/format work.

**Non-Goals:** wiring runtime geometry into fixed template engines
(kept — verified wired through validation); CRTP serialization generator
(P3-28, deferred); hub dashboard (separate follow-up change); changing
`CompositeTraceSink` (already contract-true).

## Decisions

### D1 — LTO via `INTERPROCEDURAL_OPTIMIZATION_<CONFIG>`

`check_ipo_supported` stays in `nexigen_set_optimizations`; on success
`set_property(GLOBAL PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)`
applies LTO to every compiled target in Release. Removes the no-op
target-level property on `nexigen_options`; the "🚀 LTO" message becomes
honest.

### D2 — `core_mask` renders as `{:#x}`

The three `to_string`s use `std::format("0x{:x}", mask)`: mask `10` renders
`0xa`, not `0x10`. Touches `ChronoProvider::to_string`,
`MetricProviderConfig::to_string`, `TracerProviderConfig::to_string`. The
logger-options/--settings spec scenarios stay declarative (no pinned
format text found) — implementation-only per grep.

### D3 — Scratch-overflow loss counters in the JSON-line sinks

Both sinks keep returning `BufferFull` (today's contract), and gain a
private `dropped_batches_` / `dropped_lines_` counter incremented exactly
when the writer reports overflow, visible in `to_string()`:
`discarded_batches=` / `discarded_lines=`. The metrics harvester flow
already treats a BatchFull batch end as synchronous; the counter makes
losses auditable. New scenario in both delta specs — no new types, no
serialization surface.

### D4 — `Gauge<MultiThreaded>::set` assigns, not adds

Replace `delta = value − observe(); add(delta)` with shard reassignment:
shard 0 `store(value)`, all other shards + the fallback `store(0)`.
Multiple racing sets produce last-writer-wins per shard line (control
plane, bounded), racing adds land either before or after and are folded
per-shard; no lost check-then-act delta, no CAS loop, no hot-path change
(`add`/`sub` untouched with their relaxed fetch_add). `StateGauge`
semantics text stops claiming wait-free readers — the honest story is
"seqlock: readers retry a torn window (bounded spin), never observe torn
bytes", and `read()` documents the thread_local buffer lifetime (valid
until the next `read()` on this thread). The counter-gauge requirement
body ("set moving the aggregate to the target value as observed at call
time") stays satisfied — reassignment is the strongest reading of it.

### D5 — Composite parity: `CompositeLogger::flush` returns the first error

Members flush in declaration order, collecting the first non-OK status;
later errors are swallowed but the flush still reaches everyone
(no short-circuiting). Matches `CompositeTraceSink` (already
spec-contract "first error surfacing"). The composite-logger spec does
not pin flush-error selection anywhere — implementation parity only.

### D6 — `SignalWatcher::start` guards double start

`start` returns `Conflict` when `active()` (one signalfd exists) or the
jthread is joinable — installs nothing. `Infrastructure::initialize`
gets a small `emplace_checked` helper (folds the ×4 fail-idioma). The
runner's pre-exec handshake write and the suite child's equivalent wrap
`signal(SIGPIPE, SIG_IGN)` for the write window only (handlers reset on
`exec`, so the launched app keeps its own disposition).

### D7 — One `FixedWriter`, one settings-io helper

New `include/nexigen/core/detail/fixed_writer.hpp`: the superset API
(`append_char`/`append_sv`/`append_u64`/`append_i64`/`append_double`/
`append_escaped`/`append_hex16` + `ok()`/`size()`/`data()'), inline (all
three current copies are template/inline in headers; the shared one keeps
zero-cost compile-time behavior, no .cpp). The three
`nexigen/{logging,metrics,tracing}/detail/json_writer.hpp` become one-line
re-includes for compatibility with existing code paths. New
`include/nexigen/core/detail/settings_io.hpp`: `read_settings_file`
(file→JSON via the existing `Result` pattern), `parse_u64`, and
`settings_add_diagnostic` — the three module copies drop their local
implementations. Both headers carry the standard mandate-comment block;
no capability spec (internal detail surface, like the existing detail
headers).

### D8 — `.gitignore` gains `build-*/`

Suffix build trees (asan/tsan/release/coverage, plus my verify trees)
stop polluting git status. Exact `build/` line retained.

## Risks / Trade-offs

- [Removal of three spam quota fields is BREAKING for external settings
  files carrying them] → loader keeps diagnostics-per-unknown-key;
  existing files carrying removed keys get a `value discarded` diagnostic
  instead of silent application (previously they were applied to nothing
  — behavior-equivalent). Release note wording provided in the delta.
- [LTO on all targets] → LTO has been documenté as on since the Release
  config exists; enabling it for real may surface codegen hangs; the CI
  matrix runs the same Release build, gate-checked by the test suites.
- [Gauge set reorders writer ordering] → concurrent set/set race is
  control-plane by spec; the shard-line assignment is deterministic
  per call, and `observe()` never produces a torn sum (relaxed loads see
  pre- or post-values).
- [Duplicate `FixedWriter` codegen] → single definition shrinks compile
  surfaces; public per-module include shims keep existing includes
  compiling.

## Migration Plan

External settings: removed quota keys → diagnostics (documented); no
on-fs format change otherwise. Rollback = revert.

## Open Questions

(none — analysis items re-verified against the tree)
