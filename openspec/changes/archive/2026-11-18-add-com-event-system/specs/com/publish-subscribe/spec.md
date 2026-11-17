# Spec Delta

## ADDED Requirements

### Requirement: Publish-subscribe service options

`nexigen::com::Publisher` and `Subscriber` SHALL accept an optional `PublishSubscribeOptions` containing `max_subscribers`, `max_publishers`, `max_nodes`, `subscriber_max_buffer_size`, `subscriber_buffer_size`, `history_size`, `subscriber_history_request`, and `enable_safe_overflow`. The options SHALL be applied when the service is created and verified when an existing service is opened. Existing overloads SHALL preserve the current transport defaults and behaviour.

#### Scenario: Existing callers keep the old defaults

- **WHEN** a caller creates a publisher or subscriber without options
- **THEN** it uses the same transport settings and compiles with the same call shape as before

#### Scenario: Safe overflow and history settings are applied

- **WHEN** options request safe overflow and a history size
- **THEN** the created service reports those settings

#### Scenario: Subscriber and publisher limits are applied

- **WHEN** options request 16 subscribers, one publisher, and 17 nodes
- **THEN** the service admits that many endpoints and rejects the next one beyond the limit

#### Scenario: Incompatible options are rejected

- **WHEN** two endpoints open the same service with incompatible service-level options
- **THEN** the second open fails instead of silently using different settings

#### Scenario: Buffer depth still works

- **WHEN** options set a subscriber buffer depth
- **THEN** that subscriber can hold that many samples without deterministic discards

### Requirement: Event service options

`nexigen::com::EventNotifier` and `EventListener` SHALL accept an optional `EventOptions` containing `max_listeners`, `max_notifiers`, and `max_nodes`. The options SHALL be applied when the event service is created and verified when it is opened. Existing overloads SHALL preserve the transport defaults.

#### Scenario: Event listener limit is applied

- **WHEN** options request 16 listeners
- **THEN** sixteen listeners can attach and the next listener creation fails

#### Scenario: Event notifier limit is applied

- **WHEN** options request one notifier
- **THEN** a second notifier for the same event service is rejected

#### Scenario: Event node limit is applied

- **WHEN** options request a specific node count
- **THEN** the event service enforces that node limit

#### Scenario: Existing event callers are unchanged

- **WHEN** a caller creates a notifier or listener without options
- **THEN** the call compiles and uses the transport defaults

### Requirement: Service option serialization

`PublishSubscribeOptions` and `EventOptions` SHALL provide `to_string`, `std::formatter`, and non-throwing JSON under `JSON_NOEXCEPTION=1`. A malformed JSON document SHALL leave the destination unchanged.

#### Scenario: Options round-trip

- **WHEN** each option struct is serialized and parsed back
- **THEN** every field keeps its value and the formatter output equals `to_string()`

#### Scenario: Hostile JSON is rejected safely

- **WHEN** a non-object or mistyped JSON value is parsed into an option struct
- **THEN** parsing does not throw and the destination keeps its previous values
