# Spec Delta

## ADDED Requirements

### Requirement: Spec emptiness and fallback are explicit

`ThreadSpec::has_requests()` SHALL return true exactly when the spec requests at least one attribute: a non-empty name, a non-empty CPU set, or an explicitly configured scheduler (including an explicit `Other,0` reset). `core::effective_thread_spec(primary, fallback)` SHALL return `primary` when it has requests and `fallback` otherwise, without mutating either argument.

#### Scenario: A default spec requests nothing

- **WHEN** a default-constructed `ThreadSpec` is queried
- **THEN** `has_requests()` is false

#### Scenario: Every individual attribute counts as a request

- **WHEN** a spec sets only a name, only a non-empty CPU set, or only an explicit scheduler
- **THEN** `has_requests()` is true in each case, including an explicit `Other,0` scheduler reset

#### Scenario: Empty primary inherits the fallback

- **WHEN** `effective_thread_spec` is called with an empty primary and a fallback carrying CPUs and FIFO priority
- **THEN** the result equals the fallback

#### Scenario: Primary wins when it requests anything

- **WHEN** the primary sets only a name and the fallback sets CPUs and a scheduler
- **THEN** the result equals the primary and the fallback is ignored

#### Scenario: Neither argument is mutated

- **WHEN** `effective_thread_spec` returns either argument
- **THEN** both inputs still compare equal to their pre-call values
