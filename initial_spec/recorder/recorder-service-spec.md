# Specification: `recorder/recording-service`

## Metadata
- **Module:** `recorder` (CMake target: `nexigen_recorder`, tool: `nexigen-recordd`)
- **Layer:** L3 (depends on `core`, `chrono`, `com`, optional `media`)
- **Build Gates:** Requires `NEXIGEN_ENABLE_COM=ON`; video encoding requires `NEXIGEN_ENABLE_MEDIA=ON`
- **Status:** Draft (Proposal)

## Overview
The `recorder` module and `nexigen-recordd` daemon provide high-throughput, append-only session recording for telemetry (`LogFrame`, `MetricFrame`, `SpanFrame`), arbitrary `iceoryx2` payloads, and multi-stream video feeds (compressed pass-through and All-Intra encoded RAW streams). The design isolates storage I/O and video encoding from real-time producers and enables $O(\log N)$ time-range extraction for offline analytics and export.

---

## Requirements

### REC-001: Zero-Impact Subscription & Ingestion
The recorder MUST act as a passive, non-blocking subscriber over `iceoryx2` (`nexigen_com`) without interfering with real-time producers or existing operational daemons (`logd`, `metricsd`, `traced`).

* **Scenario REC-001.1: Multi-channel discovery and subscription**
  * **Given** a configuration listing target `iceoryx2` service names or wildcard patterns (e.g., `Nexigen/Log/*`, `nexigen/metrics/stream`, `camera/*/raw`)
  * **When** `nexigen-recordd` starts or detects a newly published matching service
  * **Then** it registers a `Subscriber` in a unified `com::WaitSet` and assigns a monotonically increasing internal `ChannelId`.
* **Scenario REC-001.2: Producer isolation on ring overflow**
  * **Given** the recorder I/O or encoding pipeline falls behind incoming message rates
  * **When** an `iceoryx2` subscriber buffer overflows
  * **Then** the producer continues without blocking, and the recorder detects the `sequence` discontinuity and writes an explicit `GapRecord` containing the lost frame count.

### REC-002: Segmented Storage Layout & Out-of-Band Video Blobs
Session data MUST be partitioned into fixed-size or time-bounded binary segments to guarantee bounded memory usage, crash resilience, and fast linear NVMe writes.

* **Scenario REC-002.1: Creation of synchronized segment triplets**
  * **Given** an active recording session
  * **When** a new segment is opened (initially or upon reaching `max_segment_bytes` or `max_segment_duration`)
  * **Then** the storage engine creates three synchronized files:
    1. `<session>_<seq>.nxrec` — primary binary stream for telemetry, IPC messages, and `VideoFrameRef` metadata.
    2. `<session>_<seq>.nxidx` — sparse timestamp index mapping `[Timestamp -> .nxrec offset]`.
    3. `<session>_<seq>.nxvid` — contiguous, 4 KiB page-aligned blob file for video frame payloads.
* **Scenario REC-002.2: Out-of-band video payload referencing**
  * **Given** an incoming video frame (compressed or newly encoded)
  * **When** the frame is committed to storage
  * **Then** the raw frame bytes are written to `.nxvid` via `writev`/`io_uring`, and a fixed-size `VideoFrameRef` record (containing `ChannelId`, `Timestamp`, codec tag, resolution, `.nxvid` byte offset, byte length, and CRC-32C) is appended to `.nxrec`.

### REC-003: Compressed Video Pass-Through
Pre-compressed video streams (`JPEG XS`, `H.264`, `H.265`, `AV1`) MUST be recorded without re-encoding or CPU-side bitstream parsing.

* **Scenario REC-003.1: Zero-copy pass-through write**
  * **Given** a video channel configured with `mode = passthrough`
  * **When** a sample arrives via `iceoryx2`
  * **Then** the recorder forwards the payload pointer directly to the storage writer queue without copying pixel/bitstream buffers into user-space heap memory.

### REC-004: All-Intra Encoding for RAW Video Streams
When `NEXIGEN_ENABLE_MEDIA=ON`, RAW video streams (`RGB`, `YUV`, `Bayer`) MUST support either uncompressed direct storage or **All-Intra** (`GOP = 1`) compression via a hardware-agnostic backend (`nexigen_media`).

* **Scenario REC-004.1: Hardware-accelerated All-Intra encoding**
  * **Given** a RAW video channel configured with `codec = h265 | av1` and `backend = nvenc | vaapi | rkmpp`
  * **When** RAW frames are ingested
  * **Then** frames are routed by `ChannelId` affinity to an encoder worker pool, encoded strictly as independent I-frames (`keyint=1`), and written to `.nxvid`.
* **Scenario REC-004.2: Zero-copy DMA-BUF ingestion**
  * **Given** an incoming RAW frame carrying a Linux `dma_buf` descriptor
  * **When** passed to a hardware encoder backend (`VAAPI` / `DRM_PRIME`)
  * **Then** the frame is imported directly into VPU/GPU memory without copying pixels through CPU RAM.
* **Scenario REC-004.3: Software encoding fallback and backpressure**
  * **Given** a non-RT RAW channel configured with `backend = cpu` (`libx265` / `libsvtav1`)
  * **When** encoder queue depth exceeds `max_pending_frames`
  * **Then** excess incoming frames are dropped prior to encoding, and a `GapRecord` with reason `EncoderOverload` is emitted to `.nxrec`.

### REC-005: Sparse Indexing & Fast Extraction
Every segment MUST maintain a sparse index to allow sub-millisecond seeking during offline export and analytics without scanning full `.nxrec` or `.nxvid` files.

* **Scenario REC-005.1: Periodic index emission**
  * **Given** active writes to `.nxrec`
  * **When** elapsed time since the last index entry exceeds `index_interval` (default: `50 ms`) or written bytes exceed `index_stride` (default: `256 KiB`)
  * **Then** an `IndexEntry` (`Timestamp` in `CLOCK_MONOTONIC_RAW`, `UtcOffset`, `.nxrec` file offset, active `ChannelMask`) is appended to `.nxidx`.
* **Scenario REC-005.2: Time-range slice extraction**
  * **Given** a recorded session and a query interval `[T_start, T_end]` with an optional `ChannelId` filter
  * **When** the reader API opens the session
  * **Then** it locates the bounding `.nxidx` entries via binary search in $O(\log N)$ time, maps the exact byte range via `mmap`/`pread`, and yields matching records and video frame slices without reading unrelated segments.

### REC-006: Data Integrity & Crash Resilience
All persisted records MUST withstand ungraceful process termination or power loss without corrupting previously flushed data.

* **Scenario REC-006.1: Frame-level checksum validation**
  * **Given** any record written to `.nxrec` or `.nxvid`
  * **When** the record header is serialized
  * **Then** it includes a `CRC-32C` checksum (computed via `nexigen_core::integrity`) covering the header and payload.
* **Scenario REC-006.2: Recovery of unclosed segments**
  * **Given** a segment file whose footer was not written due to a crash
  * **When** the reader or export tool opens the segment
  * **Then** it reads valid records sequentially up to the last valid `CRC-32C` boundary and reconstructs the missing `.nxidx` in memory.

### REC-007: C++23 & Platform Compliance
The module MUST strictly adhere to `nexigen-cpp` core mandates.

* **Scenario REC-007.1: Build flags and error handling**
  * **Given** compilation with `-fno-exceptions -fno-rtti -Werror`
  * **When** any I/O, encoder, or parsing error occurs
  * **Then** functions return `core::Result<T, core::ErrorCode>` (never throwing exceptions or calling `.value()`).
* **Scenario REC-007.2: Public type serialization**
  * **Given** public configuration and metadata structures (e.g., `RecorderConfig`, `SegmentHeader`, `VideoFrameRef`)
  * **When** inspected or serialized
  * **Then** all types provide `to_string()`, ADL `to_json`/`from_json` (`JSON_NOEXCEPTION=1`), and `std::formatter` specializations.
* **Scenario REC-007.3: Thread affinity and resource isolation**
  * **Given** `RecorderConfig` specifying `CpuSet` masks for ingestion, encoding, and disk I/O workers
  * **When** `nexigen-recordd` initializes its worker pools
  * **Then** thread pinning is applied via `core::apply_thread_spec_self` so encoding workloads cannot preempt real-time simulation cores.
