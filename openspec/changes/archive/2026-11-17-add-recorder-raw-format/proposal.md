# Proposal

## Why

The storage-format benchmark showed that the custom raw design is the only candidate that keeps the recording path allocation-free and under the project's `-fno-exceptions` mandate: MCAP's writer allocates schema/channel/index state and needs an exceptions-enabled wrapper, while the custom prototype sustained 0.8–1.6 GB/s for telemetry and 3.5% of one writer core for FullHD 16 bpp 50 Hz video. The recorder therefore keeps a fast raw format on the hot path, covers every record with CRC-32C (video CRC optional), and moves all interpretation to an on-demand converter that can emit MCAP, JSONL, Perfetto, or MP4/MKV when a user asks for it.

## What Changes

- Add the `recorder` raw storage module (COM-free, media-free): `session.json`, append-only `.nxrec`, sparse `.nxidx`, and 4 KiB-aligned `.nxvid`.
- Add append-only `core::ErrorCode` values needed by the storage/export contracts (`UnsupportedFormat`, `NotFound`, `CorruptData`); existing values and spellings are unchanged.
- Add a single-writer, preallocated recording path: all record, index, video, and schema buffers are sized at session start; no heap, no JSON, no strings, and no MCAP in the write loop.
- Cover every telemetry and metadata record with CRC-32C over header and payload. Cover video frames with an optional per-frame CRC (default off) and always cover segment footers. Use a hardware CRC-32C path with a table fallback.
- Embed a schema and channel registry in each segment (built-in descriptors for platform frames, config-provided schemas for opaque channels) and mirror it in `session.json`. Every schema's full descriptor, channel declaration, codec extradata, units, endianness, and time calibration SHALL be stored in the session, so a copied session plus the converter is sufficient for decoding; no external schema files, producer SDK, or configuration are required.
- Store video as `VideoFrameRef` records in `.nxrec` pointing into `.nxvid`; carry capture time, source sequence, `config_epoch`, codec, bitstream, dimensions, and extradata via `VideoStreamConfig`.
- Add `nexigen-export`, an **on-demand** converter with no background process and no `--follow`: list every dataset available in a session, report each dataset's size, start/end time and byte range, inspect its schema/arrays, select datasets by id/name, time range, and channel, decode through the embedded registry, and emit MCAP (JSON Schema + JSON for built-in frames, pass-through for registered opaque encodings), JSONL, Perfetto, or codec elementary video streams. MP4/MKV remux is deferred to a follow-up after `nexigen_media` exists.
- Keep MCAP and FFmpeg out of the recorder. They are optional converter dependencies, so the recording binary has no MCAP/FFmpeg exception or allocation surface.
- **No ingestion/daemon, encoder service, or live converter in this change.** Those remain follow-ups; this change delivers the storage library and the offline converter.
- **BREAKING:** none to existing modules; `recorder` is a new module.

## Capabilities

### New Capabilities

- `recorder/raw-storage`: session manifest, `.nxrec`/`.nxidx` layout, self-contained schema/channel registry, preallocated single-writer path, CRC-32C coverage, timestamp/sequence mapping, rotation/retention, integrity and recovery.
- `recorder/video-storage`: `.nxvid` blob layout, `VideoFrameRef`/`VideoStreamConfig`, 4 KiB alignment, optional per-frame CRC, capture/source-sequence preservation, video gap reporting, and self-contained codec extradata.
- `recorder/export`: on-demand dataset inventory and per-dataset size/time/byte-range information, schema-driven extraction and decoding, MCAP/JSONL/Perfetto output, elementary video extraction, session portability without external decoding files, and explicit errors for unsupported data or output formats. MP4/MKV remux is a follow-up after `nexigen_media`.

### Modified Capabilities

None. No existing module's requirements change; `recorder` is additive.

## Impact

- **Code**: new `include/nexigen/recorder/`, `src/recorder/`, `tests/recorder/`, and `tools/nexigen-export/`; no existing headers or sources change.
- **Build**: new `nexigen_recorder` library with no COM or media dependency; `nexigen-export` optionally links MCAP C++ (pinned `42aea98d01dbbfe14c53f787378ee3d70ddb2e61`) and `nexigen_media` for remuxing. Neither dependency is linked into the recorder.
- **Performance**: FullHD 16 bpp 50 Hz `.nxvid` recording measured at 3.5% of one writer core without per-frame CRC and 5.1% with it; telemetry at 0.8–1.6 GB/s on the benchmark filesystem. The converter is offline and not latency-constrained.
- **Memory/allocation**: the write loop is allocation-free after preallocation. Record headers are 48 bytes, index entries 64 bytes, video frames 4 KiB aligned; exact buffer sizing and alignment are fixed in design.
- **Storage**: one session directory with per-segment `.nxrec`/`.nxidx`/`.nxvid` plus `session.json`; retention deletes only closed segments.
- **Interop**: no direct tool access to raw files; `nexigen-export` is the single conversion boundary, run only on request. It can list every dataset, report each dataset's size and start/end, and decode from the session alone.
