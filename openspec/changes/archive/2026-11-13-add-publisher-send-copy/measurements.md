# Measurements

Release x86-64 (`build-release-com`, strict latency, same machine, best-of-3):

## Transport: `tests/com/latency_test.cpp`

| Path | Median | p99 | p99.9 | Max |
|---|---:|---:|---:|---:|
| Before — `(a) direct loan/send` | 187.6 ns | 257.9 ns | 398.1 ns | 3572.8 ns |
| After — `(a2) send_copy` | **137.5 ns** | 147.5 ns | 167.6 ns | 4854.7 ns |

Delta: **−50.1 ns** (~27%) per publication. `heap-delta 0 B` covers both paths.

## Trace sink: `Iceoryx2TraceSink::on_span`

| Revision | Median | p99 | p99.9 | Max |
|---|---:|---:|---:|---:|
| Before (`loan/write_payload/send`) | 210.3–220.6 ns | 260.6–310.7 ns | 360.8 ns | 3375–7661 ns |
| After (`send_copy`) | **150.5 ns** | 340.8 ns | 551.1 ns | 5548.6 ns |

Gate: median ≤400 ns — passes with ~2.6× headroom. The gain (−60…−70 ns) exceeds the
design's 10 ns keep-threshold, so the sink changes stay.

## Unchanged gates

- Logging producer latency: accepted median 0.50 ns (gate 20).
- Metrics hot path: `st_inc=0.25`, `mt_inc=3.70`, `gauge_set=6.38`, `gauge_add=3.69`,
  `stats=5.73`, `hist=4.63`, `expo=9.07` ns/op — all within their gates.
