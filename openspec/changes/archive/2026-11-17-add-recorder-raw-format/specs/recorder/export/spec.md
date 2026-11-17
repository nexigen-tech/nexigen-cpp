# Spec Delta

## Purpose

Defines `nexigen-export`, the on-demand converter for raw recorder sessions: session inspection, time-range and channel selection, schema-driven decoding, MCAP/JSONL/Perfetto output, elementary video extraction from `.nxvid` (MP4/MKV remux is deferred until `nexigen_media` exists), integrity verification, and the absence of any background or live-follow behavior.

## ADDED Requirements

### Requirement: On-demand conversion only

The converter SHALL run as a single invocation over a completed or currently closed session and SHALL exit when the requested output is written. It SHALL NOT run as a daemon and SHALL NOT implement background, live-tail, `--follow`, or sidecar conversion.

#### Scenario: One invocation converts and exits

- **WHEN** the user runs `nexigen-export` with an input session and an output path
- **THEN** the process reads the requested data, writes the output, and exits without leaving a background process

#### Scenario: Live-follow is not offered

- **WHEN** the user requests follow, watch, or live conversion
- **THEN** the command rejects the option and explains that conversion is on demand

#### Scenario: Unclosed session is explicit

- **WHEN** the active segment has no footer
- **THEN** the converter reads the intact prefix, reports the unclosed tail, and does not claim the session is complete

### Requirement: Session inspection

The converter SHALL inspect a session from `session.json` and the segment indexes without reading video payloads. It SHALL report format version, session time domain, segments, schemas, channels, codecs, record counts, gap counts, and closed/unclosed state.

#### Scenario: Inspect reports the registry

- **WHEN** the user inspects a valid session
- **THEN** the output names every schema, channel, codec, and segment state

#### Scenario: Inspect does not scan video bytes

- **WHEN** a session contains multi-gigabyte `.nxvid` files
- **THEN** inspection completes without reading the frame payload stream

### Requirement: Dataset inventory

The converter SHALL list every dataset in a session: telemetry channels, opaque channels, and video streams. Each entry SHALL include a stable dataset id, topic or name, kind, schema id/name/encoding, video codec/bitstream when applicable, segment list, record or frame count, payload byte size, first/last event time, and clean/unclosed state. The inventory SHALL be available as human-readable text and JSON.

#### Scenario: All datasets are listed

- **WHEN** a session contains logs, metrics, an opaque sensor channel, and two video streams
- **THEN** the list contains five datasets with their ids, kinds, schemas, counts, sizes, and time ranges

#### Scenario: JSON inventory is machine-readable

- **WHEN** the user requests JSON output
- **THEN** the inventory is a structured document with one entry per dataset and no prose mixed into numeric fields

### Requirement: Per-dataset information

For a selected dataset the converter SHALL report payload bytes, record/frame count, first/last event time, first/last record offset, per-segment path/size/time/count, and the schema's fields and arrays with name, type, units, and offset. For video it SHALL additionally report codec, bitstream, dimensions, extradata size, and `config_epoch` values. It SHALL compute this from the manifest, index, and record headers without reading payload bytes.

#### Scenario: Telemetry dataset information

- **WHEN** the user requests information for a telemetry dataset
- **THEN** the output reports size, start/end time, record range, segment breakdown, and the schema's fields and arrays

#### Scenario: Video dataset information

- **WHEN** the user requests information for a video stream
- **THEN** the output reports frame count, byte size, time range, codec, bitstream, dimensions, extradata size, and configuration epochs

#### Scenario: Info does not read payloads

- **WHEN** a dataset contains multi-gigabyte payloads
- **THEN** the information command completes by reading metadata and record headers only

### Requirement: Dataset selection and extraction

The converter SHALL accept a dataset id, topic, or name for extraction, combined with a time range and output format. It SHALL decode and emit only the selected datasets, and SHALL fail with a named error for an unknown dataset.

#### Scenario: Extract one dataset

- **WHEN** the user selects one dataset by id and requests JSONL
- **THEN** the output contains only that dataset's records in the requested interval

#### Scenario: Unknown dataset is rejected

- **WHEN** the requested id or name is not present in the inventory
- **THEN** the converter exits non-zero and lists the available dataset ids

### Requirement: Self-contained decoding

The converter SHALL decode a session using only the session and the converter binary. Every schema descriptor, channel declaration, codec extradata, unit, endianness, time calibration, and dataset identity SHALL be stored in the session; the converter SHALL NOT require external schema files, project configuration, environment variables, a producer SDK, or network access.

#### Scenario: Convert a copied session

- **WHEN** a session directory is copied to a clean machine with only the converter binary
- **THEN** list, info, and extract all work without additional files or configuration

#### Scenario: Missing metadata is explicit

- **WHEN** a selected dataset lacks a schema or codec descriptor in the session
- **THEN** the converter exits non-zero and names the missing metadata

#### Scenario: No external lookup

- **WHEN** the converter decodes a built-in or opaque dataset
- **THEN** it resolves the schema from the session registry and does not read a schema path, URL, or environment variable

### Requirement: Time-range and channel selection

The converter SHALL accept start/end timestamps and optional channel patterns and SHALL use the sparse index to skip records outside the interval. The selected output SHALL contain only matching records and the declared schemas they reference.

#### Scenario: Indexed time window

- **WHEN** the user requests a window inside a multi-segment session
- **THEN** the converter seeks through `.nxidx` and reads only the matching `.nxrec` byte ranges

#### Scenario: Channel filter

- **WHEN** the user selects one video channel and one telemetry channel
- **THEN** other channels are excluded from the output

#### Scenario: Empty selection is explicit

- **WHEN** the requested interval contains no records
- **THEN** the converter reports an empty result and exits successfully

### Requirement: Schema-driven decoding

The converter SHALL decode platform frames through built-in descriptors and opaque channels through the embedded schema/channel registry. It SHALL fail with a diagnostic when a required schema is missing or its encoding is unsupported, and SHALL NOT guess a payload layout.

#### Scenario: Built-in frame decodes

- **WHEN** a `LogFrame`, `MetricFrame`, or `SpanFrame` record is selected
- **THEN** the converter decodes it with the built-in descriptor and emits the requested representation

#### Scenario: Registered opaque channel decodes

- **WHEN** an opaque channel declares a JSON Schema, Protobuf descriptor, or FlatBuffers schema
- **THEN** the converter uses that declaration for the output representation

#### Scenario: Unknown schema is an error

- **WHEN** a selected channel references a schema that is absent from the registry
- **THEN** the converter exits non-zero and names the channel and schema id

### Requirement: MCAP output

The converter SHALL be able to emit an MCAP file with schemas, channels, messages, and timestamps. Built-in frames SHALL be written with a generated JSON Schema and JSON message payload; registered opaque channels SHALL retain their declared schema and message encoding when MCAP can represent it. Timestamps SHALL be converted to UTC epoch nanoseconds when the session provides calibration.

#### Scenario: Telemetry becomes MCAP

- **WHEN** the user exports selected telemetry channels to MCAP
- **THEN** the output contains schemas, channels, and JSON messages readable by external tools

#### Scenario: Opaque encoding is preserved

- **WHEN** a channel declares a Protobuf or FlatBuffers message encoding
- **THEN** the MCAP channel preserves that schema and encoding and the payload bytes

#### Scenario: UTC timestamps

- **WHEN** the session has `TimeSync` calibration and the output is MCAP
- **THEN** message timestamps are UTC epoch nanoseconds derived from the recorded calibration

### Requirement: JSONL and Perfetto output

The converter SHALL emit newline-delimited JSON for selected records and Chrome/Perfetto trace JSON for spans, including explicit gap objects at their sequence positions.

#### Scenario: JSONL includes common fields

- **WHEN** records are exported to JSONL
- **THEN** each line carries the record type, channel, sequence, capture/commit time, and the decoded payload or an opaque representation

#### Scenario: Gap object is explicit

- **WHEN** a gap record is encountered
- **THEN** the output contains a gap object with first lost sequence, lost count, and reason at the correct position

#### Scenario: Perfetto spans

- **WHEN** span records are exported to Perfetto
- **THEN** the output contains complete-duration events with process/thread metadata and timestamps in microseconds

### Requirement: Video extraction to elementary streams

The converter SHALL extract stored video by reading `VideoFrameRef` records and the corresponding `.nxvid` byte ranges. It SHALL write the codec-native elementary stream and a sidecar timestamp manifest containing capture time, source sequence, `config_epoch`, size, and offset for each frame. It SHALL NOT decode, re-encode, or parse the bitstream. MP4/MKV output SHALL fail with `UnsupportedFormat` in this revision because `nexigen_media` is not part of the change.

#### Scenario: H.265 elementary stream

- **WHEN** an H.265 video stream is extracted
- **THEN** the output contains the original stored bitstream bytes and a sidecar with capture-derived timestamps and frame metadata

#### Scenario: RAW or JPEG XS extraction

- **WHEN** an uncompressed or JPEG XS stream is extracted
- **THEN** the output contains the original frame bytes and the sidecar metadata without muxing

#### Scenario: MP4/MKV is explicitly deferred

- **WHEN** the user requests MP4 or MKV output
- **THEN** the converter exits non-zero with `UnsupportedFormat` and names the missing `nexigen_media` follow-up

#### Scenario: Frame integrity is checked

- **WHEN** per-frame CRC is enabled
- **THEN** extraction fails with a named error if a frame's bytes do not match its reference CRC

### Requirement: Integrity verification

The converter SHALL verify per-record CRC-32C and, when enabled, per-frame video CRC; it SHALL compare counts with the manifest and report truncated or unclosed segments. A mismatch SHALL produce a non-zero exit status and an explicit diagnostic.

#### Scenario: Record CRC mismatch

- **WHEN** a `.nxrec` record fails its CRC check
- **THEN** the converter reports the segment, offset, and channel and exits non-zero

#### Scenario: Video CRC mismatch

- **WHEN** per-frame CRC is enabled and a `.nxvid` frame fails it
- **THEN** the converter reports the frame reference and exits non-zero

#### Scenario: Clean session verifies

- **WHEN** every record and enabled frame CRC matches and every segment is closed
- **THEN** verification succeeds and reports total record and frame counts

### Requirement: CLI contract

The converter SHALL expose `list`, `info`, `extract`, `inspect`, `jsonl`, `perfetto`, `video`, and `verify` subcommands with `--dataset`, `--start`, `--end`, `--channels`, `--output`, `--format`, and `--json`. `--help` SHALL exit zero; invalid arguments and unsupported requests SHALL exit non-zero with a diagnostic on stderr.

#### Scenario: List is available

- **WHEN** the user runs `nexigen-export list <session>`
- **THEN** the command prints the dataset inventory and exits zero

#### Scenario: Info needs a dataset

- **WHEN** the user runs `nexigen-export info <session> --dataset <id>`
- **THEN** the command prints that dataset's size, time range, segment breakdown, and schema fields

#### Scenario: Extract needs a selection

- **WHEN** the user runs `nexigen-export extract <session> --dataset <id> --format jsonl --output out.jsonl`
- **THEN** the command writes only that dataset's selected records

#### Scenario: Help is successful

- **WHEN** the user runs `nexigen-export --help`
- **THEN** the command prints usage and exits zero

#### Scenario: Invalid arguments fail

- **WHEN** an unknown subcommand, malformed timestamp, or missing output path is supplied
- **THEN** the command exits non-zero and names the offending argument

#### Scenario: Unsupported format is controlled

- **WHEN** a requested output format cannot represent the selected codec or schema
- **THEN** the command exits non-zero with `UnsupportedFormat` and leaves no partial output file

### Requirement: Export type serialization

`ExportFormat`, `VideoExportFormat`, `ExportSelection`, `DatasetKind`, `DatasetInfo`, and `DatasetSegmentInfo` SHALL provide canonical `to_string`, `std::formatter`, and ADL `to_json`/`from_json` under `JSON_NOEXCEPTION=1`.

#### Scenario: Export formats round-trip

- **WHEN** every `ExportFormat` and `VideoExportFormat` value passes through `to_string`, `from_string`, JSON, and the formatter
- **THEN** every canonical spelling round-trips

#### Scenario: Selection round-trips

- **WHEN** an `ExportSelection` with start/end times and channel patterns is serialized and parsed back
- **THEN** the interval and channel list survive unchanged

#### Scenario: Dataset information round-trips

- **WHEN** a `DatasetInfo` with `DatasetSegmentInfo` entries and a `DatasetKind` is serialized and parsed back
- **THEN** the id, kind, schema, counts, byte sizes, time range, and segment entries survive unchanged
