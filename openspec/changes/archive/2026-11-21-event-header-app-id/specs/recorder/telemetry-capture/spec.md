# Spec Delta

## MODIFIED Requirements

### Requirement: Event channel capture

The collector SHALL record `com` event channels as `ComPayload` records without a compile-time
payload type. Each record SHALL carry the event header followed by the payload bytes, and its
channel SHALL name the application code from the header when it is non-empty, otherwise the service
name, plus the payload type and payload size. A new producer epoch SHALL start a fresh sequence
baseline.

#### Scenario: Event is recorded with header and payload

- **WHEN** an application publishes an event with a 32-byte payload
- **THEN** the session stores one `ComPayload` record whose payload is the event header followed
  by the 32 payload bytes

#### Scenario: Channel names the payload type

- **WHEN** events of the type `CameraFrame` are recorded
- **THEN** the channel metadata names `CameraFrame` and its payload size

#### Scenario: Channel names the application

- **WHEN** two applications with codes `LIDR` and `ADAS` publish on one event service
- **THEN** the session declares two event channels whose topics identify `LIDR` and `ADAS`

#### Scenario: Missing application code falls back to the service

- **WHEN** an event arrives with an empty application code
- **THEN** its channel topic names the service instead

#### Scenario: Epoch change is not a gap

- **WHEN** an event producer restarts and its producer epoch changes
- **THEN** the collector starts a new baseline and writes no gap record for the reset sequence

#### Scenario: Event transport gap is recorded

- **WHEN** event sequences skip three values within one producer epoch
- **THEN** a gap record with count 3 and reason `IoxRingOverflow` precedes the next event

#### Scenario: Unknown event service is skipped

- **WHEN** a configured event service does not exist
- **THEN** the collector reports it and continues recording the remaining streams
