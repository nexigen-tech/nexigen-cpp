# Design

## Context

`examples/app/ex_app_cli_settings.cpp` runs a 500-tick loop that completes in microseconds under a 2-second safety deadline, writes `"collect_interval_ns": 0` into `metrics.json` and `chrono.json` (which disables the background harvester/snapshot workers), and records one flat `tick-loop` span. The providers already support periodic background delivery: a non-zero `collect_interval` starts a pinned worker that emits one batch/snapshot per interval (`metric_provider.hpp`, `chrono_provider.hpp`), and `Tracer::start_span` takes an explicit parent `SpanContext*` for nesting. See proposal.md for motivation.

Constraints: example code is cold path — sleeps and wall-clock pacing are acceptable there; no library headers change; the runner script wraps the run in a 60-second `timeout`.

## Goals / Non-Goals

**Goals:**
- A paced workload whose wall time is deterministic (~15 s) and observable in the artifacts.
- The generated settings exercise periodic one-second background collection.
- Traces that demonstrate nesting, multiple span kinds and attributes — the shape a real application produces.
- Keep the example self-verifying (non-empty artifact checks) and the exit-code contract unchanged.

**Non-Goals:**
- No changes to the command-line interface, settings parsers, providers, or any library code.
- No new example flags (e.g., a `--quick` mode); the CTest smoke entry runs the full 15 s.
- No cross-thread span hand-off (already demonstrated by `ex_nested_and_cross_thread_spans`).
- No README restructuring — only the example description paragraph is touched up.

## Decisions

1. **Duration-driven loop over tick-cap loop.** The workload loops until `steady_clock` start + 15 s with `sleep_until` pacing at a 10 ms tick period (~1500 ticks), instead of racing through `kTickCap = 500`. A wall-clock deadline makes the runtime deterministic regardless of machine speed; sleeping (not spinning) keeps the example cheap on CI. Alternative: keep the tick cap and insert a 15 s sleep at the end — rejected, the artifacts would show all activity in the first millisecond. `kLogEvery`/`kCollectEvery` are dropped: logging moves to a time cadence (~every 500 ms), and periodic collection is delegated to the background workers (decision 2).

2. **Background workers replace periodic `collect_now()`/`snapshot_now()`.** `metrics.json` and `chrono.json` write `"collect_interval_ns": 1000000000`; the per-100-tick manual collection calls are removed. One explicit final `collect_now()`/`snapshot_now()` pair stays before shutdown so the last counters/gauge values are deterministic. Over 15 s the workers emit ~14 batches each, satisfying the "at least ten" floor in the spec with margin.

3. **Three-trace structure with explicit-parent nesting.** The workload records:
   - `startup` trace: root `startup` (Internal) with children `create-instruments` and `check-settings`. The tracer exists only after `initialize()` returns, so the root covers the post-initialization phases rather than the pre-init settings writing (which cannot carry live spans).
   - `tick-loop` trace: root `tick-loop` (Internal) with a `tick-batch` child (Producer) opened for every 2 s of paced ticks, each closed by a nested `collect` child (Consumer) carrying the per-batch report, and attributes (`batch` index, `ticks` count, `elapsed_ms`, string `settings_dir` on the root);
   - `shutdown` trace: root `shutdown` (Internal) with children `final-collect` (the kept final `collect_now()`/`snapshot_now()` pair) and `flush-artifacts` (the last log write before the sinks flush), status `Ok`.
   This yields 3 trace ids, two-level parent-child edges, three span kinds and both numeric and string attributes — the "more complex" shape the request asks for — using only the existing hot-path API (`start_span(name, kind, &parent.context())`, `set_attribute`, `set_status`, `end`). `ScopedSpan` is non-movable, so each batch span lives in its own loop iteration scope.

4. **Attributes as static strings.** Attribute keys/values are `constexpr` string views so the trace construction stays allocation-free, consistent with the project's hot-path discipline even though the example is cold path.

## Risks / Trade-offs

- [CI time grows by ~15 s per build mode] → Accepted; the runner's 60-second `timeout` retains ~4x margin, and the spec floor (10 s) leaves room for slow sanitizer builds. If this ever becomes painful, a `--quick` flag can be proposed separately.
- [Timing floors ("at least ten batches", "no less than ten seconds") could flake on a heavily loaded machine] → Floors are set well below the deterministic expectation (14 batches, 15 s); `steady_clock` pacing is immune to wall-clock jumps; TSan/ASan slowdowns do not shorten the window because the deadline is wall time.
- [A dropped or coalesced batch under extreme load weakens the artifact counts] → The harvest workers run on their own pinned threads and drain once per second; the 10-batch floor absorbs several consecutive misses without failing.
- [Paced sleeps make the loop dependent on `sleep_until` resolution] → On Linux this is far below 10 ms granularity; the loop re-checks the deadline each iteration, so overshoot only stretches the run toward the 60 s bound, never breaks it.
