# Recorder storage-format benchmark

Opt-in comparison of the two recorder storage candidates:

- **MCAP** (Foxglove C++ writer) in chunked, unchunked, and no-message-index
  modes with compression disabled;
- the custom `.nxrec` prototype (48-byte record header, 64-byte sparse index,
  per-record CRC-32C) and a 4 KiB-aligned `.nxvid` blob.

The benchmark is not built by default and is never installed or exported.

## Build

```bash
cmake -S . -B build-storage-bench -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DNEXIGEN_BUILD_STORAGE_BENCHMARKS=ON \
  -DNEXIGEN_BUILD_EXAMPLES=OFF -DNEXIGEN_BUILD_TOOLS=OFF
cmake --build build-storage-bench --target nexigen_storage_bench -j 64
```

The MCAP C++ dependency is pinned to commit
`42aea98d01dbbfe14c53f787378ee3d70ddb2e61` through a SHA-256-verified source
archive. Its include tree is treated as `SYSTEM`; the implementation TU is
compiled with exceptions enabled and isolated in
`nexigen_mcap_bench_impl`, because MCAP's writer contains an explicit
`throw std::system_error`. Production targets keep `-fno-exceptions`.

Enable `NEXIGEN_ENABLE_COM=ON` to build the `publish-copy` / `publish-header`
measurement.

## Run

```bash
# One run
build-storage-bench/benchmarks/recorder/nexigen_storage_bench \
  --workload logs-high --mode mcap-chunked-1m --sink file \
  --iterations 5 --seconds 0.5 --verify --out /var/tmp/nexigen-storage-bench

# Full matrix (telemetry + video + publish if COM is available)
benchmarks/recorder/run_matrix.sh \
  --bin build-release-com/benchmarks/recorder/nexigen_storage_bench \
  --out /var/tmp/nexigen-storage-bench --iterations 5
```

Workloads: `logs-low`, `logs-high`, `logs-stress`, `spans`, `metrics`,
`video-50`, `video-100`, `video-200`, `video-fullhd16-50`
(1920 × 1080, 16 bpp, 50 Hz).

Modes: `mcap-chunked-256k`, `mcap-chunked-1m`, `mcap-unchunked`,
`mcap-no-message-index`, `nxrec-batched-64k`, `nxrec-batched-1m`,
`mcap-video`, `nxvid-video`, `publish-copy`, `publish-header`.

Sinks: `file` writes to the output directory; `counting` discards bytes and
measures writer CPU only. File runs use one `fdatasync` at close, not per
record; the report therefore reflects page-cache throughput unless the output
directory is on a slow device.

Video CRC switches: `--video-crc` computes a CRC-32C per `.nxvid` frame
(default off); `--no-chunk-crc` disables the MCAP chunk CRC (default on).
See the CRC-sensitivity section in [RESULTS.md](RESULTS.md).

## Results

See [RESULTS.md](RESULTS.md). Raw rows are written to
`<out>/results.tsv`; every file-sink row is verified against the expected
message count (MCAP/`.nxrec`) or frame alignment (`.nxvid`).

## Tests

```bash
ctest --test-dir build-storage-bench -R nexigen_storage_bench_tests --output-on-failure
```

The unit tests pin the deterministic blob checksum, the 48/64-byte prototype
layouts, sink byte counts, MCAP round-trip, MCAP truncated-prefix recovery,
custom index verification, and `.nxvid` alignment.

### Production writer companion

`nexigen_production_bench` drives the shipped `nexigen::recorder::SessionWriter`
(not the prototypes) for the two published workloads:

```bash
cmake --build build-storage-bench --target nexigen_production_bench -j 64
build-storage-bench/benchmarks/recorder/nexigen_production_bench \
  --workload logs-high --seconds 1 --out /var/tmp/nexigen-production-bench
build-storage-bench/benchmarks/recorder/nexigen_production_bench \
  --workload video-fullhd16-50 --seconds 0.5 --out /var/tmp/nexigen-production-bench [--video-crc]
```

See `RESULTS.md` ("Production writer") for the measured envelope.
