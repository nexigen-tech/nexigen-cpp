# Spec Delta

## MODIFIED Requirements

### Requirement: Stateless infrastructure policy

The system SHALL require a `Policy` with nested `Logger`, `MetricsSink`, and `TraceSink` types and static `emplace_logger`, `emplace_metrics_sink`, and `emplace_trace_sink` methods returning `core::Status`. All three emplacement methods SHALL receive the application name so each policy can inject it into its sink. `Infrastructure` SHALL enforce the logger/sink concepts at compile time; the policy SHALL hold no runtime state.

#### Scenario: Policy types are validated at compile time

- **WHEN** a policy supplies component types and emplacement methods
- **THEN** `Infrastructure` instantiates only if the logger and every sink satisfy their concepts

#### Scenario: Components outlive the providers

- **WHEN** an initialized infrastructure is destroyed or shut down
- **THEN** providers are destroyed before the sinks and logger they reference

#### Scenario: Logger and sinks receive the application name

- **WHEN** initialization runs with a non-empty application name
- **THEN** `emplace_logger`, `emplace_metrics_sink`, and `emplace_trace_sink` observe that name
