# Specification: `recorder/export-cli`

## Metadata
- **Module:** `recorder` (CLI tool: `nexigen-export`)
- **Layer:** L3 (depends on `core`, `chrono`, `recorder`, optional `media`)
- **Build Gates:** Compiles independently of `NEXIGEN_ENABLE_COM` (offline reader); container remuxing uses `nexigen_media` when `NEXIGEN_ENABLE_MEDIA=ON` (or built-in zero-dependency ISO-BMFF/Matroska muxer)
- **Status:** Draft (Proposal)

## Overview
`nexigen-export` is an offline command-line tool for inspecting, slicing, and exporting recorded sessions (`.nxrec` + `.nxidx` + `.nxvid`) into standard analytical and media formats: **JSONL**, **Chrome/Perfetto Trace JSON**, **MP4**, and **MKV**. It operates strictly on cold storage without requiring `iceoryx2` or running platform daemons.

---

## Requirements

### EXP-001: Session Inspection & Time-Range Slicing
The CLI MUST support fast metadata inspection and $O(\log N)$ extraction of arbitrary time windows and channel subsets.

* **Scenario EXP-001.1: Session summary (`inspect`)**
  * **Given** a session directory or manifest path
  * **When** the user runs `nexigen-export inspect <session_dir>`
  * **Then** the tool reads segment headers and `.nxidx` files (without scanning `.nxvid` blobs) and prints start/end timestamps (`Timestamp` + ISO-8601 UTC), duration, list of `ChannelId`s, message counts, drop/gap counts, and video stream resolutions/codecs.
* **Scenario EXP-001.2: Time and channel filtering (`slice`)**
  * **Given** `--start <ts|iso8601|offset>`, `--end <ts|iso8601|offset>`, and `--channels <pattern,...>`
  * **When** any export subcommand runs
  * **Then** the reader uses `.nxidx` binary search to jump directly to the starting offset in `.nxrec` and processes only records matching the time interval and channel filter.

### EXP-002: Telemetry & IPC Export to JSONL
Logs, metrics, traces, and custom `iceoryx2` messages MUST be exportable to newline-delimited JSON (`.jsonl`) using cold-path formatters.

* **Scenario EXP-002.1: Structured JSONL generation**
  * **Given** `nexigen-export jsonl <session_dir> -o out.jsonl`
  * **When** `LogFrame`, `MetricFrame`, `SpanFrame`, and `VideoFrameRef` records are read from `.nxrec`
  * **Then** each record is serialized via its ADL `to_json()` (`JSON_NOEXCEPTION=1`) with unified `mono_ns`, `utc_iso`, `channel`, `sequence`, and `type` fields.
* **Scenario EXP-002.2: Explicit gap representation**
  * **Given** a `GapRecord` encountered in `.nxrec`
  * **When** exporting to JSONL
  * **Then** a synthetic JSON object with `"type": "gap"`, `"lost_count"`, and `"reason"` is emitted at the exact sequence position.

### EXP-003: Distributed Trace Export to Chrome / Perfetto Format
Span records (`SpanFrame`) and correlated events MUST be exportable for visualization in Perfetto UI (`ui.perfetto.dev`).

* **Scenario EXP-003.1: Perfetto trace generation**
  * **Given** `nexigen-export perfetto <session_dir> -o trace.json`
  * **When** `SpanFrame` records (and optional `LogFrame` instant events via `--include-logs`) are extracted
  * **Then** the tool outputs valid Chrome Trace Event JSON (`ph: "X"` complete events, `pid`/`tid` mapped from process/thread metadata, `ts` in microseconds) compatible with `nexigen_tracing` Perfetto sink format.

### EXP-004: Zero-Reencode Video Remuxing (`.mp4` / `.mkv`)
Video streams stored in `.nxvid` MUST be exported into standard `.mp4` or `.mkv` containers via packet remuxing without decoding or re-encoding frames.

* **Scenario EXP-004.1: Remuxing H.264, H.265, and AV1 to `.mp4` / `.mkv`**
  * **Given** `nexigen-export video <session_dir> --channel <cam_id> -o clip.mp4` (or `.mkv`)
  * **When** the target stream codec is `H.264`, `H.265`, or `AV1`
  * **Then** the tool reads `VideoFrameRef` offsets from `.nxrec`, copies compressed frame payloads directly from `.nxvid`, computes exact presentation timestamps (`PTS`) from `Timestamp` deltas, and muxes them into `.mp4` or `.mkv` at disk I/O speed.
* **Scenario EXP-004.2: Remuxing JPEG XS and RAW streams to `.mkv`**
  * **Given** a video stream encoded as `JPEG XS` or uncompressed `RAW`
  * **When** the user exports to `.mkv` (Matroska)
  * **Then** frames are muxed into `.mkv` with exact `PTS` timestamps; if `.mp4` is requested for an incompatible codec (e.g., uncompressed RAW), the tool fails fast with `core::ErrorCode::UnsupportedFormat` and suggests `.mkv`.
* **Scenario EXP-004.3: Synchronized telemetry subtitle track (`--with-telemetry-srt`)**
  * **Given** `nexigen-export video ... --with-telemetry-srt`
  * **When** exporting a video slice
  * **Then** the tool generates a companion `.srt` file (or embedded subtitle track in `.mkv`) displaying synchronized `UTC` timestamps, `mono_ns`, and critical log/metric annotations for frame-accurate visual review.

### EXP-005: Integrity & Error Handling
* **Scenario EXP-005.1: Checksum verification (`verify`)**
  * **Given** `nexigen-export verify <session_dir>`
  * **When** scanning `.nxrec` and `.nxvid`
  * **Then** every record's `CRC-32C` is validated against `nexigen_core::integrity`, reporting corrupted byte ranges or unclosed tail segments with a non-zero exit code on mismatch.

---

## CLI Usage & Engineering Workflows

### 1. Session Inspection & Integrity Check
Inspect session metadata, active channels, codecs, and drop statistics without reading multi-gigabyte `.nxvid` blobs:
```bash
nexigen-export inspect /var/log/nexigen/sessions/run_2026_11_16/
nexigen-export verify /var/log/nexigen/sessions/run_2026_11_16/
```

### 2. Anomaly Investigation (Telemetry + Perfetto Slice)
Extract a narrow time window around a detected anomaly (by ISO-8601 timestamp or relative offset from session start) into JSONL and Perfetto trace formats:
```bash
# Export logs, metrics, and IPC messages to JSONL
nexigen-export jsonl /var/log/nexigen/sessions/run_2026_11_16/ \
  --start 2026-11-16T02:30:10Z \
  --end 2026-11-16T02:30:20Z \
  --channels "Nexigen/Log/*,nexigen/metrics/stream,sensor/lidar/*" \
  -o anomaly_telemetry.jsonl

# Export execution spans and log markers for Perfetto UI
nexigen-export perfetto /var/log/nexigen/sessions/run_2026_11_16/ \
  --start +120.5s \
  --end +130.5s \
  --include-logs \
  -o anomaly_trace.json
```

### 3. Synchronized Video Extraction with Telemetry Overlay
Remux a specific camera stream for the exact same incident window without re-encoding, embedding synchronized timestamps and critical log events as subtitles:
```bash
# H.265 / AV1 / H.264 stream to MP4 or MKV
nexigen-export video /var/log/nexigen/sessions/run_2026_11_16/ \
  --channel camera/front/h265 \
  --start 2026-11-16T02:30:10Z \
  --end 2026-11-16T02:30:20Z \
  --with-telemetry-srt \
  -o front_cam_incident.mkv

# JPEG XS or RAW stream to Matroska (.mkv)
nexigen-export video /var/log/nexigen/sessions/run_2026_11_16/ \
  --channel camera/stereo_left/jpegxs \
  --start +120.5s \
  --end +125.0s \
  -o stereo_left.mkv
```
