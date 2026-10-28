# tracing/examples Specification

## Purpose

Defines the two runnable tracing executables that demonstrate the public tracing API end to end: nested and cross-thread span hierarchies, and a Perfetto-loadable Chrome trace timeline.

## Requirements

### Requirement: Nested and cross-thread spans example

The project SHALL provide a self-contained executable `ex_nested_and_cross_thread_spans` under `examples/tracing/` with its own `main`: it builds a provider with a console sink, starts nested spans on the main thread demonstrating automatic parent-child linkage, hands a live span's context to a worker thread and starts a child span there through the explicit parent, and verifies the drained parent-child relationships before exiting.

#### Scenario: The example runs and verifies linkage

- **WHEN** `ex_nested_and_cross_thread_spans` runs in an environment where its scenario can complete
- **THEN** it prints the span timeline to the console, verifies every expected parent-child relationship (nested same-thread spans and the cross-thread child), and exits with status 0

#### Scenario: Verification failure is visible

- **WHEN** the example cannot initialize its provider or a parent-child check fails
- **THEN** it reports the failure on the error stream and exits with a non-zero status

#### Scenario: The example is self-contained

- **WHEN** the example is built and run outside the test harness
- **THEN** it creates its own configuration and inputs, requires no test runner or fixture, and depends only on the public tracing API

### Requirement: Perfetto Chrome trace example

The project SHALL provide a self-contained executable `ex_perfetto_chrome_trace` under `examples/tracing/` with its own `main`: it builds a provider with a Chrome trace sink, records a workload of nested and concurrent spans, flushes and closes the trace file, and prints the file's path with instructions to open it in `ui.perfetto.dev`.

#### Scenario: The example produces a loadable trace file

- **WHEN** `ex_perfetto_chrome_trace` runs to completion
- **THEN** it exits with status 0 and the printed trace file exists, starts with the Chrome trace header, ends with the closing array bracket, and parses as one JSON object containing the recorded spans' events

#### Scenario: The example guides the user

- **WHEN** the example's console output is read
- **THEN** it names the produced file and explains how to load it at `ui.perfetto.dev` to visualize the execution timeline

#### Scenario: The example is self-contained

- **WHEN** the example is built and run outside the test harness
- **THEN** it creates its own configuration and output location, requires no test runner or fixture, and depends only on the public tracing API

### Requirement: Tracing examples pass in every build mode

Both tracing example executables SHALL build and exit 0 under the project's Debug, Release, ThreadSanitizer, and AddressSanitizer configurations, without sanitizer diagnostics.

#### Scenario: Full build-mode matrix is green

- **WHEN** the project is configured and built in Debug, Release, TSan, and ASan modes and both tracing examples run in each
- **THEN** every run exits 0 with the expected console output and no sanitizer report is emitted
