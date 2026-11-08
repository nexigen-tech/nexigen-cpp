# Proposal

## Why
`stopwatch.hpp` and `scoped_stopwatch.hpp` each hold two types (`LapSnapshot` + `Stopwatch`, `OutRefSink` + `ScopedStopwatch`). Split for file-per-entity navigation and include hygiene.

## What Changes
- `stopwatch.hpp` → `lap_snapshot.hpp` + trimmed `stopwatch.hpp`.
- `scoped_stopwatch.hpp` → `out_ref_sink.hpp` + trimmed `scoped_stopwatch.hpp`.

## Capabilities
Pure refactor — `skip_specs: true`.

## Impact
Header-only reorganization; no behavior, layout, or performance change.
