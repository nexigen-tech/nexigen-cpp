# Design

## Context

See `proposal.md` for motivation. Existing constraints that shape the benchmark:

- The recorder storage format is still a draft; no `.nxrec` reader/writer exists yet.
- The only measured baseline today is the iceoryx2 publish path in `build-release-com` on this machine: `send_copy` 167.7 ns median, `loan/write/send` 217.8 ns median, producer `try_push` 7.5 ns, zero heap delta. The proposed `SampleHeader` forces the 218 ns path unless an alternative is chosen.
- The project compiles all targets with `-fno-exceptions -fno-rtti`; MCAP's C++ `FileWriter` contains a `throw std::system_error` and the writer uses `std::vector` / `std::unordered_map` internally. That is an integration constraint, not just a performance detail.
- MCAP is not installed on this machine; the benchmark must fetch and pin it, and must not require zstd/lz4 development packages.

## Goals / Non-Goals

**Goals:**

- Measure MCAP and the custom prototype on identical bytes, rates, and sinks.
- Separate writer CPU cost from storage cost: a counting sink (no disk) and a file sink (NVMe) for both candidates.
- Measure telemetry, video, and the publish-path header cost.
- Produce a reproducible report that names hardware, OS, filesystem, compiler, library revisions, commands, and iteration counts.
- Expose the MCAP integration cost under project constraints (exceptions, allocations, sequence width).

**Non-Goals:**

- No production `recorder` target, public header, or install rule in this change.
- No decision about the recorder config schema, rotation policy, or session layout.
- No compression comparison; compression is disabled in all candidates.
- No read-path benchmark beyond recovery time and message-count verification; read performance is a follow-up if the format decision needs it.

## Decisions

### D1: Separate opt-in benchmark tree and target

The benchmark lives under `benchmarks/recorder/` and builds only with `NEXIGEN_BUILD_STORAGE_BENCHMARKS=ON` (default `OFF`). It is not installed, not exported, and not linked by production targets. A single executable (`nexigen_storage_bench`) takes a workload and mode on the command line and prints TSV plus a human-readable summary.

**Alternative:** putting the benchmark under `tests/recorder/` would mix it with unit tests and run it in the default test matrix; rejected because the benchmark is long-running and hardware-sensitive.

### D2: Two sinks per candidate

Every candidate writes through the same small sink interface:

- `CountingSink` — discards bytes and counts them. Measures writer CPU and per-message overhead without disk or page-cache effects.
- `FileSink` — writes to a file on the target filesystem, with optional `fdatasync` on a policy boundary. Measures end-to-end throughput, file size, and durability behavior.

Both candidates use the same sink semantics. MCAP uses a custom `IWritable` implementation for `FileSink` and `CountingSink`; the custom prototype uses the same interface. This isolates the format's CPU cost from storage.

### D3: Deterministic shared payload stream

A single 16 MiB blob is generated deterministically (fixed seed) and every message is a window into it:

```text
offset(i) = (i * 7919) % (16 MiB - 512 KiB)
```

The same blob, message count, channel mix, and rate are used for every candidate. A CRC-32 of the exact bytes handed to each writer is computed and compared; a mismatch fails the run. This prevents a candidate from "winning" by being fed different data.

### D4: Workloads

| Workload | Rate | Payload | Purpose |
|---|---:|---:|---|
| `logs-low` | 10k/s | 256 B | typical logging |
| `logs-high` | 100k/s | 256 B | busy logging |
| `logs-stress` | 1M/s | 256 B | sequence/index stress |
| `spans` | 100k/s | 384 B | tracing |
| `metrics` | 1k/s | 4464 B | large periodic frames |
| `video-50` | 50 MB/s | encoded frames | moderate video |
| `video-100` | 100 MB/s | encoded frames | high video |
| `video-200` | 200 MB/s | encoded frames | stress video |

Video frames are fixed-size synthetic encoded payloads (for example 128 KiB, 512 KiB, 1 MiB) repeated to the target byte rate; the frame count and byte count are verified. The benchmark does not encode; it measures storage.

### D5: Candidate modes

MCAP modes:

- `mcap-chunked-256k` / `mcap-chunked-1m`: chunking on, compression `None`, chunk CRC on, message index on and off.
- `mcap-unchunked`: `noChunking=true`, no message/chunk index; optionally `noSummary=true`.
- `mcap-chunked-nosindex`: chunk index on, `noMessageIndex=true`.

Custom prototype modes:

- `nxrec-batched-64k`: 48-byte record header, 64 KiB write buffer, sparse index every 256 KiB or 50 ms, per-record CRC-32C.
- `nxrec-batched-1m`: same with a 1 MiB write buffer.
- `nxvid-blob`: 4 KiB-aligned frame writes, per-frame CRC optional (off for the video comparison), separate `.nxvid`.

Video modes:

- `mcap-video`: one message per encoded frame, chunk size at least the largest frame, chunk CRC on/off.
- `nxvid-video`: `.nxvid` blob writer with 4 KiB alignment and no per-record CRC-32C requirement.

### D6: Publish-path header measurement

Reuse the existing `nexigen_com_latency_tests` harness style:

- `send_copy(payload)` (current fastest path).
- `loan_uninit -> write_payload -> user_header_mut -> send` (the path a user header forces).
- Producer `try_push` into an SPSC ring, measured separately.
- 10,000 measured iterations, 2,000 warm-up, TSC calibration, best-of-three medians, thread pinning, zero-heap check via `mallinfo2`.
- Report median, p99, p99.9, max, and heap delta.

This measurement is independent of the storage format and feeds the `SampleHeader` decision.

### D7: Metrics and verification

Each run reports:

- Messages/s and MB/s.
- CPU time (thread and process).
- Median and p99 per-message write latency (TSC).
- Peak RSS (`ru_maxrss`).
- File size and index size.
- Recovery time: for the custom prototype, scan to the last valid CRC; for MCAP, measure `mcap recover` or linear chunk scan on an intentionally truncated file.
- Active CRC mode (per-record, per-chunk, data-section, none).
- Payload checksum and message count.

A run fails if the checksum or count differs, or if the writer reports an error.

### D8: Reproducibility and report

- At least 5 iterations per mode; medians reported; the first iteration is discarded after warm-up.
- Record CPU model, core count, frequency governor, RAM, kernel, filesystem, mount options, compiler version, build type, MCAP commit, and exact commands.
- Write a comparison table and a recommendation to `benchmarks/recorder/RESULTS.md`.
- Raw TSV output is kept beside the report for re-analysis.

The MCAP C++ dependency is pinned to Foxglove commit `42aea98d01dbbfe14c53f787378ee3d70ddb2e61` (the latest `cpp/` change on `main` as of 2026-11-09), fetched through `FetchContent` with a git tag/commit; no system package is used. Compression is disabled with `MCAP_COMPRESSION_NO_ZSTD` and `MCAP_COMPRESSION_NO_LZ4`.

### D9: MCAP integration constraints are measured, not hidden

The MCAP implementation TU must be compiled with exceptions enabled because `FileWriter` contains an explicit `throw`. It is isolated in an object library that is linked only into the benchmark; the rest of the project keeps `-fno-exceptions`. The benchmark reports:

- whether a custom `IWritable` avoids the throwing `FileWriter`;
- whether steady-state `write` allocates (message-index vectors, statistics map, chunk growth);
- whether `noMessageIndex`/`noSummary` remove those allocations and what they cost semantically (coarse or no index).

This turns the exception/allocation question into evidence for the production decision.

### D10: Concurrency and memory rules inside the benchmark

- The writer loop is single-threaded; no atomics are needed in the measured region.
- The producer/consumer publish benchmark uses the existing `core::SpscRingBuffer` with `alignas(64)` producer/consumer cells and `relaxed`/`acquire`/`release` orderings; no new synchronization primitives are introduced.
- All message, chunk, and index buffers are preallocated before the measured region. Any allocation inside the measured region is reported as a failure of the benchmark's own zero-allocation contract.
- Threads are pinned to non-RT cores; TSC is calibrated as in the existing latency test.

## Risks / Trade-offs

- **MCAP throws and allocates internally** → measure it explicitly; if production adoption requires a patched no-exception writer, treat that as a separate cost in the report.
- **Page cache masks disk cost** → run CPU-only (`CountingSink`) and file-backed (`FileSink`) variants; report both. Do not drop caches (requires root); document that file-backed numbers include page cache.
- **TSC/frequency noise** → pin cores, warm up, use medians and best-of-three, and report the governor.
- **MCAP noChunking has no index** → report it as a semantic difference, not only a performance win; compare it against the custom sparse index explicitly.
- **Video frames larger than the chunk size** → force one chunk per frame or set the chunk size above the largest frame; report which setting was used.
- **Library pin drift** → pin the MCAP commit in CMake and record it in the report; no `main` tracking in CI.
- **Benchmark gaming** → checksum and count verification; the same sink and payload generator for all candidates.

## Migration Plan

This change adds only benchmark code and a report. No production target or format changes, so rollback is deleting the opt-in target and the report. The follow-up storage-format change consumes the report; if MCAP wins, its exception/allocation wrapper becomes part of that change, not this one.

## Open Questions

- Whether a patched no-exception / preallocated MCAP writer is acceptable if MCAP wins; the benchmark will quantify the cost but the decision belongs to the follow-up format change.
- Whether `noSummary` + `noChunking` is ever acceptable for production (it removes the index); the report will show the trade-off, but the requirement will be decided in the format change.
