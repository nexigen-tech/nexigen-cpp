# Tasks

## 1. Benchmark scaffolding and dependency pin

- [x] 1.1 Add `NEXIGEN_BUILD_STORAGE_BENCHMARKS` (default `OFF`) and `benchmarks/recorder/CMakeLists.txt`; verify a default configure produces no new target and `cmake --build` output is unchanged.
- [x] 1.2 Pin MCAP C++ through `FetchContent` to commit `42aea98d01dbbfe14c53f787378ee3d70ddb2e61` with `MCAP_COMPRESSION_NO_ZSTD` and `MCAP_COMPRESSION_NO_LZ4`; verify a configure with the benchmark option ON fetches and builds it, while OFF never fetches it.
- [x] 1.3 Add the `nexigen_storage_bench` skeleton with `--workload`, `--mode`, `--iterations`, `--out`, and `--verify`; verify `--help` works and an unknown workload/mode exits non-zero with a diagnostic.

## 2. Shared benchmark harness

- [x] 2.1 Implement the deterministic 16 MiB payload blob and the `offset(i) = (i * 7919) % (16 MiB - 512 KiB)` window function; verify a unit test pins the blob checksum for the fixed seed and message count.
- [x] 2.2 Implement the `CountingSink` and `FileSink` behind one interface; verify byte counts match and `FileSink` file size equals bytes written.
- [x] 2.3 Implement TSC calibration, per-message timing, median/p99/p99.9, thread/process CPU time, and peak RSS; verify the metrics against a synthetic known-duration loop and a known allocation size.
- [x] 2.4 Implement TSV output plus a human-readable summary carrying the run metadata (CPU, governor, RAM, kernel, filesystem, compiler, build type, MCAP commit, command line); verify every column is present and parses as a number where expected.

## 3. MCAP candidate

- [x] 3.1 Isolate the MCAP implementation TU in an exceptions-enabled object library and link it only into the benchmark; verify every production target still compiles with `-fno-exceptions`.
- [x] 3.2 Implement the custom `IWritable` adapters for `CountingSink` and `FileSink`; verify an MCAP file written in `FileSink` reopens and reports the exact message count and channel count.
- [x] 3.3 Implement `mcap-chunked-256k`, `mcap-chunked-1m`, `mcap-unchunked`, and `mcap-no-message-index` modes with compression disabled and chunk CRC selectable; verify each mode reports its index mode and writes the expected message count.
- [x] 3.4 Instrument steady-state allocations inside the measured region (message-index vectors, statistics map, chunk growth); verify the report states whether the region is allocation-free and identifies the allocating component when it is not.
- [x] 3.5 Add a truncated-file recovery check; verify MCAP recovery (or linear chunk scan) reports the surviving message count and the measured recovery time.

## 4. Custom prototype candidate

- [x] 4.1 Define the prototype 48-byte record header and 64-byte sparse index entry with `static_assert` on size/alignment; verify the layout test fails on any field drift.
- [x] 4.2 Implement the batched writer (64 KiB and 1 MiB buffers), sparse index every 256 KiB or 50 ms, and per-record CRC-32C; verify message count, payload checksum, and an index lookup that lands on the expected record.
- [x] 4.3 Implement the `.nxvid` writer with 4 KiB-aligned frame offsets and optional per-frame CRC; verify every frame offset is 4 KiB aligned and the frame/byte counts match the workload.
- [x] 4.4 Add a torn-tail recovery test that truncates the file mid-record; verify recovery returns the intact prefix and stops at the last valid CRC.

## 5. Publish-path header benchmark

- [x] 5.1 Reuse the existing latency harness pattern to measure `send_copy(payload)` and the producer `try_push` path; verify zero heap delta and report median/p99/p99.9/max.
- [x] 5.2 Add the `loan_uninit -> write_payload -> user_header_mut -> send` variant with a local `SampleHeader`-shaped type; verify it compiles, measures the same payload, and reports the delta versus `send_copy`.

## 6. Run matrix and report

- [x] 6.1 Run every workload and mode for at least 5 iterations on the target machine; verify all runs pass checksum/count validation before their numbers are accepted.
- [x] 6.2 Write `benchmarks/recorder/RESULTS.md` with the metadata, the comparison table (throughput, CPU, p99, RSS, file/index size, recovery, CRC mode), and an explicit recommendation for telemetry, video, and the publish header; verify every metric required by the spec is present.
- [x] 6.3 Re-run one telemetry mode and one video mode from a clean output directory; verify the medians agree within the run-to-run noise stated in the report.

## 7. Verification and guardrails

- [x] 7.1 Build Debug and Release (`-O3 -march=native -flto` with latency gates) and run 100% of the unit tests; verify no production test regressions.
- [x] 7.2 Build ThreadSanitizer (`-DNEXIGEN_ENABLE_TSAN=ON`) and AddressSanitizer (`-DNEXIGEN_ENABLE_ASAN=ON`) and run 100% of the unit tests; verify no sanitizer reports.
- [x] 7.3 Build with `NEXIGEN_BUILD_STORAGE_BENCHMARKS=ON` in Release and confirm the benchmark target is built but not installed or exported; verify `cmake --install` does not include it.
- [x] 7.4 Confirm `git diff` touches no production headers, sources, tools, or existing specs; verify the only new tree is `benchmarks/recorder/` plus the opt-in CMake wiring.
- [x] 7.5 Confirm no public type is added, so no Doxygen or `to_string`/JSON/formatter work is required; verify the build exposes no new public include path.
- [x] 7.6 Confirm `docs/architecture.md` is unchanged because the benchmark does not alter the production architecture; verify the Mermaid diagrams are untouched.

## Workflow follow-up

- Archive the change after the report is reviewed and the follow-up storage-format decision is recorded.
- The follow-up change consumes `benchmarks/recorder/RESULTS.md` and decides the production recorder format.
