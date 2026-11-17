# Spec Delta

## Purpose

Defines the recorder's video storage: 4 KiB-aligned `.nxvid` frames referenced from `.nxrec`, the video stream declaration and frame reference records, optional per-frame CRC, capture/source-sequence preservation, and video gap reporting.

## ADDED Requirements

### Requirement: `.nxvid` blob layout

Each segment's `.nxvid` SHALL start at a 4 KiB-aligned boundary and SHALL store each compressed frame at a 4 KiB-aligned offset. Frames SHALL be appended without reinterpretation; the recorder SHALL NOT decode, re-encode, or parse the bitstream.

#### Scenario: Every frame offset is aligned

- **WHEN** frames are written to `.nxvid`
- **THEN** every recorded frame offset is a multiple of 4096

#### Scenario: Frame bytes are preserved

- **WHEN** an encoded frame is committed
- **THEN** the bytes in `.nxvid` are identical to the encoded payload

#### Scenario: Large frame still fits

- **WHEN** a frame exceeds the configured write buffer
- **THEN** the writer flushes the buffer and writes the frame without splitting it across records

### Requirement: Video frame references

Every `.nxvid` frame SHALL have a corresponding `VideoFrameRef` record in `.nxrec` carrying the video segment sequence, byte offset, byte length, optional CRC-32C, codec, bitstream, dimensions, capture time, source sequence, and `config_epoch`.

#### Scenario: Reference locates the frame

- **WHEN** a reader reads a `VideoFrameRef`
- **THEN** it can read exactly that byte range from the companion `.nxvid` and verify it when the CRC is present

#### Scenario: Reference identifies its segment

- **WHEN** a session contains multiple video segments
- **THEN** each reference names the `.nxvid` segment sequence so the correct blob is opened

#### Scenario: Reference preserves capture time

- **WHEN** an encoded frame carries `capture_mono_ns`
- **THEN** the reference carries the same instant without substituting the recorder commit time

### Requirement: Video stream declarations

Before the first frame of a stream and after every `config_epoch` change, the recorder SHALL write a `VideoStreamConfig` containing codec, bitstream format, width, height, timebase, and codec extradata. No frame of a new epoch SHALL precede its declaration.

#### Scenario: First frame is preceded by a declaration

- **WHEN** the first frame of a video stream is committed
- **THEN** `VideoStreamConfig` is already in `.nxrec` and readers can remux without parsing keyframes

#### Scenario: Encoder restart starts an epoch

- **WHEN** the encoder restarts or changes codec parameters, bitstream format, or extradata
- **THEN** a new declaration with a new `config_epoch` is written before the next frame

### Requirement: Self-contained video codec configuration

`VideoStreamConfig` SHALL embed everything needed to remux a stream: codec, bitstream format, dimensions, timebase, `config_epoch`, and codec extradata. The converter SHALL NOT require the encoder, a producer SDK, an external codec configuration file, or the local environment to interpret the stream.

#### Scenario: Remux from a copied session

- **WHEN** a session is copied to a machine that has only the converter
- **THEN** the converter remuxes the recorded stream using the embedded declaration and frame references

#### Scenario: Missing extradata is explicit

- **WHEN** a stream requires extradata that is not present in `VideoStreamConfig`
- **THEN** the converter exits non-zero and names the stream and missing configuration instead of scanning or guessing

### Requirement: Optional per-frame CRC

Per-frame CRC-32C for `.nxvid` SHALL be configurable and SHALL default to off. When enabled, the reference SHALL carry the CRC of the exact frame bytes. The segment footer SHALL always carry a CRC over its recorded metadata.

#### Scenario: CRC off is the default

- **WHEN** the configuration omits the video CRC setting
- **THEN** frames are written without per-frame CRC and the benchmarked fast path is used

#### Scenario: CRC on is verified

- **WHEN** per-frame CRC is enabled
- **THEN** the reference carries a CRC-32C and a reader rejects a frame whose bytes do not match

#### Scenario: Footer remains covered

- **WHEN** per-frame CRC is off
- **THEN** the segment footer still carries its own CRC over the recorded metadata

### Requirement: Capture time and source sequence preservation

The recorder SHALL preserve the encoder-provided `capture_mono_ns`, 64-bit `source_sequence`, and `config_epoch` for every frame. Storage SHALL NOT renumber frames; the reference carries the original values.

#### Scenario: Capture time is not replaced

- **WHEN** a frame is recorded
- **THEN** its capture time in `VideoFrameRef` equals the encoder-provided capture time

#### Scenario: Source sequence survives

- **WHEN** a frame with a 64-bit source sequence is recorded
- **THEN** the reference carries the full 64-bit value

#### Scenario: Config epoch groups frames

- **WHEN** a reader filters one encoder configuration
- **THEN** it groups frames by `config_epoch` without decoding payloads

### Requirement: Video gap reporting

When a source sequence discontinuity is detected within one `config_epoch`, the recorder SHALL emit a gap record containing the first lost sequence, lost count, and reason. A new `config_epoch` SHALL NOT be reported as a gap.

#### Scenario: Lost frames produce a gap record

- **WHEN** the source sequence jumps from N to N+3 within one epoch
- **THEN** a gap record reports `first_lost_sequence = N+1` and `lost_count = 2`

#### Scenario: Encoder restart is not a gap

- **WHEN** a new `config_epoch` begins with a reset source sequence
- **THEN** the reader sees the new declaration and no artificial multi-frame gap

### Requirement: Storage-only video responsibility

The recorder SHALL accept already-encoded frames and SHALL NOT encode, decode, transcode, or parse them. Codec selection and All-Intra encoding belong to the encoder service; video remuxing belongs to the converter.

#### Scenario: Encoder output is opaque bytes

- **WHEN** a frame arrives from the encoder service
- **THEN** the recorder writes its bytes and metadata without inspecting the bitstream

#### Scenario: No media dependency

- **WHEN** the recorder is built with `NEXIGEN_ENABLE_MEDIA=OFF`
- **THEN** video recording still works for every codec tag the encoder publishes

### Requirement: Video storage type serialization

`VideoCodecTag`, `BitstreamFormat`, `VideoCrcMode`, `VideoStreamConfig`, `VideoFrameRef`, and `VideoGapReason` SHALL provide canonical `to_string`, `std::formatter`, and ADL `to_json`/`from_json` under `JSON_NOEXCEPTION=1`.

#### Scenario: Video enums round-trip

- **WHEN** every `VideoCodecTag`, `BitstreamFormat`, `VideoCrcMode`, and `VideoGapReason` value passes through `to_string`, `from_string`, JSON, and the formatter
- **THEN** every canonical spelling round-trips

#### Scenario: Stream configuration round-trips

- **WHEN** a `VideoStreamConfig` with extradata is serialized and parsed back
- **THEN** codec, bitstream, dimensions, timebase, `config_epoch`, and extradata survive unchanged

#### Scenario: Frame reference round-trips

- **WHEN** a `VideoFrameRef` is serialized and parsed back
- **THEN** its segment sequence, offset, size, CRC, capture time, source sequence, and `config_epoch` survive unchanged
