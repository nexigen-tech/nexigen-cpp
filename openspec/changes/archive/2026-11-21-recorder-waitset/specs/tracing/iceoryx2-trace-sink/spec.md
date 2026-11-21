# Spec Delta

## ADDED Requirements

### Requirement: Span publication wakes waiting collectors

`Iceoryx2TraceSink` SHALL create a companion `<service_name>Event` notifier in `start()` and emit a
best-effort notification after each successfully published frame. A notification failure SHALL NOT
fail `on_event` or change its drop accounting. `stop()` SHALL release the notifier with the
publisher.

#### Scenario: Collector wakes on a span

- **WHEN** a sink publishes a span and a collector listens on `<service_name>Event`
- **THEN** the collector is notified after the frame is sent

#### Scenario: Notification failure is non-fatal

- **WHEN** the notifier reports an error
- **THEN** `on_event` still returns success and the publication counters are unchanged

#### Scenario: Stop releases the companion service

- **WHEN** `stop()` runs
- **THEN** the notifier and publisher are released and `on_event` returns `NotInitialized`
