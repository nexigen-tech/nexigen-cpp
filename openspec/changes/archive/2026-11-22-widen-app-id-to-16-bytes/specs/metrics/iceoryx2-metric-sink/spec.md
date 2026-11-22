# Spec Delta

## MODIFIED Requirements

### Requirement: MetricFrame wire type

`MetricFrame` SHALL be a fixed-size, trivially-copyable type that carries the publishing
application's 16-byte identifier, the publisher PID, the window timestamp, and exactly one
`MetricRecord`. It SHALL be the sole cross-process contract between the sink and the daemon.

#### Scenario: Frame is a fixed-size POD

- **WHEN** `sizeof(MetricFrame)`, `alignof(MetricFrame)`, and `std::is_trivially_copyable_v<MetricFrame>` are evaluated
- **THEN** the type is trivially copyable, its size is fixed and independent of the record count, and its alignment does not exceed the cache-line alignment of its members

#### Scenario: Frame serialization round-trip

- **WHEN** a `MetricFrame` carrying a 16-byte `app_id`, `pid`, timestamp, and a `MetricRecord` is serialized with `to_json` and parsed back with `from_json` under `JSON_NOEXCEPTION=1`
- **THEN** the `app_id`, `pid`, timestamp, and the embedded record survive the round-trip unchanged

#### Scenario: Sixteen-byte identifier travels whole

- **WHEN** the publisher uses the identifier `CN_LOOP_1`
- **THEN** the frame carries all nine bytes and the daemon renders `CN_LOOP_1`

#### Scenario: Frame diagnostics contract

- **WHEN** `to_string()`, the free `to_string`, and `std::format` are applied to a `MetricFrame`
- **THEN** the member and free results are equal non-empty strings, and the formatter renders the same text

### Requirement: Iceoryx2 metric sink configuration

`Iceoryx2MetricSinkConfig` SHALL carry the publishing application's 16-byte identifier, the
iceoryx2 service name to publish to, and the subscriber buffer depth the daemon needs to receive
one publish burst without discards. It SHALL reject an empty, oversized, or malformed identifier
before creating any port. Its values SHALL round-trip through `to_json`/`from_json` under
`JSON_NOEXCEPTION=1`.

#### Scenario: Config serialization round-trip

- **WHEN** an `Iceoryx2MetricSinkConfig` carrying an app identifier, service name, and subscriber buffer depth is serialized and parsed back
- **THEN** all fields survive the round-trip under `JSON_NOEXCEPTION=1`

#### Scenario: Invalid identifier is rejected

- **WHEN** the configured identifier is empty, longer than 16 bytes, or contains a forbidden
  character
- **THEN** validation fails before any publisher port is created

#### Scenario: Config diagnostics contract

- **WHEN** `to_string()`, the free `to_string`, and `std::format` are applied to an `Iceoryx2MetricSinkConfig`
- **THEN** the member and free results are equal non-empty strings, and the formatter renders the same text
