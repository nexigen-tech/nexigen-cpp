# Spec Delta

## Purpose

The tracing settings loader reads the worker poll interval from the file so it reaches `TracerProviderConfig::worker_poll_interval_us`.

## MODIFIED Requirements

### Requirement: Tracing settings map every provider field

`tracing::Settings` SHALL wrap `tracing::TracerProviderConfig` and read these JSON keys: `core_mask`, `max_tracers`, `max_spans_per_window`, `window_ns`, `worker_poll_interval_us`. Defaults SHALL equal the default-constructed `TracerProviderConfig`.

#### Scenario: Tracing fields load from the global file

- **WHEN** the global tracing file sets a core mask, tracer limit, span budget, and worker poll interval
- **THEN** the merged `tracing::Settings::options` carries those values and `global_loaded` is true

#### Scenario: Rate budget defaults to unlimited

- **WHEN** neither file nor environment sets the span budget or window
- **THEN** both remain zero, which the provider interprets as no rate limiting
