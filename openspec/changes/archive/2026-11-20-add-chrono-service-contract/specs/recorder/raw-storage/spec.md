# Spec Delta

## MODIFIED Requirements

### Requirement: Time and sequence mapping

The 48-byte header's commit time SHALL be the recorder's `CLOCK_MONOTONIC_RAW`
write time. Event time SHALL stay in the payload or in the record type's
documented field, in the native `TimeDomain` the channel declares. The session
SHALL declare its hub domain as `TimeDomain::MonotonicRaw`; conversion to any
other scale SHALL use the recorded time-base history and snapshots, never the
local wall clock. Headers SHALL carry the 64-bit source sequence and publisher
id; a new publisher id or a backwards sequence SHALL start a new publisher
epoch.

#### Scenario: Capture time survives storage

- **WHEN** a video frame with `capture_mono_ns` is recorded
- **THEN** the `.nxrec` record and the video reference preserve that capture
  instant

#### Scenario: UTC is derived, not invented

- **WHEN** a reader converts a stored timestamp to TAI, UTC, or a device scale
- **THEN** it uses the recorded history and snapshots and reports the hub value
  when no entry covers that instant

#### Scenario: Publisher restart is an epoch

- **WHEN** a publisher id changes or its sequence moves backwards
- **THEN** the reader starts a new epoch instead of reporting a multi-billion-
  record gap

#### Scenario: Channel declares its native domain

- **WHEN** a channel is declared
- **THEN** the declaration names the `TimeDomain` and epoch of its event fields,
  and a hardware channel names the device domain and its access mode

### Requirement: Time-base history persistence

The session SHALL record every published map change as an append-only series
carrying generation, domain id/class/name, epoch, forward and reverse maps,
uncertainty, status, sync mode, service flags, and validity intervals in native
and hub units. It SHALL also record 1 Hz `TimeSnapshot` entries; intervals
without either SHALL be recorded as explicit gaps.

#### Scenario: Every change is recorded

- **WHEN** a domain row changes its map, epoch, lock state, or uncertainty
- **THEN** the session gains one history entry for that change and earlier
  entries remain unchanged

#### Scenario: Every second is snapshotted

- **WHEN** the time base is available during recording
- **THEN** the session gains one snapshot per second with every domain value,
  epoch, lock state, and service flag

#### Scenario: Daemon downtime is a gap

- **WHEN** the time base is unavailable during recording
- **THEN** the session records a gap for that interval

#### Scenario: History travels with the session

- **WHEN** the session is copied to another machine
- **THEN** the exporter resolves any domain instant through the embedded history
  and snapshots without reading the local clock

#### Scenario: Unresolvable instant is reported

- **WHEN** an instant falls into a recorded gap
- **THEN** extraction reports it as unresolved and does not substitute a
  neighbouring map

### Requirement: Storage type serialization

`RecorderFormat`, `chrono::TimeDomain`, `chrono::TimeDomainClass`, `TimeSnapshot`,
`StorageSettings`, `SessionManifest`, `SegmentInfo`, `SchemaDeclare`, and
`ChannelDeclare` SHALL provide canonical `to_string`, `std::formatter`, and ADL
`to_json`/`from_json` under `JSON_NOEXCEPTION=1`.

#### Scenario: Enums round-trip

- **WHEN** every `RecorderFormat`, `TimeDomain`, and `TimeDomainClass` value
  passes through `to_string`, `from_string`, JSON, and the formatter
- **THEN** every canonical spelling round-trips

#### Scenario: Settings round-trip

- **WHEN** a valid `StorageSettings` is serialized and parsed back
- **THEN** the decoded value equals the original and invalid JSON leaves the
  destination unchanged

#### Scenario: Manifest, segment, and declarations round-trip

- **WHEN** a `SessionManifest`, `SegmentInfo`, `SchemaDeclare`, and
  `ChannelDeclare` are serialized and parsed back
- **THEN** every path, id, count, schema blob, and channel field survives
  unchanged

#### Scenario: Snapshot round-trip

- **WHEN** a `TimeSnapshot` is serialized and parsed back
- **THEN** every domain entry and service flag survives

## ADDED Requirements

### Requirement: TimeSnapshot record

The recorder SHALL define a `TimeSnapshot` record type carrying generation,
boot token, sync mode, status, service flags, hub and system-scale instants, TAI
and UTC offsets, uncertainty, and every domain's epoch/value/lock state. The
record SHALL be self-describing through the built-in schema and SHALL NOT
duplicate the coefficient table.

#### Scenario: Snapshot is a first-class record

- **WHEN** the recorder captures a 1 Hz snapshot
- **THEN** the `.nxrec` record has the documented type, payload size, and
  CRC-32C coverage

#### Scenario: Snapshot resolves a stored event

- **WHEN** an event native instant falls between map changes
- **THEN** the snapshot and the active history entry together resolve it to any
  destination domain

#### Scenario: Snapshot schema is self-contained

- **WHEN** a session is copied without the project SDK
- **THEN** the converter decodes the snapshot from the embedded built-in schema
