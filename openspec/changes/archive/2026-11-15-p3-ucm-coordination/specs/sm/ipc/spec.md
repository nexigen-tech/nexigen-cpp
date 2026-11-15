# Spec Delta

## ADDED Requirements

### Requirement: UCM channel is served by the arbitration loop

The SM SHALL open a `Nexigen/UCM/UpdateRequest` request-response server and
serve it from the arbitration loop after the TriggerIn drain. UCM handling SHALL
run on the same single writer thread; no additional thread or synchronization
SHALL be introduced.

#### Scenario: UCM request is served

- **WHEN** a UCM client sends an `UpdateRequest`
- **THEN** the arbitration loop receives it, applies the coordinator, and returns an `UpdateResponse`

#### Scenario: UCM does not block idle arbitration

- **WHEN** no UCM request is pending
- **THEN** the `serve_once` call is non-blocking and the loop remains WaitSet-driven
