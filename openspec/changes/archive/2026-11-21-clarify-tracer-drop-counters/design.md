# Design

## Context

See `proposal.md`. `rate_limited` is incremented in `Tracer::start_span`; metering it would require
passing an instrument out of the component or threading `M` through `Tracer`/`ScopedSpan`. The
project rule keeps instruments inside the component that created them.

## Decisions

### D1. Keep `rate_limited` in the snapshot only

The provider meters `started`, `dropped.buffer_full`, and `dropped.slot_exhausted`. The
`DroppedStats` snapshot keeps all three causes, including `rate_limited`.

## Risks / Trade-offs

- **One drop cause is not a metric** → dashboards read `rate_limited` from the snapshot; documented.
