# recorder/raw-storage Specification

## Purpose
Defines the recorder's raw session storage: the session manifest, append-only `.nxrec` records, sparse `.nxidx` entries, the embedded schema/channel registry, the preallocated single-writer path, CRC-32C coverage, timestamp/sequence mapping, rotation/retention, and crash recovery.

## Requirements

### Requirement: Session manifest and segment triplet

A session SHALL consist of `session.json` and one or more segments, each with `<session>_<seq>.nxrec`, `<session>_<seq>.nxidx`, and `<session>_<seq>.nxvid`. The manifest SHALL record the format version, session id, time domain, boot token, time-base epoch binding, storage settings, schema/channel tables, and every segment's paths, closed state, record count, gap count, and byte counts. The manifest SHALL be replaced atomically.

#### Scenario: New session creates the triplet and manifest

- **WHEN** a session starts
- **THEN** the manifest exists beside the first `.nxrec`, `.nxidx`, and `.nxvid` files and names each one

#### Scenario: Rotation updates the manifest

- **WHEN** a segment rotates
- **THEN** the closed segment's counts are recorded, the new triplet is appended, and `session.json` is replaced atomically

#### Scenario: Unclosed segment is visible

- **WHEN** the process stops without a footer
- **THEN** the manifest reports the segment as not cleanly closed

#### Scenario: Session is bound to a boot epoch

- **WHEN** a session is opened for recording
- **THEN** its manifest names the boot token and the time-base epoch it was recorded under

### Requirement: Preallocated allocation-free write path

The recorder SHALL preallocate every record, index, video, and schema buffer before recording starts. The measured write loop SHALL perform no heap allocation, no JSON serialization, and no `std::string` construction.

#### Scenario: Steady state does not allocate

- **WHEN** a session records a fixed workload after initialization
- **THEN** heap use measured before and after the measured loop is unchanged

#### Scenario: Buffers are sized at start

- **WHEN** the configured payload capacity or index stride is insufficient for a record
- **THEN** the recorder reports the limit before recording rather than growing a container in the write loop

#### Scenario: Single writer owns the stream

- **WHEN** multiple ingestion or encoder threads produce records
- **THEN** they hand frames to one writer that owns the append order and the index

### Requirement: Record framing and CRC-32C

Every `.nxrec` record SHALL begin with a 48-byte header containing a magic value, record type, flags, channel id, payload size, commit time, source sequence, publisher id, header CRC-32C, and payload CRC-32C. The recorder SHALL compute CRC-32C over the header fields and the payload for every telemetry and metadata record.

#### Scenario: Header and payload are validated

- **WHEN** a reader opens a record
- **THEN** it rejects the record if either the header or payload CRC-32C does not match

#### Scenario: CRC uses the accelerated path

- **WHEN** the CPU supports SSE4.2 or ARMv8 CRC instructions
- **THEN** the recorder uses the hardware CRC-32C path and produces the same checksum as the table fallback

#### Scenario: Torn record is rejected

- **WHEN** the file ends in the middle of a header or payload
- **THEN** the reader stops at the previous valid record and does not expose the torn bytes

### Requirement: Sparse timestamp index

Each segment SHALL maintain an `.nxidx` file of 64-byte entries containing commit time, record offset, minimum and maximum event time, channel mask, interval record count, end offset, and an entry CRC-32C. An entry SHALL be emitted when the configured byte stride or time interval is reached.

#### Scenario: Index entry bounds an interval

- **WHEN** an interval is closed
- **THEN** its entry describes the exact `.nxrec` byte range and the min/max event time of the records inside it

#### Scenario: Time-range lookup uses the index

- **WHEN** a reader requests an event-time interval
- **THEN** it binary-searches the index and reads only intervals whose `[min_event, max_event]` intersects the request

#### Scenario: Out-of-order event times are tolerated

- **WHEN** a record's event time is older than the previous record
- **THEN** the interval's min/max still bounds it and the reader does not assume global ordering

### Requirement: Embedded schema and channel registry

Each segment SHALL declare every schema and channel it uses before the first record that references it. Declarations SHALL include a stable schema id, schema name/version/encoding, the full embedded schema descriptor bytes, channel id, topic, kind, message encoding, units, endianness, and metadata. The registry SHALL also be mirrored in `session.json`. No declaration SHALL depend on an external file, producer SDK, network resource, or environment configuration.

#### Scenario: A segment is self-describing

- **WHEN** a reader opens a single `.nxrec` without `session.json`
- **THEN** the leading schema and channel declarations suffice to identify the payload types used in that segment

#### Scenario: Schema change creates a new channel

- **WHEN** a producer changes its payload schema
- **THEN** the recorder assigns a new channel id and declares it before the first record using the new schema

#### Scenario: Unknown schema is explicit

- **WHEN** a channel references a schema id that was never declared
- **THEN** the reader reports the missing schema instead of decoding the payload by guesswork

#### Scenario: Declarations carry no external references

- **WHEN** a channel's schema is config-provided
- **THEN** the full descriptor bytes are embedded in the session and no path, URL, digest-only reference, or producer SDK is needed to decode it

### Requirement: Self-contained session portability

A session copied to another machine SHALL be decodable with the converter alone. The session SHALL contain every schema descriptor, channel declaration, codec extradata, unit, endianness, time-calibration record, and dataset identity needed for listing, inspection, and extraction; the converter SHALL NOT require external schema files, configuration, environment variables, a producer SDK, or network access.

#### Scenario: Copy session and converter only

- **WHEN** a session directory is copied to a clean machine that has the converter binary but no project SDK or schema files
- **THEN** the converter lists the datasets, reports their size and time range, and extracts the selected data

#### Scenario: Missing decoding metadata is an error

- **WHEN** a selected dataset references a schema or codec configuration absent from the session
- **THEN** the converter exits non-zero and names the missing metadata instead of guessing

#### Scenario: Time calibration travels with the session

- **WHEN** a session recorded with UTC calibration is copied elsewhere
- **THEN** the converter derives UTC timestamps from the embedded calibration without reading the local clock

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

### Requirement: Rotation and retention

A segment SHALL rotate when maximum bytes, duration, or record count is reached. A record SHALL NOT span segments. Retention SHALL delete only fully closed segments when `circular`; `stop` SHALL finalize the active segment and stop accepting data; `min_free_bytes` SHALL prevent opening a new segment.

#### Scenario: Record never spans segments

- **WHEN** the next record does not fit the active segment
- **THEN** the segment closes and the record is written entirely to the next one

#### Scenario: Circular retention keeps the active segment

- **WHEN** the retention limit is exceeded
- **THEN** only closed triplets are deleted and the active triplet remains readable

#### Scenario: Low disk stops rotation cleanly

- **WHEN** free space is below `min_free_bytes`
- **THEN** no new segment is opened and the closed segments remain intact

### Requirement: Crash recovery

A reader or recovery pass SHALL scan `.nxrec` from the start, validate each header/payload CRC, stop at the first invalid or incomplete record, and reconstruct the missing `.nxidx` in memory. A clean segment SHALL end with a footer whose CRC is validated.

#### Scenario: Truncated tail yields a prefix

- **WHEN** a segment is truncated mid-record
- **THEN** recovery returns every intact record before the truncation and reports the segment as unclosed

#### Scenario: Missing index is rebuilt

- **WHEN** `.nxidx` is absent or truncated
- **THEN** the reader reconstructs the sparse index from `.nxrec` and still answers time-range queries

#### Scenario: Clean footer validates

- **WHEN** a segment was finalized normally
- **THEN** the footer CRC matches and the reader reports a clean close

### Requirement: COM-free storage boundary

The storage library SHALL build and link without `nexigen_com` or `nexigen_media`; the recorder library SHALL not include MCAP, FFmpeg, or iceoryx2 headers.

#### Scenario: Build without COM or media

- **WHEN** the project is configured with both gates off
- **THEN** the recorder storage library and its tests build and pass

#### Scenario: Converter uses the same reader

- **WHEN** `nexigen-export` opens a session
- **THEN** it reads the raw files through the storage reader without creating a transport node

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

### Requirement: Safe session identifiers and paths

The recorder SHALL reject empty session identifiers, `..`, absolute segment names, and traversal in layout tokens before creating directories. A valid relative layout SHALL create the session beneath the configured root.

#### Scenario: Traversal is rejected

- **WHEN** a session id or layout token contains `..` or an absolute path
- **THEN** initialization returns `InvalidArgument` and no directory is created outside the root

#### Scenario: Valid nested layout is created

- **WHEN** the layout resolves to a safe relative path
- **THEN** the session directory is created beneath the configured root

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
