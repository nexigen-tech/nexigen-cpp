# Design

## Context

See `proposal.md` for motivation. Constraints that shape the implementation:

- The benchmark measured the custom prototype at 0.8–1.6 GB/s for telemetry, allocation-free, and `.nxvid` FullHD 16 bpp 50 Hz at 3.5% of one writer core without per-frame CRC (5.1% with it).
- MCAP’s C++ writer allocates schema/channel/index/statistics state and contains a throwing `FileWriter`; it is therefore excluded from the recorder hot path and confined to the converter.
- The project builds with `-fno-exceptions -fno-rtti`, forbids heap allocation in runtime loops, and requires `core::Result`/`std::expected` error handling.
- The recorder storage library must be usable without COM and without media; the converter may depend on MCAP and optionally FFmpeg.
- No recorder module exists yet; this change creates it and the converter.
- The storage/export contracts need `UnsupportedFormat`, `NotFound`, and `CorruptData`; the core `ErrorCode` vocabulary is extended append-only, keeping existing values and spellings unchanged.

## Goals / Non-Goals

**Goals:**

- A preallocated, single-writer raw storage path with no heap activity, no JSON, and no `std::string` in the measured loop.
- CRC-32C on every telemetry/metadata record; optional per-frame video CRC; footer CRC.
- Self-describing segments through an embedded schema/channel registry.
- A one-shot, on-demand converter with no daemon, no follow, and no background work.
- A dataset inventory and per-dataset size/time/byte-range report that requires no payload scan.
- A fully self-contained session: copying the session and the converter is enough to list, inspect, and decode it.
- A clear extension path: `.nxvid` and `VideoFrameRef` are first-class now, and future independent video rotation is already representable.

**Non-Goals:**

- No ingestion daemon, encoder service, lifecycle/SM/PHM integration, or COM subscription in this change.
- No live tailing or background conversion.
- No MCAP or FFmpeg linkage in the recorder library.
- No compression of `.nxrec`; no bitstream parsing of video.
- No producer-published runtime schemas in v1; config-provided schemas only.

## Decisions

### D1: Raw triplet and session manifest

```text
<session_root>/<session_id>/
  session.json
  <session>_000000.nxrec
  <session>_000000.nxidx
  <session>_000000.nxvid
  ...
```

`session.json` is cold-path data: format version, session id, time domain, storage settings, schema table, channel table, segment catalog, and clean-close state. It is written with temp-file + `rename` + directory `fsync` after each rotation.

**Alternative:** one monolithic file per session. Rejected: retention, recovery, and video alignment are simpler with bounded segments.

### D2: Exact wire layouts

All wire structs are little-endian, trivially copyable, and fixed-size.

```text
RecordHeader (48 B, alignas(8))
  0  u32 magic              "RCD1"
  4  u16 type
  6  u16 flags
  8  u32 channel_id
  12 u32 payload_size
  16 u64 commit_mono_ns
  24 u64 sequence
  32 u64 publisher_id
  40 u32 header_crc32c      CRC [0..39]
  44 u32 payload_crc32c

IndexEntry (64 B, alignas(8))
  0  u64 commit_ns
  8  u64 rec_offset
  16 u64 min_event_ns
  24 u64 max_event_ns
  32 u64 channel_mask
  40 u64 end_rec_offset
  48 u32 record_count
  52 u32 flags
  56 u32 entry_crc32c       CRC [0..55]
  60 u32 reserved

VideoFrameRef (64 B, alignas(8))
  0  u64 vid_offset
  8  u64 capture_mono_ns
  16 u64 source_sequence
  24 u64 config_epoch
  32 u32 vid_size
  36 u32 vid_crc32c         0 = no per-frame CRC
  40 u32 width
  44 u32 height
  48 u16 codec
  50 u16 bitstream
  52 u32 vid_segment_seq
  56 u64 reserved

VideoStreamConfig (1056 B, alignas(8))
  0  u16 codec
  2  u16 bitstream
  4  u32 width
  8  u32 height
  12 u32 timebase_num
  16 u32 timebase_den
  20 u16 extradata_size
  22 u16 reserved
  24 u64 config_epoch
  32 u8  extradata[1024]

SegmentFooter (64 B, alignas(8))
  0  u32 magic              "FTR1"
  4  u32 version
  8  u64 total_records
  16 u64 total_gaps
  24 u64 rec_bytes
  32 u64 vid_bytes
  40 u64 index_entries
  48 u64 end_commit_ns
  56 u32 footer_crc32c
  60 u32 magic_end
```

`SchemaDeclare` and `ChannelDeclare` are bounded TLV payloads (max 256 KiB schema, 64 schemas and 64 channels per session). Channel ids are immutable: a schema change creates a new channel id.

**Alternative:** variable-length records with no fixed headers. Rejected: the fixed header keeps recovery and indexing O(1) per record and matches the measured prototype.

### D3: CRC-32C coverage and acceleration

- Every `.nxrec` record: header CRC over bytes `[0..39]`, payload CRC over the payload. Both must match before a record is exposed.
- `.nxvid`: per-frame CRC optional, default off; when on it is copied into `VideoFrameRef.vid_crc32c`.
- `SegmentFooter`: CRC over its metadata; `IndexEntry`: CRC over its interval metadata.
- A recorder-local `detail/crc32c` helper uses SSE4.2 or ARMv8 CRC instructions when available and falls back to `core::integrity::crc32c`. Both produce the Castagnoli checksum.
- The benchmark measured per-frame CRC over 128 KiB frames at ~12–13 GB/s; FullHD 50 Hz per-frame CRC adds ~1.6% of one core, so it stays optional.

### D4: Preallocation and writer ownership

- One writer thread owns `.nxrec`, `.nxidx`, and `.nxvid`; append order and index entries are produced there.
- Buffers sized at session start: record batch (default 1 MiB), index batch (default 64 KiB), video scratch (default 4 MiB or `max_frame_bytes`), schema/channel slots.
- Producers hand records to the writer through per-thread SPSC shards: power-of-two capacity (default 1024 slots), `alignas(64)` producer/consumer indices, local index `relaxed`, handoff `release`/`acquire`; no mutex, no CAS.
- A full shard drops the sample and emits a `GapMarker` with `IoBackpressure`; it never blocks the producer. The shard path carries envelopes up to 4 KiB; larger records bypass the shard and call `append()` directly.
- If a configured buffer or slot count is insufficient, initialization fails before recording; the write loop never grows a container.
- No JSON, `std::string`, `std::vector` growth, or `new`/`delete` in the write loop.

**Alternative:** lock-free MPSC directly in the writer. Rejected: per-thread SPSC shards avoid cross-thread CAS and are already the project pattern.

### D5: Schema and channel registry

- Built-in descriptors for `LogFrame`, `MetricFrame`, `SpanFrame`, `GapMarker`, `TimeSync`, `VideoStreamConfig`, and `VideoFrameRef`; each descriptor is checked with `static_assert(offsetof/sizeof)`. `ChannelDeclare` is a manifest/wire type, not a built-in payload schema.
- Opaque channels load a schema from configuration at startup (path + SHA-256, bounded size, encoding whitelist) and **embed the full descriptor bytes** in the session. The recorder never parses the schema on the hot path; it stores bytes and metadata. No external path, URL, or producer SDK is needed later.
- Every segment begins with its schema/channel declarations; `session.json` mirrors them. A reader can open a single segment without the manifest.
- The converter uses the registry; an unknown schema is a hard error, never a guessed layout.

### D6: Time model

- `RecordHeader.commit_mono_ns` is the writer's `CLOCK_MONOTONIC_RAW` time.
- Event time remains in the payload (`LogFrame.timestamp_ns`, `VideoFrameRef.capture_mono_ns`, etc.) or in the record type's documented field.
- `IndexEntry.min_event_ns`/`max_event_ns` bound the event times of each interval, so sparse lookup works even if events arrive out of order.
- `TimeSync` records and the segment header offset provide UTC conversion; the converter uses them. No wall-clock reads in the writer.
- `publisher_id` and 64-bit `sequence` live in every header. A new publisher id or a backwards sequence starts a new epoch; gaps within an epoch emit `GapMarker`.

### D7: Video storage

- `.nxvid` frames are 4 KiB aligned; no header per frame, only the blob header and aligned padding.
- `VideoFrameRef` is appended to `.nxrec` after the frame bytes are durable in `.nxvid` order; a crash may leave an unreferenced tail, which recovery ignores.
- `VideoStreamConfig` is written before the first frame of a stream and on every `config_epoch` change.
- Video CRC is optional; the footer CRC is always present.
- The recorder does not link `nexigen_media` and never parses the bitstream.
- MP4/MKV remux is deferred until `nexigen_media` exists; the v1 converter writes elementary streams plus a timestamp sidecar.

### D8: On-demand converter

- `nexigen-export` is a one-shot CLI. It opens `session.json`, the segment list, and the index; it does not watch, poll, or write sidecars.
- Reads use `pread` over the sparse index intervals; `.nxvid` reads use `VideoFrameRef` ranges.
- Built-in frames are converted with their `to_json`/descriptor; opaque channels use their declared schema/encoding.
- MCAP output uses JSON Schema + JSON messages for built-ins and pass-through for opaque encodings. Video extraction writes the stored elementary stream and a timestamp sidecar; MP4/MKV remux is a follow-up after `nexigen_media`.
- MCAP C++ is pinned and confined to the converter target. Its exception-enabled implementation TU is isolated; the converter catches at the boundary. The recorder never links it.

### D9: Dataset inventory and self-contained decoding

- A **dataset** is a logical array of records: one telemetry channel, one opaque channel, or one video stream. Its stable id is the channel id plus kind; its name is the topic or stream name.
- `list` reports every dataset: id, name/topic, kind, schema id/name/encoding, video codec/bitstream, segments, record/frame count, payload bytes, first/last event time, and closed state.
- `info` reports one dataset: payload bytes, count, first/last event time, first/last record offset, per-segment sizes/times/counts, schema fields and arrays (name/type/units/offset), and video codec/dimensions/extradata/epochs. It derives the data from `session.json`, `.nxidx`, and record headers; it never reads payload bytes.
- `extract` accepts a dataset id/name/topic plus time range and output format and emits only the selected dataset.
- The session is the complete decoding package: full schema descriptor bytes, channel declarations, codec extradata, units, endianness, time calibration, and dataset identities are inside it. The converter resolves everything from the session and its own built-in decoders; it never reads a schema path, URL, environment variable, producer SDK, or network resource.
- Supported schema encodings are limited to what the converter can decode generically: built-in platform descriptors, JSON Schema/JSON, Protobuf `FileDescriptorSet`, FlatBuffers `.bfbs`, and pass-through encodings. A missing descriptor or unsupported encoding is a named error, not a guess.
- The converter may allocate, use `std::string`/JSON, and load MCAP/FFmpeg; it is offline and not on the recorder hot path.

### D10: Rotation, retention, recovery

- Rotation triggers: `max_segment_bytes` (sum of rec + vid), `max_segment_duration`, `max_segment_records`; a record never spans segments.
- Retention `circular` deletes only closed triplets; `stop` finalizes the active segment and halts; `min_free_bytes` blocks a new segment.
- Recovery scans `.nxrec` from the start, validates CRC, stops at the first invalid/incomplete record, and rebuilds `.nxidx` in memory. A missing/truncated index is not fatal; ranged reads rebuild it on demand. Recovered bounds use commit time for record types without a documented event field. A clean footer validates the close.

### D11: Build boundaries

- `nexigen_recorder`: core + chrono only; no COM, no media, no MCAP. Contains wire structs, registry, writer, reader, settings, session manifest.
- `nexigen-export`: `nexigen_recorder` + optional `nexigen_media` + optional MCAP C++ (converter-only object library).
- Tests: recorder tests are COM-free; converter tests are optional/gated with MCAP/media.
- No install/export of benchmark-only or prototype targets.

## Risks / Trade-offs

- **Preallocation caps** → validate `max_record_bytes`, `max_frame_bytes`, schema/channel counts at startup; fail before recording.
- **CRC cost** → hardware path; video CRC off by default; measure telemetry CRC in the latency gate.
- **Schema registry growth** → fixed 64/64 slots; reject extra schemas/channels with a clear error.
- **Converter schema fidelity** → static descriptors with layout assertions; opaque schemas are untrusted input, so bound and fuzz the reader. Generic decoders are limited to the encodings the converter supports; an unknown descriptor is a named error.
- **Dataset inventory without payload scan** → compute counts, bytes, and time ranges from the manifest, index entries, and record headers; never read `.nxvid` payloads for `list`/`info`.
- **Self-contained session** → embed full schema bytes and codec extradata at record time; test by copying a session to a clean directory with only the converter binary.
- **MCAP converter exceptions/allocations** → isolated to the converter, never the recorder; the converter may allocate and use `std::string`/JSON.
- **Converter cannot follow live** → deliberate; users convert after stop or from the intact prefix of an unclosed session.
- **Video codec coverage** → remux is codec/container dependent; unsupported combinations fail before output; JPEG XS/RAW may require MKV/elementary streams.
- **Session deletion during conversion** → the one-shot converter snapshots the segment list; Linux keeps open descriptors valid if retention deletes a file.
- **Cross-tool readability** → raw files are not directly readable; `nexigen-export` is the only boundary. MCAP JSON output is the tool-friendly path.

## Migration Plan

This is a new module and tool; no existing data or APIs migrate. The benchmark change remains the evidence source. The first recorder revision writes raw sessions; a later change adds the ingestion daemon and encoder service. Rollback is deleting the new module/tool; no production target changes.

## Open Questions

- Whether MCAP output should also embed video as `foxglove.CompressedVideo` in v1 or leave video to MP4/MKV; the current spec allows telemetry MCAP plus video remux, and embedding can be added without changing the raw format.
- Whether producer-published runtime schemas (instead of config-provided) are needed; the registry shape already allows them later.
