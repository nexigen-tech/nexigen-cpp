# Spec Delta

## ADDED Requirements

### Requirement: Worker thread settings preserve legacy affinity compatibility

The chrono loader SHALL read canonical `worker_thread` fields and normalize legacy `core_mask` and `NEXIGEN_CHRONO_CORE_MASK` into worker CPUs. Canonical fields SHALL win within one source, normal global → local → environment precedence SHALL remain, and the canonical thread object SHALL be file-only. Explicitly empty canonical CPUs SHALL clear a lower-priority pin.

#### Scenario: Canonical worker fields merge across sources

- **WHEN** global settings provide legacy affinity and local settings provide a named FIFO worker on CPU 64
- **THEN** the merged options contain the local name/policy/priority and CPU 64

#### Scenario: Legacy environment override remains effective

- **WHEN** no higher-priority canonical setting exists and `NEXIGEN_CHRONO_CORE_MASK=5`
- **THEN** the merged worker CPU set equals the CPUs represented by mask 5 and the apply mode remains best effort

## MODIFIED Requirements

### Requirement: Merged settings apply to provider configuration

The system SHALL provide an apply helper that populates a `ChronoProviderConfig` from merged settings, transferring the collection interval and the complete worker thread spec.

#### Scenario: Provider config receives every mapped field

- **WHEN** merged settings set an interval of one second and a named RR worker on CPUs `{2, 64}` with priority 40
- **THEN** the applied `ChronoProviderConfig` carries the interval and every worker attribute

#### Scenario: Legacy affinity maps to the worker CPU set

- **WHEN** merged settings contain only a legacy core mask of `0x3`
- **THEN** the applied worker CPU set contains CPUs 0 and 1 and the apply mode remains best effort
