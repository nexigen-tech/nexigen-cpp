# Proposal

## Why

The recorder's storage format is still a draft custom design (`.nxrec` / `.nxidx` / `.nxvid`), while MCAP offers native schemas, channels, indexes, and direct Foxglove / PlotJuggler / Python compatibility. MCAP changes integrity granularity, index overhead, and video-write cost, and no measured comparison exists on the target hardware. Committing to either format without numbers would be speculative.

## What Changes

- Add a reproducible, gated benchmark harness that writes identical synthetic workloads through:
  - the MCAP C++ writer in `chunked`, `noChunking`, and `noMessageIndex` modes with compression disabled and configurable chunk sizes;
  - a minimal custom `.nxrec` prototype writer (48-byte record header, sparse index, batched writes, per-record CRC-32C);
  - a video path comparing MCAP chunked messages against a 4 KiB-aligned `.nxvid` blob writer.
- Measure throughput, CPU time, p99 write latency, peak RSS, file size, index size, and recovery time for each mode.
- Measure the cost of the proposed `com::SampleHeader` on the publish path: `send_copy(payload)` versus `loan_uninit -> write_payload -> user_header_mut -> send`.
- Produce a comparison report with a table and a recommendation.
- **No production storage format is changed by this change.** The follow-up recorder-format change consumes the report.
- Video integrity: per-record CRC-32C is explicitly **not** a requirement for the video comparison; chunk-level or optional CRC is acceptable. Per-record CRC-32C remains a criterion for telemetry.

## Capabilities

### New Capabilities

- `quality/storage-format-benchmark`: benchmark workloads, modes, metrics, reproducibility rules, and the reporting contract used to choose the recorder storage format.

### Modified Capabilities

None. Existing behavior is not changed; the benchmark is additive.

## Impact

- **Code**: new benchmark sources under `benchmarks/recorder/`; no production headers, targets, or APIs change.
- **Build**: new opt-in target (for example `NEXIGEN_BUILD_STORAGE_BENCHMARKS=ON`); the MCAP dependency is pinned and built without zstd/lz4 compression. The default build remains untouched.
- **Dependencies**: MCAP C++ (header/source, pinned revision) only for the benchmark target.
- **Performance**: benchmark-only code; no hot-path, latency, or allocation impact on shipped targets. The benchmark itself is single-threaded per writer and pinned to a non-RT core where measured.
- **Memory**: benchmark processes allocate their own buffers; production memory behavior is unchanged.
- **Decision input**: the report selects MCAP-native telemetry + `.nxvid` video, MCAP for everything, or the custom `.nxrec` format before any format implementation starts.
