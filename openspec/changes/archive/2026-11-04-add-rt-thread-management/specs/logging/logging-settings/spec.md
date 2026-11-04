# Spec Delta

## ADDED Requirements

### Requirement: Canonical and legacy worker settings merge deterministically

The logging loader SHALL read canonical `worker_thread` fields and normalize legacy affinity inputs into the file-worker CPU set. Canonical fields SHALL override legacy fields within one source; defaults → global → local → environment precedence SHALL remain. Canonical thread fields SHALL be file-only, legacy environment affinity SHALL remain supported, and an explicitly empty canonical CPU array SHALL clear a lower-priority pin.

#### Scenario: Legacy environment affinity remains compatible

- **WHEN** `NEXIGEN_LOG_CPU_AFFINITY=2,64-65` is applied without a higher-priority canonical source
- **THEN** the merged file-worker CPU set contains CPUs 2, 64, and 65 and no uint64 mask is formed

#### Scenario: Canonical fields override legacy fields in one source

- **WHEN** one file supplies both legacy affinity and `worker_thread.cpus`
- **THEN** the canonical CPU set wins while omitted name/policy/mode fields inherit normally

#### Scenario: Explicit empty canonical CPUs clear affinity

- **WHEN** a global source supplies affinity and the local file sets `worker_thread.cpus` to `[]`
- **THEN** the merged file worker has an empty CPU set and preserves inherited placement

#### Scenario: Console does not inherit legacy file affinity

- **WHEN** only legacy `flush_cpu_affinity` is supplied
- **THEN** console configuration remains unpinned unless its own canonical worker spec is supplied

## MODIFIED Requirements

### Requirement: Merged settings apply to logger configuration

The system SHALL provide apply helpers that populate a `FileLoggerConfig` and a `ConsoleLogger` configuration from merged settings. The file mapping SHALL transfer the log level, log root directory, maximum file size, worker thread spec, and worker poll interval; the console mapping SHALL transfer the initial level, an explicitly supplied canonical worker thread spec, and the worker poll interval. The application name SHALL be supplied separately through the app layer, not through logging settings. Returned string views SHALL remain valid while the source `logging::Settings` is alive.

#### Scenario: File config receives every mapped field

- **WHEN** merged settings set level `warning`, root `/var/log/hil`, max file size `4096`, a worker thread on CPUs `{1, 64}` with RR priority 40, and worker poll interval `5000`
- **THEN** the applied `FileLoggerConfig` carries those values, including the complete worker spec and `worker_poll_interval_us == 5000`

#### Scenario: Console config receives the poll interval

- **WHEN** merged settings set a canonical worker thread spec and `worker_poll_interval_us`
- **THEN** the applied `ConsoleLogger` config carries those values

#### Scenario: Empty affinity means OS placement

- **WHEN** the merged file-worker CPU set is empty
- **THEN** the applied `FileLoggerConfig` requests no affinity operation
