# Spec Delta

## Purpose

Defines the portable CAN viewer session contract: canonical JSONL v1 with session metadata, typed frame/gap/status records, legacy `nexigen-canmon --json` import, CSV/TXT export, and deterministic offline replay.

## ADDED Requirements

### Requirement: Canonical JSONL starts with a session header

The viewer SHALL write a first-line `session` object with format, version, creation time, boot token, time-base information, channel profiles, EDS bindings, and initial counters.

#### Scenario: Header identifies the format
- **WHEN** a JSONL export starts
- **THEN** the first line has `type` `session`, `format` `nexigen.canview.session`, and `version` 1

#### Scenario: Channel metadata is available offline
- **WHEN** an offline session is opened
- **THEN** channel names, ids, nominal/data bitrates, and FD flags are reconstructed from the header

#### Scenario: EDS binding metadata is available offline
- **WHEN** the header contains EDS bindings
- **THEN** the viewer can resolve node aliases, colours, paths, and hashes before decoding records

### Requirement: Records are typed

Every JSONL record SHALL have a `type` of `frame`, `gap`, or `status`, and SHALL carry the fields required to reconstruct its history record without guessing.

#### Scenario: Frame record
- **WHEN** a frame is exported
- **THEN** its identifier, kind, origin, timestamp source, hardware sequence, length, flags, payload, channel, and deltas are present

#### Scenario: Gap record
- **WHEN** a loss record is exported
- **THEN** its reason, lost count, count-exact flag, channel, and reference time are present

#### Scenario: Status record
- **WHEN** a channel status is exported
- **THEN** its state and counters are present and it is not inserted into the frame history

### Requirement: Hardware time is authoritative

Every frame record SHALL carry `hw_ts_ns`; estimated UTC fields SHALL be optional and explicitly marked as estimated.

#### Scenario: Hardware-only export
- **WHEN** no UTC anchor is available
- **THEN** `hw_ts_ns` is present and `utc_est_ns`/`timestamp` are absent or null

#### Scenario: Estimated UTC export
- **WHEN** a UTC anchor is available
- **THEN** `utc_est_ns` and `utc_estimated` are present and `utc_source` names the anchor source

#### Scenario: No fabricated UTC
- **WHEN** a record has no usable hardware timestamp
- **THEN** the exporter does not emit an ISO timestamp for it

### Requirement: The canonical payload form is hexadecimal

The canonical frame payload SHALL be a lowercase hexadecimal string without separators; import SHALL also accept spaced hex and numeric arrays.

#### Scenario: Canonical output
- **WHEN** a frame with payload `01 02 AB` is exported
- **THEN** `data` is `"0102ab"`

#### Scenario: Legacy array input
- **WHEN** an imported legacy line contains `data:[1,2,171]`
- **THEN** the payload is reconstructed as `01 02 AB`

#### Scenario: Spaced hex input
- **WHEN** an imported line contains `"01 02 AB"`
- **THEN** the payload is reconstructed as `01 02 AB`

### Requirement: Legacy CLI output is importable

The viewer SHALL import `nexigen-canmon --json` frame and status lines that have no session header, SHALL create synthetic channel descriptors from their ids, and SHALL leave UTC unavailable.

#### Scenario: Legacy frame line
- **WHEN** a line contains `hardware_timestamp_ns`, `channel_id`, `can_id`, and `data`
- **THEN** it is imported as a frame and assigned a new local sequence number

#### Scenario: Legacy status line
- **WHEN** a line contains channel status fields
- **THEN** it is applied to the synthetic channel registry and is not written as a frame

#### Scenario: Missing bitrate
- **WHEN** a legacy session has no bitrate metadata
- **THEN** the offline heatmap reports unavailable bitrate instead of inventing one

### Requirement: Import renumbers records locally

Imported records SHALL receive new monotonically increasing local `seq` values; the file's `seq` SHALL NOT be used as the local identity.

#### Scenario: Single file
- **WHEN** a file whose first frame has `seq` 100 is imported into an empty viewer
- **THEN** the first imported record receives local `seq` 1

#### Scenario: Repeated import
- **WHEN** two files with overlapping `seq` values are imported in sequence
- **THEN** later records receive larger local sequences and no collision occurs

### Requirement: Import is bounded and fault-tolerant

The viewer SHALL read JSONL line by line with a maximum line size, SHALL wait for an incomplete last line, SHALL count and skip malformed lines, and SHALL keep importing after an error.

#### Scenario: Malformed line
- **WHEN** one line is invalid JSON
- **THEN** the malformed counter grows, the line is skipped, and the next valid line is imported

#### Scenario: Partial last line
- **WHEN** the file ends in the middle of a line
- **THEN** the importer waits for completion or reports the incomplete line at end of file

#### Scenario: Oversized line
- **WHEN** a line exceeds the maximum line size
- **THEN** it is rejected and counted without exhausting memory

#### Scenario: Cancel
- **WHEN** the operator cancels an import
- **THEN** already imported records remain available with a cancelled-import banner

### Requirement: Export selection is explicit

`Save selected` SHALL export selected Trace records in sequence order or the latest record of each selected Overwrite key; exporting all retained frames for selected keys SHALL be a separate action.

#### Scenario: Trace selection
- **WHEN** three Trace records are selected and saved
- **THEN** the file contains those three records in sequence order

#### Scenario: Overwrite selection
- **WHEN** two Overwrite keys are selected and saved
- **THEN** the file contains the latest record for each key

#### Scenario: All frames for keys
- **WHEN** the all-frames action is used for selected keys
- **THEN** every retained frame matching those keys is exported in sequence order

### Requirement: Export is asynchronous and atomic

Export SHALL run on a low-priority writer fed by a bounded queue, SHALL report progress and cancellation, SHALL reject a second concurrent export, and SHALL publish the target only on success.

#### Scenario: UI remains responsive
- **WHEN** a large selection is exported
- **THEN** the UI continues to drain capture and render while progress is shown

#### Scenario: Cancel
- **WHEN** the export is cancelled
- **THEN** the partial file is removed and the cancelled status is shown

#### Scenario: Success
- **WHEN** the export completes
- **THEN** the temporary file is renamed to the target path

#### Scenario: Concurrent export
- **WHEN** an export is active and another is requested
- **THEN** the second request is rejected with a visible message

#### Scenario: Evicted before enqueue
- **WHEN** a selected record is evicted before it is copied to the export queue
- **THEN** it is counted as skipped and reported

### Requirement: CSV and TXT are export-only tabular formats

The viewer SHALL export CSV with correct quoting and a header row, and TXT with fixed columns; these formats SHALL NOT be treated as offline sessions.

#### Scenario: CSV quoting
- **WHEN** a decoded value contains a comma, quote, or newline
- **THEN** the CSV field is quoted and escaped correctly

#### Scenario: TXT missing value
- **WHEN** an optional value is absent
- **THEN** the TXT column contains a placeholder

#### Scenario: CSV import is rejected
- **WHEN** a CSV file is passed to the offline session opener
- **THEN** the viewer reports that CSV is not a session format

### Requirement: Offline replay reconstructs derived views

The viewer SHALL rebuild history, filters, Overwrite rows, CANopen associations, heatmap, bit activity, and time presentation from an imported session without opening COM services.

#### Scenario: Offline heatmap
- **WHEN** an imported session contains bitrates and BusIdle records
- **THEN** the heatmap shows measured or estimated load according to the imported data

#### Scenario: Offline CANopen
- **WHEN** an imported session contains CANopen frames and either EDS data or exported decoded text
- **THEN** decoded node/service/signals are available

#### Scenario: Live status is unavailable
- **WHEN** the viewer is offline
- **THEN** live channel status is shown as unavailable rather than as a connection error

### Requirement: Mixed sessions are detectable

The viewer SHALL expose the session boot token and SHALL warn when imported records span more than one boot session.

#### Scenario: Mixed boot tokens
- **WHEN** imported records carry different boot tokens
- **THEN** the viewer warns that monotonic deltas are not comparable across sessions

#### Scenario: Single boot token
- **WHEN** all records share one boot token
- **THEN** no mixed-session warning is shown

### Requirement: Session vocabulary is diagnosable and serializable

Every public session enum and struct SHALL provide a canonical string representation, a `std::formatter`, and non-throwing JSON serialization and deserialization.

#### Scenario: Record-type enum
- **WHEN** `CanViewRecordType` values are formatted or serialized
- **THEN** they produce `frame`, `gap`, or `status` and round-trip through JSON without throwing

#### Scenario: Session header
- **WHEN** a `CanViewSessionHeader` is serialized and parsed back
- **THEN** its format, version, boot token, channels, and EDS bindings survive the round-trip

#### Scenario: Frame record
- **WHEN** a `CanViewFrameRecord` is serialized and parsed back
- **THEN** its hardware time, channel, identifier, flags, and payload survive the round-trip

#### Scenario: Gap record
- **WHEN** a `CanViewGapRecord` is serialized and parsed back
- **THEN** its reason, count, and reference time survive the round-trip
