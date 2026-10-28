# examples Specification

## Purpose

Defines the optional, independently runnable example programs that demonstrate the public `nexigen` API end to end and serve as build/execution smoke checks in every supported configuration.

## Requirements

### Requirement: Optional example build

The build SHALL expose a `NEXIGEN_BUILD_EXAMPLES` option that defaults to `ON`, adding the example targets when enabled, and SHALL exclude all example targets, their sources, and their CTest entries when set to `OFF`.

#### Scenario: Default configuration builds examples

- **WHEN** the project is configured without overriding `NEXIGEN_BUILD_EXAMPLES`
- **THEN** the option reads `ON`, the `examples/` subdirectory is configured, and all catalogue executables are buildable

#### Scenario: Opting out

- **WHEN** the project is configured with `-DNEXIGEN_BUILD_EXAMPLES=OFF`
- **THEN** configuration succeeds without requiring example sources or dependencies, no example executable is produced, and no example test is registered

### Requirement: Runnable example catalogue

The project SHALL provide self-contained executables named `ex_spsc_ring_buffer`, `ex_console_logger`, `ex_file_logger`, `ex_composite_logger`, `ex_counters_and_gauges`, `ex_descriptive_statistics`, `ex_histograms`, `ex_metric_provider_and_sinks`, `ex_nested_and_cross_thread_spans`, and `ex_perfetto_chrome_trace`, located under `examples/core/`, `examples/logging/`, `examples/metrics/`, and `examples/tracing/` respectively, each with its own `main` and no dependency on test harness code.

#### Scenario: Example executables exist after a default build

- **WHEN** a default build completes
- **THEN** all ten example executables exist in the build tree and can be launched directly without arguments

#### Scenario: Examples are self-contained

- **WHEN** an example is built and run outside the test harness
- **THEN** it creates its own inputs and configuration, prints its diagnostics, and does not require a test runner, fixture, or pre-existing file

### Requirement: Example exit status reports success

Each example executable SHALL exit with status 0 after completing its documented scenario, and SHALL return a non-zero status if initialization, logging, queue transfer, or verification fails.

#### Scenario: Successful run exits zero

- **WHEN** any catalogue example runs in an environment where its scenario can complete
- **THEN** it finishes with exit code 0 and the expected diagnostic output

#### Scenario: Failed precondition is visible

- **WHEN** an example cannot initialize its logger, open its log directory, or verify its output
- **THEN** it reports the failure on the appropriate stream and exits with a non-zero status

### Requirement: CTest smoke coverage for every example

The test configuration SHALL register one CTest test per example executable, using the executable name as the test name, so that a normal `ctest` run builds confidence that the documented API links and executes.

#### Scenario: Examples run under ctest

- **WHEN** `ctest` is run after a default build
- **THEN** each example test executes its executable and passes only if the process exits with status 0

#### Scenario: Disabled examples register no tests

- **WHEN** `NEXIGEN_BUILD_EXAMPLES=OFF` and `ctest` is run
- **THEN** no example test appears in the test list and the remaining library tests are unaffected

### Requirement: Examples pass in every supported build mode

Every catalogue example SHALL build and exit 0 under the project's Debug, Release, ThreadSanitizer, and AddressSanitizer configurations, without sanitizer diagnostics.

#### Scenario: Full build-mode matrix is green

- **WHEN** the project is configured and built in Debug, Release, TSan, and ASan modes and `ctest` is run in each
- **THEN** every example test passes in all four modes and no sanitizer report is emitted

### Requirement: Documented example invocation

`README.md` SHALL contain a Runnable Examples section that names all ten executables, shows how to configure/build with examples enabled, shows how to run each executable, and documents the `NEXIGEN_BUILD_EXAMPLES=OFF` opt-out.

#### Scenario: Reader can build and run an example from the README

- **WHEN** a reader follows the Runnable Examples section commands from a clean checkout
- **THEN** the examples are built, each named executable can be run, and the opt-out configuration is documented

#### Scenario: Stale duplicated build notes are removed

- **WHEN** the README is read to its end
- **THEN** it ends with the license section and contains no leftover untranslated or duplicated build-mode appendix
