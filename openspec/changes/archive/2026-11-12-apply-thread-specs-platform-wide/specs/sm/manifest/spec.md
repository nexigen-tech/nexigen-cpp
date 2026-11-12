# Spec Delta

## ADDED Requirements

### Requirement: SM manifest carries background thread specs

`SmManifest` SHALL carry a `bg_thread` spec and an `audit_thread` spec as `core::ThreadSpec` fields. The transition-audit worker SHALL apply `effective_thread_spec(audit_thread, bg_thread)` on its own thread. Empty specs SHALL preserve inherited attributes. Both fields SHALL participate in the manifest JSON round-trip and diagnostics.

#### Scenario: Empty specs preserve inherited attributes

- **WHEN** the manifest omits both fields
- **THEN** the audit thread applies no attributes and keeps its inherited placement

#### Scenario: Audit spec overrides the background fallback

- **WHEN** `audit_thread` names one CPU and `bg_thread` names another
- **THEN** the audit thread runs on the audit spec's CPU

#### Scenario: Empty audit spec inherits the background fallback

- **WHEN** `audit_thread` is empty and `bg_thread` names a CPU
- **THEN** the audit thread runs on the background CPU

#### Scenario: Thread specs round-trip through JSON

- **WHEN** a manifest with both specs is serialized and parsed back
- **THEN** every thread-spec field equals the original value
