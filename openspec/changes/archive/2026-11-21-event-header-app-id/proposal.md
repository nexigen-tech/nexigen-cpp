# Proposal

## Why

Event channels currently identify the whole service, not the application that published a sample.
`EventHeader` has no application code, so a recorder can only attribute events by service name even
when several applications share one event service.

## What Changes

- `EventHeader` grows from 64 to 72 bytes and carries a four-byte `app_id`.
- `EventChannel::producer()` gains an overload that stamps the application code into every header.
- `nexigen-recd` names event channels by the header application code when present, falling back to
  the service name for producers that do not stamp one.
- The recorder's event-envelope schema moves the payload after the 72-byte header and exposes
  `app_id`.

**BREAKING**: the `EventHeader` wire layout and recorder event-envelope offsets change. This is a
pre-1.0 format change and all producers, consumers, and the recorder move together.

## Capabilities

### New Capabilities

None.

### Modified Capabilities

- `com/event-system`: the header carries an application code; producers can stamp it.
- `recorder/telemetry-capture`: event channels prefer the header application code.

## Impact

- `include/nexigen/com/event_header.hpp`, `event_producer.hpp`, `event_channel.hpp`.
- `src/com/event_system.cpp`, `tools/nexigen-recd`.
- Event-system tests and the recorder smoke.
