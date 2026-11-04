# Spec Delta

## ADDED Requirements

### Requirement: Worker thread settings preserve legacy affinity compatibility

The metrics loader SHALL read canonical `worker_thread` fields and normalize legacy `core_mask` and `NEXIGEN_METRICS_CORE_MASK` into worker CPUs. Canonical fields SHALL win within one source, while normal global → local → environment precedence SHALL remain. The canonical thread object SHALL be file-only, and an explicitly empty canonical CPU array SHALL clear a lower-priority pin.

#### Scenario: Canonical worker fields merge across sources

- **WHEN** global settings provide legacy affinity and local settings provide a named FIFO worker on CPU 70
- **THEN** the merged options contain the local name/policy/priority and CPU 70

#### Scenario: Legacy environment override remains effective

- **WHEN** no higher-priority canonical setting exists and `NEXIGEN_METRICS_CORE_MASK=5`
- **THEN** the merged worker CPU set equals the CPUs represented by mask 5 and the apply mode remains best effort

#### Scenario: Empty canonical CPUs clear inherited affinity

- **WHEN** a local file explicitly sets `worker_thread.cpus` to `[]` after a global pin
- **THEN** the merged worker has no affinity request

## MODIFIED Requirements

### Requirement: Metrics settings map every provider field

`metrics::Settings` SHALL wrap `metrics::MetricProviderConfig` and read these JSON keys: `collect_interval_ns`, `worker_thread`, `max_meters`, `max_counters`, `max_gauges`, `max_statistics`, `max_explicit_histograms`, `max_exponential_histograms`, plus legacy `core_mask` as a compatibility input. Defaults SHALL equal the default-constructed `MetricProviderConfig`.

#### Scenario: Metrics fields load from the local file

- **WHEN** the local metrics file sets a collection interval, every capacity, and a complete worker thread spec
- **THEN** the merged `metrics::Settings::options` carries those values and `local_loaded` is true

#### Scenario: Defaults survive an absent file

- **WHEN** no metrics file sets a field
- **THEN** the corresponding `MetricProviderConfig` default is preserved, including inherited best-effort worker attributes
