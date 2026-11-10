# Spec Delta

## Purpose

Defines the client API applications use to report their readiness to the Execution Manager over the iceoryx2 event pattern, so dependency-ordered startup can gate on the `Running` state instead of file polling.

## ADDED Requirements

### Requirement: Report application state

`nexigen::exec` SHALL provide a client API `ReportApplicationState(ApplicationState::Running)` that an application calls after its internal initialization and thread pinning; the notification SHALL reach the EM through the iceoryx2 event pattern, and until it arrives the EM SHALL treat the application as still starting.

#### Scenario: Application reports running

- **WHEN** an application finishes initialization and reports `Running`
- **THEN** the EM's listener receives the notification promptly without polling

#### Scenario: Unreported application stays starting

- **WHEN** an application has not reported any state
- **THEN** the EM keeps it in the starting state and dependency gating does not release its dependents

### Requirement: Application state diagnostics

The reporting client SHALL provide a cold-path `to_string()` member plus free helper and a `std::formatter` specialization naming the application and its last reported state.

#### Scenario: Reporter renders its state

- **WHEN** `to_string()` and `std::format("{}", reporter)` are evaluated
- **THEN** the text names the application and the state, and the formatter output equals the member output
