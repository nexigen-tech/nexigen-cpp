# Spec Delta

## MODIFIED Requirements

### Requirement: StateClient command reception

The EM SHALL expose the `StateClient` receiver as a request-response service accepting typed `SetState(FunctionGroup, TargetState)` commands from the SM. Every command SHALL be acknowledged with an acceptance flag and a result value from `Success`, `InProgress`, `OperationFailed`, `OperationCanceled`, `UnexpectedTermination`, `InvalidTransition`, `InvalidArgument`, or `AccessDenied`; the result vocabulary SHALL round-trip through text, formatting, and JSON. `OperationCanceled` SHALL be emitted when a newer request-id supersedes an older in-flight or already-completed request. The command SHALL carry a 64-bit `request_id`; `0` keeps the legacy no-cancellation behavior.

#### Scenario: EM receives a SetState command

- **WHEN** the SM sends `SetState(FunctionGroup, TargetState)` over the request-response channel
- **THEN** the EM receives the typed command and acknowledges it with an acceptance flag and result

#### Scenario: Result vocabulary round-trips

- **WHEN** each state-result value passes through `to_string`, `from_string`, `to_json`, `from_json`, and `std::format`
- **THEN** every canonical name round-trips and the formatter renders the name

#### Scenario: Superseded request is canceled

- **WHEN** a newer request id replaces an in-flight transition and the older request is retried
- **THEN** the acknowledgement reports `OperationCanceled`
