# Spec Delta

## ADDED Requirements

### Requirement: Worker thread settings preserve legacy affinity compatibility

The tracing loader SHALL read canonical `worker_thread` fields and normalize legacy `core_mask` and `NEXIGEN_TRACING_CORE_MASK` into worker CPUs. Canonical fields SHALL win within one source, normal global → local → environment precedence SHALL remain, and the canonical thread object SHALL be file-only. Explicitly empty canonical CPUs SHALL clear a lower-priority pin.

#### Scenario: Canonical worker fields merge across sources

- **WHEN** global settings provide legacy affinity and local settings provide a named RR worker on CPU 65
- **THEN** the merged options contain the local name/policy/priority and CPU 65

#### Scenario: Legacy environment override remains effective

- **WHEN** no higher-priority canonical setting exists and `NEXIGEN_TRACING_CORE_MASK=5`
- **THEN** the merged worker CPU set equals the CPUs represented by mask 5 and the apply mode remains best effort

## MODIFIED Requirements

### Requirement: Tracing settings map every provider field

`tracing::Settings` SHALL wrap `tracing::TracerProviderConfig` and read these JSON keys: `worker_thread`, `max_tracers`, `max_spans_per_window`, `window_ns`, `worker_poll_interval_us`, plus legacy `core_mask` as a compatibility input. Defaults SHALL equal the default-constructed `TracerProviderConfig`.

#### Scenario: Tracing fields load from the global file

- **WHEN** the global tracing file sets a complete worker thread spec, tracer limit, and span budget
- **THEN** the merged `tracing::Settings::options` carries those values and `global_loaded` is true

#### Scenario: Rate budget defaults to unlimited

- **WHEN** neither file nor environment sets the span budget or window
- **THEN** both remain zero, which the provider interprets as no rate limiting
