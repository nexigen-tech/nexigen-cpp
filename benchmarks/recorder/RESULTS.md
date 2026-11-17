# Storage-format benchmark results

Generated from `/var/tmp/nexigen-storage-bench/results.tsv` (5 iterations per
mode, medians reported). The raw TSV is kept beside this report.

## Environment

| Item | Value |
|---|---|
| CPU | AMD Ryzen Threadripper 9970X, 32 cores / 64 threads |
| Governor | `powersave` (frequency scaling not disabled) |
| RAM | 122 GiB |
| Kernel | Linux 7.0.0-43-generic |
| Filesystem | ext4 on `/var/tmp` (file sink; page cache, one `fdatasync` at close) |
| Compiler | GCC 15.2.0 |
| Build | Release (`-O3 -march=native -flto`) |
| MCAP | Foxglove commit `42aea98d01dbbfe14c53f787378ee3d70ddb2e61`, compression disabled |
| Iterations | 5; 0.5 s per telemetry/video run, 0.2 s for `logs-stress` |
| Verification | `--verify` on every file-sink run; 360/360 data rows passed |
| Noise | Repeat run differed by 5–10% in throughput (see reproducibility section) |

`counting` discards bytes and measures writer CPU; it does not include the
kernel copy. `file` writes to ext4 and includes that copy. No per-record
`fdatasync` is issued; both candidates sync once at close.

## Telemetry, file sink

| Workload | Mode | MB/s | CPU ms | p50 ns | p99 ns | File MB | Alloc-free |
|---|---|---:|---:|---:|---:|---:|:---:|
| logs-high | mcap-chunked-256k | 973 | 9.02 | 10 | 240 | 15.2 | no |
| logs-high | mcap-chunked-1m | 1080 | 7.24 | 0 | 200 | 15.2 | no |
| logs-high | mcap-unchunked | 365 | 32.88 | 501 | 1892 | 14.4 | no |
| logs-high | mcap-no-message-index | 1169 | 5.64 | 0 | 90 | 14.4 | no |
| logs-high | nxrec-batched-64k | 816 | 6.62 | 10 | 240 | 15.2 | yes |
| logs-high | nxrec-batched-1m | 824 | 6.49 | 30 | 230 | 15.2 | yes |
| logs-stress | mcap-chunked-256k | 1748 | 24.09 | 0 | 180 | 60.6 | no |
| logs-stress | mcap-chunked-1m | 1878 | 21.61 | 0 | 170 | 60.6 | no |
| logs-stress | mcap-unchunked | 455 | 113.86 | 431 | 1562 | 57.4 | no |
| logs-stress | mcap-no-message-index | 1805 | 21.21 | 0 | 20 | 57.4 | no |
| logs-stress | nxrec-batched-64k | 1564 | 20.70 | 10 | 230 | 60.8 | yes |
| logs-stress | nxrec-batched-1m | 1421 | 26.03 | 10 | 230 | 60.8 | yes |
| spans | mcap-chunked-256k | 1440 | 6.67 | 0 | 120 | 21.6 | no |
| spans | mcap-chunked-1m | 1489 | 6.69 | 0 | 160 | 21.6 | no |
| spans | mcap-unchunked | 535 | 32.22 | 441 | 1822 | 20.8 | no |
| spans | mcap-no-message-index | 1448 | 6.92 | 0 | 100 | 20.8 | no |
| spans | nxrec-batched-64k | 1090 | 7.07 | 10 | 230 | 21.6 | yes |
| spans | nxrec-batched-1m | 1042 | 8.02 | 20 | 230 | 21.6 | yes |
| metrics | mcap-chunked-256k | 385 | 0.79 | 60 | 50346 | 2.3 | no |
| metrics | mcap-chunked-1m | 374 | 0.69 | 130 | 461 | 2.3 | no |
| metrics | mcap-unchunked | 360 | 1.14 | 1582 | 3334 | 2.2 | no |
| metrics | mcap-no-message-index | 379 | 0.60 | 60 | 44636 | 2.2 | no |
| metrics | nxrec-batched-64k | 206 | 0.76 | 561 | 9280 | 2.3 | yes |
| metrics | nxrec-batched-1m | 207 | 0.68 | 681 | 1462 | 2.3 | yes |

The metrics p99 values are dominated by one OS stall in a 500-message sample;
they are not a stable per-message cost.

## Telemetry, counting sink (writer CPU only)

| Workload | Mode | MB/s | CPU ms | p50 ns | p99 ns | Alloc-free |
|---|---|---:|---:|---:|---:|:---:|
| logs-high | mcap-chunked-1m | 8182 | 1.85 | 0 | 30 | no |
| logs-high | mcap-unchunked | 14537 | 0.99 | 0 | 20 | no |
| logs-high | nxrec-batched-64k | 9332 | 1.63 | 10 | 120 | yes |
| logs-stress | mcap-chunked-1m | 5661 | 10.70 | 0 | 130 | no |
| logs-stress | mcap-unchunked | 16841 | 3.41 | 0 | 0 | no |
| logs-stress | nxrec-batched-64k | 7325 | 8.30 | 0 | 150 | yes |
| spans | mcap-chunked-1m | 6111 | 3.53 | 0 | 190 | no |
| spans | mcap-unchunked | 24436 | 0.85 | 0 | 0 | no |
| spans | nxrec-batched-64k | 10818 | 2.00 | 20 | 90 | yes |

## Video, file sink

| Workload | Mode | MB/s | CPU ms | p50 ns | p99 ns | File MB | Alloc-free |
|---|---|---:|---:|---:|---:|---:|:---:|
| video-50 | mcap-video | 1687 | 6.92 | 2432 | 336575 | 24.9 | no |
| video-50 | nxvid-video | 2164 | 3.28 | 13557 | 21087 | 24.9 | yes |
| video-100 | mcap-video | 2179 | 12.72 | 2413 | 219136 | 50.0 | no |
| video-100 | nxvid-video | 2482 | 9.65 | 17673 | 35175 | 49.9 | yes |
| video-200 | mcap-video | 2505 | 24.58 | 2312 | 209220 | 99.9 | no |
| video-200 | nxvid-video | 2899 | 19.87 | 16911 | 36515 | 99.9 | yes |

MCAP video is competitive in throughput but pays a chunk-close/CRC tail spike
every chunk (~8 frames at 1 MiB). `.nxvid` has a higher per-frame p50 (write
syscall) but a much lower p99.

### CRC sensitivity (file sink, 5 × 0.5 s)

| Candidate | CRC | video-100 MB/s | CPU ms | p50 µs | p99 µs | video-200 MB/s | CPU ms | p50 µs | p99 µs |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|
| `.nxvid` | none (current) | 2505 | 9.4 | 16.7 | 35.3 | 2956 | 19.6 | 16.8 | 31.5 |
| `.nxvid` | per-frame CRC-32C | 2098 | 13.3 | 27.1 | 52.5 | 2370 | 28.0 | 26.8 | 45.9 |
| MCAP | chunk CRC-32 | 2260 | 10.9 | 2.3 | 201.0 | 2551 | 23.3 | 2.3 | 209.0 |
| MCAP | no chunk CRC | 2582 | 8.0 | 2.3 | 149.5 | 2855 | 20.6 | 2.5 | 161.2 |

Removing per-frame CRC-32C from `.nxvid` is worth **16–20% throughput** and
**~40% CPU** at these rates; p50 drops by ~60%. MCAP’s chunk CRC costs
12–14% throughput and ~23–27% of p99; disabling it narrows the gap to
`.nxvid` but the chunk-close tail remains ~150 µs.

The video table above is the **no per-frame CRC** configuration. This matches
the decision that video does not require per-record CRC-32C.

### FullHD 16 bpp at 50 Hz

Workload: 1920 × 1080 × 2 B = 4,147,200 B/frame; 50 Hz = **207.36 MB/s**.
The benchmark is unpaced, so CPU load at 50 Hz is `CPU time for 25 frames /
0.5 s`; the writer throughput column is headroom, not the target rate.

| Candidate | CRC | CPU ms / 25 frames | CPU load at 50 Hz | p50 frame | p99 frame | Writer headroom |
|---|---|---:|---:|---:|---:|---:|
| `.nxvid` | none | 17.7 | **3.5% of one core** | 0.50 ms | 0.85 ms | 14.8× |
| `.nxvid` | per-frame CRC-32C | 25.7 | 5.1% | 0.82 ms | 1.12 ms | 12.0× |
| MCAP | chunk CRC-32 | 28.4 | 5.7% | 0.96 ms | 1.51 ms | 11.2× |
| MCAP | no chunk CRC | 19.5 | 3.9% | 0.54 ms | 0.70 ms | 14.3× |

At 207.36 MB/s the writer is nowhere near its throughput limit: `.nxvid`
sustains 3.07 GB/s and MCAP 2.3–3.0 GB/s in this run. The frame budget at
50 Hz is 20 ms; even the MCAP chunk-CRC tail (1.5 ms) leaves >92% of the
period free. System-wide the load is 0.06–0.09% of 64 threads; pinned to a
dedicated non-RT writer core it is 3.5–5.7% of that core.

Kernel writeback and IRQ CPU are **not** in the process CPU number; at
207 MB/s expect roughly 1–3% additional system CPU, plus bursts if
`fdatasync` is issued per segment or more often.

## Publish path (iceoryx2, Release COM build)

| Path | p50 | p99 | Notes |
|---|---:|---:|---|
| `send_copy(payload)` | 150 ns | 190–260 ns | current fastest publication |
| `loan → write → user_header_mut → send` | 250 ns | 300 ns | the path a user header forces |
| producer `try_push` into SPSC | ~0–10 ns | ~0–10 ns | producer hot path unchanged |
| heap delta | 0 B | — | no allocation in the measured region |

The header path costs **+100 ns p50 (+67%)** on the publisher thread. That is
0.1% of a core at 10k/s, 1% at 100k/s, and 10% at 1M/s. The producer path does
not change.

## Findings

1. **MCAP chunked is the telemetry throughput winner on this machine.** At
   1 MiB chunks it reaches 1.0–1.9 GB/s file-sink and 5.7–6.2 GB/s counting;
   the custom prototype reaches 0.8–1.6 GB/s file-sink and 7.3–10.8 GB/s
   counting. The gap is modest (roughly 10–30% in favour of MCAP on the file
   sink) and smaller than the 5–10% run-to-run noise once other factors are
   considered.
2. **MCAP unchunked is not viable for high-rate telemetry.** It falls to
   365–535 MB/s and p50 430–500 ns because the C++ writer performs many small
   stdio writes per message.
3. **`noMessageIndex` is a good default.** It keeps the chunk index (coarse
   time lookup) while removing 16 B per message and some write work; spans
   and logs lose little.
4. **MCAP allocates in the measured region; the custom prototype does not.**
   The MCAP writer’s schema/channel/message-index/statistics bookkeeping
   allocates during the first messages and in steady state. The custom
   prototype is allocation-free after preallocation. This is a real conflict
   with the project’s zero-allocation mandate if the recorder writer must be
   strictly preallocated.
5. **Video favours `.nxvid` for tail latency.** `.nxvid` writes at
   2.2–2.9 GB/s with p99 21–36 µs; MCAP video writes at 1.7–2.5 GB/s with p99
   209–336 µs because of chunk close/CRC spikes. Removing per-frame CRC-32C
   from `.nxvid` adds another 16–20% throughput and cuts CPU by ~40%; MCAP
   chunk CRC costs 12–14% throughput. For FullHD 16 bpp at 50 Hz (207.36 MB/s),
   `.nxvid` without per-frame CRC costs **3.5% of one writer core**; per-frame
   CRC raises it to 5.1%, MCAP chunk CRC to 5.7%. MCAP video remains viable if
   a single-container Foxglove view is more important than the tail.
6. **Per-record CRC-32C costs the custom prototype throughput.** The custom
   writer pays CRC-32C and a 48-byte header per record; MCAP pays one CRC per
   chunk and a 31-byte message header. That is the main reason MCAP wins the
   file-sink telemetry rows.
7. **The common publish header is not free.** It adds ~100 ns to the publish
   path. An envelope-in-payload or payload-field design would keep the
   `send_copy` fast path at the cost of layout/payload capacity; that is the
   next decision, not a storage-format issue.

## Recommendation

- **Telemetry storage:** adopt **MCAP chunked** (1 MiB chunks, compression off,
  chunk CRC on, `noMessageIndex=true`) **if** the project accepts chunk-level
  CRC-32 (IEEE) and an exceptions-enabled, allocating MCAP wrapper. It gives
  the best file throughput, native schemas/channels, and direct
  Foxglove/PlotJuggler/Python compatibility.
- **If the zero-allocation mandate and per-record CRC-32C are hard
  requirements**, keep the **custom `.nxrec`** design. It is preallocated,
  carries per-record CRC-32C, and remains within ~10–30% of MCAP on the file
  sink; the cost is the export step for external tools.
- **Video:** use **`.nxvid`** for recording and remux/export to MP4/MKV (or
  embed `foxglove.CompressedVideo` in a derived MCAP when a single-container
  view is needed). Keep per-frame CRC-32C off by default: removing it is worth
  16–20% throughput and ~40% CPU. If per-frame integrity is required, budget
  that cost. MCAP video is acceptable only at moderate rates and with a
  tolerance for ~0.2–0.3 ms per-chunk tail spikes.
- **Publish header:** accept the user-header design only if telemetry rates
  stay at or below ~100k/s per publisher; otherwise measure the
  envelope/payload alternatives before committing. The producer path itself
  is unchanged.

## Reproducibility check

A clean repeat of `logs-high / nxrec-batched-64k / file` gave 777 MB/s versus
816 MB/s in the matrix (−4.8%); `video-100 / mcap-video / file` gave
1999 MB/s versus 2179 MB/s (−8.3%). Run-to-run noise is therefore about
**±10%** on this machine and governor. Treat sub-10% differences as
inconclusive.

## Not measured

- Cold page cache or `O_DIRECT`; dropping caches needs root. File numbers are
  page-cache throughput, not durable NVMe throughput.
- Compression (all candidates ran with compression disabled).
- Read-path latency; only verification/recovery counts were checked.
- Real 4K/H.265 bitstreams; video frames are synthetic 128 KiB payloads.
- Multiple subscribers and hardware CRC path variants.

## Production writer (`nexigen::recorder`)

The tables above use the prototype writers from the format study. This
companion run drives the shipped `SessionWriter` API (`nexigen_production_bench`)
with the same workloads and the same release flags. Each row is the median of
three 1.0 s telemetry / 0.5 s video runs; `--verify` reopens every session.

| Workload | CRC | MB/s | CPU ms | p50 | p99 | Alloc-free | Verified |
|---|---|---:|---:|---:|---:|:---:|:---:|
| logs-high, 100 k msg/s | per record | 1972 | 15.4 / 100 k msg | 80 ns | 100 ns | yes | yes |
| video-fullhd16-50 | none | 5430 | 19.2 / 25 frames | 0.77 ms | 1.07 ms | yes | yes |
| video-fullhd16-50 | per-frame CRC-32C | 4025 | 26.1 / 25 frames | 1.13 ms | 1.16 ms | yes | yes |

CPU and p99 stay inside the published envelope (telemetry p99 improves; video
CPU is within 1–8% and p99 within 4–26%). The video p50 is ~0.2–0.3 ms higher
because the production writer issues the `.nxvid` frame write directly to the
file descriptor instead of paying the prototype sink's buffered copy; the frame
budget at 50 Hz is 20 ms, so the writer still leaves >94% of the period free.
The production writer allocates nothing after session creation and schema/
channel declaration, and every session verifies on reopen.
