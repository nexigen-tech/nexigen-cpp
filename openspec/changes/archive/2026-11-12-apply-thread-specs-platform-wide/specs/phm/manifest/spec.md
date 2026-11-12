# Spec Delta

## ADDED Requirements

### Requirement: PHM manifest carries background thread specs

`PhmManifest` SHALL carry a `bg_thread` spec and a `fault_storage_thread` spec as `core::ThreadSpec` fields. The fault-storage worker SHALL apply `effective_thread_spec(fault_storage_thread, bg_thread)` on its own thread. Empty specs SHALL preserve inherited attributes. Both fields SHALL participate in the manifest JSON round-trip and diagnostics.

#### Scenario: Empty specs preserve inherited attributes

- **WHEN** the manifest omits both fields
- **THEN** the fault-storage thread applies no attributes and keeps its inherited placement

#### Scenario: Storage spec overrides the background fallback

- **WHEN** `fault_storage_thread` names one CPU and `bg_thread` names another
- **THEN** the fault-storage thread runs on the storage spec's CPU

#### Scenario: Empty storage spec inherits the background fallback

- **WHEN** `fault_storage_thread` is empty and `bg_thread` names a CPU
- **THEN** the fault-storage thread runs on the background CPU

#### Scenario: Thread specs round-trip through JSON

- **WHEN** a manifest with both specs is serialized and parsed back
- **THEN** every thread-spec field equals the original value
