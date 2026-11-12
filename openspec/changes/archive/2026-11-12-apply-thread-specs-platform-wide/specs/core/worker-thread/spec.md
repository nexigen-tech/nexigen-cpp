# Spec Delta

## ADDED Requirements

### Requirement: probe_thread_spec checks a spec on a throwaway thread

`nexigen::core::probe_thread_spec(spec)` SHALL start a `WorkerThread` with an empty body, wait for its start sequence to complete, join it, and return the attribute application status without modifying the caller's thread attributes. A `Required` failure SHALL return its error; a `BestEffort` partial application SHALL return success.

#### Scenario: Required failure is returned

- **WHEN** a required spec is rejected by the OS on the probe thread
- **THEN** `probe_thread_spec` returns the failure status and the caller's affinity and scheduler are unchanged

#### Scenario: BestEffort partial application succeeds

- **WHEN** one requested attribute fails in `BestEffort` mode
- **THEN** `probe_thread_spec` returns success

#### Scenario: Probe thread does not outlive the call

- **WHEN** `probe_thread_spec` returns
- **THEN** the thread has been joined and no probe thread remains
