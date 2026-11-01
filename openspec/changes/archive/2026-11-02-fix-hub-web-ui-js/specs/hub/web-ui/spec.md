# Spec Delta

## ADDED Requirements

### Requirement: Embedded script parses and bootstraps

The UI's embedded script SHALL be valid JavaScript that executes on load: it SHALL register the SSE tree subscription and the initial tree fetch, and it SHALL transition the connection badge out of its initial `connecting` state once the hub serves the tree.

#### Scenario: Script parses without error

- **WHEN** the embedded script is loaded by a browser
- **THEN** it parses without a syntax error and its top-level statements execute

#### Scenario: Connection badge leaves the initial state

- **WHEN** a browser opens `/` and the hub serves the tree
- **THEN** the connection badge transitions from `connecting` to `connected` (or an explicit error state) without a page reload

#### Scenario: Initial tree is fetched and rendered

- **WHEN** the script bootstraps against a hub with discovered suites
- **THEN** the run tree is fetched and rendered without requiring the user to select a stream first
