# Spec Delta

## ADDED Requirements

### Requirement: Application settings carry the signal-watcher thread spec

`app::Settings` SHALL carry a `signal_thread` field of type `core::ThreadSpec`. `Infrastructure` SHALL start the signal watcher through a `WorkerThread` that applies this spec on its own thread before polling the signalfd. An empty spec SHALL preserve inherited attributes. The field SHALL participate in the settings diagnostic and JSON serialization.

#### Scenario: Empty spec preserves inheritance

- **WHEN** `signal_thread` is left at its default
- **THEN** the signal watcher applies no attributes and keeps the process's inherited placement

#### Scenario: Explicit spec is applied by the watcher

- **WHEN** `signal_thread` names a CPU and a scheduler
- **THEN** the signal watcher thread runs with those attributes while the caller's thread is unchanged

#### Scenario: The spec round-trips through settings JSON

- **WHEN** `Settings` with a non-empty `signal_thread` is serialized
- **THEN** the JSON contains the thread spec and parsing it back preserves every field
