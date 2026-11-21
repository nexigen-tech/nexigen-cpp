# Proposal

## Why

The archived `telemetry-instrumentation` change implemented option C: `dropped.rate_limited`
stays in the `DroppedStats` snapshot and is not metered, because it is counted inside
`Tracer::start_span`. The main spec still promises a `dropped.rate_limited` meter counter.

## What Changes

- Reword the tracer provider counter requirement to cover `started`, `dropped.buffer_full`, and
  `dropped.slot_exhausted`; leave `dropped.rate_limited` to the snapshot.

**BREAKING**: none.

## Capabilities

### New Capabilities

None.

### Modified Capabilities

- `tracing/tracer-provider`: the meter counter list excludes `dropped.rate_limited`.

## Impact

- Spec text only; the implementation already follows this behaviour.
