# Spec Delta

## MODIFIED Requirements

### Requirement: Stateless infrastructure policy

The system SHALL require a `Policy` with nested `Logger`, `Metrics_sink`, `Trace_sink`, and `Chrono_sink` types and static `emplace_logger`, `emplace_metrics_sink`, `emplace_trace_sink`, and `emplace_chrono_sink` methods returning `core::Status`. The logger, metrics-sink, and trace-sink emplacement methods SHALL additionally receive the application name so each policy can inject it into its sink; `emplace_chrono_sink` SHALL NOT receive it. `Infrastructure` SHALL enforce the logger/sink concepts at compile time; the policy SHALL hold no runtime state.

#### Scenario: Policy types are validated at compile time

- **WHEN** a policy supplies component types and emplacement methods
- **THEN** `Infrastructure` instantiates only if the logger and every sink satisfy their concepts

#### Scenario: Components outlive the providers

- **WHEN** an initialized infrastructure is destroyed or shut down
- **THEN** providers are destroyed before the sinks and logger they reference

#### Scenario: Logger and sinks receive the application name

- **WHEN** initialization runs with a non-empty application name
- **THEN** `emplace_logger`, `emplace_metrics_sink`, and `emplace_trace_sink` observe that name, and `emplace_chrono_sink` does not receive it
