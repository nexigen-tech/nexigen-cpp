# Proposal

## Why

The `ex_app_cli_settings` example finishes its 500-tick workload in well under a second and writes `"collect_interval_ns": 0` in its local settings, so it never demonstrates the periodic background collection it configures — and it emits a single flat span, which under-uses the tracing surface the command-line flow is meant to showcase. A longer, paced run with one-second collection intervals and richer traces makes the example exercise the same paths a real application would.

## What Changes

- The example's workload runs for **15 seconds** (paced ticks) instead of completing instantly under a 2-second safety deadline; the process still exits 0 and stays far below the existing 60-second bound.
- The generated local settings files `metrics.json` and `chrono.json` set `"collect_interval_ns": 1000000000` (one second), enabling the background harvester/snapshot workers; periodic batches appear in `metrics.jsonl` and `chrono.jsonl` alongside the explicit `collect_now()`/`snapshot_now()` calls.
- The example writes **several traces** with **nested span hierarchies**: at least three distinct trace ids, parent-child edges created through explicit `SpanContext` parenting, more than one `SpanKind`, and numeric/string attributes plus per-span statuses.

No breaking changes: the command-line interface, the settings file names, the output artifact names and the exit-code contract are unchanged.

## Capabilities

### New Capabilities

(none)

### Modified Capabilities

- `app/application`: the "Runnable command-line settings example" requirement changes — the run duration (15 s workload), the generated settings contents (`collect_interval_ns` = 1 s for metrics and chrono), and the trace/span shape (several traces, nested spans, varied kinds and attributes) become part of the observable contract.

## Impact

- **Affected code:** `examples/app/ex_app_cli_settings.cpp` only (workload pacing, settings-file constants, trace construction), plus a wording touch-up in the README description of the example. No library headers, sources or settings parsers change.
- **Performance:** no hot-path impact — the example is cold-path startup plus a paced loop that sleeps between ticks; library producer/worker paths are untouched.
- **Latency (ns):** zero change to library latency budgets. The example deliberately idles ~10 ms per tick; this is example pacing, not a latency surface. Span/attribute writes stay on the existing fixed-offset hot API.
- **Memory allocation:** no change to library allocation behavior. Span records, metric slots and snapshots come from the pre-allocated provider arenas; the workload loop introduces no heap allocation (attribute keys/values are static strings).
- **Test/CI time:** the `ex_app_cli_settings` CTest entry grows from <1 s to ~15 s per build mode (Debug/Release/TSan/ASan); the runner script's 60-second timeout still covers the run with ~4x margin.
