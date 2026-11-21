# Design

## Context

See `proposal.md`. `com` already uses the pattern: the meter is the last template parameter,
defaulted to `core::NullMeter`, counters are created once at construction, and `NullMeter` endpoints
contain no counter code.

## Decisions

### D1. Same meter convention as `com`

Every meter-aware template takes `core::concepts::MeterLike M = core::NullMeter` as its last
parameter and accepts `M*` at construction/initialization. `bind_counters` creates each counter once;
recording increments a pointer after a null check.

- Logging: `Producer<Shard, Message, MaxThreads, M>` creates `accepted`,
  `dropped.rate_limited`, `dropped.buffer_full`, `dropped.slot_exhausted`; the three loggers forward
  the meter to `Producer`.
- Tracing: `TracerProvider<Shard, MaxThreads, M>` creates `started` plus the same three drop
  counters; increments sit beside the existing drop accounting.
- Chrono: `Provider<MaxSources, M>` creates `calibration_failed`, `acquire_exhausted`,
  `snapshot_failed`; increments sit on the existing error paths.

### D1a. Instruments never leave the component that created them

A component receives a `MeterLike` factory and creates the instruments it needs. The resulting
instrument pointers stay inside that component: they are never returned, stored in another
component, or passed across a module boundary. Other components receive behaviour, not instruments.

Consequence for tracing: `TracerProvider` creates `started`, `dropped.buffer_full`, and
`dropped.slot_exhausted`; `dropped.rate_limited` stays in the `DroppedStats` snapshot only, because
it is counted in `Tracer::start_span` and metering it would require handing an instrument to another
object. `Tracer` remains part of the tracing component: it may call the provider's non-template
counter methods, but never receives a counter pointer.

### D2. Per-endpoint meters

The composition root creates one meter per logger/provider (`MetricProvider::get_meter`), because
`Meter` rejects duplicate counter names. This matches the com endpoint rule.

### D3. e2e and CI

`tests/tools/recd_smoke.sh` grows a two-application run, an event stream, and a forced sequence gap;
the COM CI job builds `nexigen-recd` and runs the smoke test with the pinned iceoryx2 revision.

## Risks / Trade-offs

- **Template parameters ripple through aliases** → defaults keep existing code compiling; aliases
  are updated where the meter is used.
- **Counter increments on hot paths** → logging/tracing increments sit on the worker or drop paths;
  `NullMeter` removes them.
- **Fixed publisher tables in the collector** → the smoke test uses two application codes, not a
  stress population.

## Migration Plan

1. Logging meter + counters, tests.
2. Tracing meter + counters, tests.
3. Chrono meter + counters, tests.
4. Collector e2e + CI job, then the validation matrix.
