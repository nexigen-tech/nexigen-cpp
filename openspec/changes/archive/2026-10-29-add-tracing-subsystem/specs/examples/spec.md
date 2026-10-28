# Spec Delta

## MODIFIED Requirements

### Requirement: Runnable example catalogue

The project SHALL provide self-contained executables named `ex_spsc_ring_buffer`, `ex_console_logger`, `ex_file_logger`, `ex_composite_logger`, `ex_counters_and_gauges`, `ex_descriptive_statistics`, `ex_histograms`, `ex_metric_provider_and_sinks`, `ex_nested_and_cross_thread_spans`, and `ex_perfetto_chrome_trace`, located under `examples/core/`, `examples/logging/`, `examples/metrics/`, and `examples/tracing/` respectively, each with its own `main` and no dependency on test harness code.

#### Scenario: Example executables exist after a default build

- **WHEN** a default build completes
- **THEN** all ten example executables exist in the build tree and can be launched directly without arguments

#### Scenario: Examples are self-contained

- **WHEN** an example is built and run outside the test harness
- **THEN** it creates its own inputs and configuration, prints its diagnostics, and does not require a test runner, fixture, or pre-existing file

### Requirement: Documented example invocation

`README.md` SHALL contain a Runnable Examples section that names all ten executables, shows how to configure/build with examples enabled, shows how to run each executable, and documents the `NEXIGEN_BUILD_EXAMPLES=OFF` opt-out.

#### Scenario: Reader can build and run an example from the README

- **WHEN** a reader follows the Runnable Examples section commands from a clean checkout
- **THEN** the examples are built, each named executable can be run, and the opt-out configuration is documented

#### Scenario: Stale duplicated build notes are removed

- **WHEN** the README is read to its end
- **THEN** it ends with the license section and contains no leftover untranslated or duplicated build-mode appendix
