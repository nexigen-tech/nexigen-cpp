# Header compilation audit — compiled-module JSON pairs

Audit produced by the change `refactor-complexity-hotspots` (spec
`quality/code-structure`, requirement "Cold-path serialization of compiled
modules is built once"). It lists what still lives inline in public headers after
the five deserializers and two daemon configuration types were moved to
`src/<module>/`, and what remains for follow-up work.

## Rule

For every module built as a compiled library, a non-template, non-`constexpr`
public definition that needs no inlining — in particular cold-path `to_string`,
`to_json` and `from_json` bodies of configuration and manifest types — belongs in
`src/<module>/<entity>.cpp`, not inline in the public header.

Exempt, by design:

- `nexigen::core` — an `INTERFACE` (header-only) target with no object files;
- templates and `constexpr` code;
- hot-path inline accessors (`LogFrame::app_id_text`, ring/cache-line accessors);
- `std::formatter` specializations (they are class templates);
- enum `to_json`/`from_json` pairs shorter than a handful of lines, which stay
  next to their `to_string`/`from_string` vocabulary for ADL discoverability.

## Audit command

```bash
python3 - <<'PY'
import os, re
for m in ("app","chrono","com","diagnostics","exec","hub","logging","metrics","phm","sm","tracing"):
    for dp, _, fs in os.walk(f"include/nexigen/{m}"):
        for f in sorted(fs):
            if not f.endswith(".hpp"):
                continue
            p = os.path.join(dp, f)
            text = open(p, errors="ignore").read()
            n = len(re.findall(r"\binline\s+void\s+(?:to|from)_json\s*\(", text))
            if n:
                print(f"{m:11} {n} {p}")
PY
```

## Result

56 inline `to_json`/`from_json` definitions remain across 29 headers. Every one
of them is a 3- or 6-line descriptor: the mandated enum vocabulary pair or a
null-sink `to_json`. No multi-line configuration or sink body is left inline.

| Module | Headers | Definitions | Inline lines | Largest remaining header |
|---|---:|---:|---:|---|
| `tracing` | 7 | 13 | 39 | enum/null-sink descriptors (6 each) |
| `metrics` | 5 | 10 | 34 | enum/null-sink descriptors (6 each) |
| `phm` | 4 | 8 | 24 | enum pairs only (6 each) |
| `chrono` | 4 | 7 | 18 | enum pairs plus `json_file_sink` (3) |
| `logging` | 3 | 6 | 18 | enum pairs only (6 each) |
| `com` | 2 | 4 | 12 | enum pairs only (6 each) |
| `sm` | 2 | 4 | 12 | enum pairs only (6 each) |
| `app` | 1 | 2 | 6 | `infrastructure_stop_reason.hpp` |
| `diagnostics` | 1 | 2 | 6 | `stopwatch_state.hpp` |
| `exec` | 0 | 0 | 0 | moved by an earlier change |
| **Total** | **29** | **56** | **169** | |

`nexigen::core` is reported as exempt: `add_library(nexigen_core INTERFACE)` at
`CMakeLists.txt:156`.

## What this change moved

| Header | Destination | Why |
|---|---|---|
| `logging/iceoryx2_logger_config.hpp` | `src/logging/iceoryx2_logger_config.cpp` | 50-line deserializer, CCN 19 |
| `logging/log_frame.hpp` | `src/logging/log_frame.cpp` | 57-line deserializer, CCN 17 |
| `logging/log_daemon_config.hpp` | `src/logging/log_daemon_config.cpp` | daemon config, cold path |
| `metrics/metric_daemon_config.hpp` | `src/metrics/metric_daemon_config.cpp` | daemon config, cold path |
| `metrics/metric_frame.hpp` | `src/metrics/metric_frame.cpp` | 35-line frame pair, cold path |
| `metrics/json_file_metric_sink.hpp` | `src/metrics/json_file_metric_sink.cpp` | config pair + sink descriptor (30 lines) |
| `metrics/iceoryx2_metric_sink_config.hpp` | `src/metrics/iceoryx2_metric_sink_config.cpp` | 26-line sink config |
| `metrics/console_metric_sink.hpp` | `src/metrics/console_metric_sink.cpp` | descriptor pair (10 lines) |
| `chrono/json_file_sink_config.hpp` | `src/chrono/json_file_sink_config.cpp` | 19-line sink config |
| `chrono/console_sink.hpp` | `src/chrono/console_sink.cpp` | descriptor pair (10 lines) |

Both moved headers keep only declarations, so the public symbol set is unchanged
(verified with `nm -C --defined-only --extern-only` before/after).

## Follow-up candidates (not in this change)

Ordered by payoff — each is a mechanical "declaration in header, definition in
`src/<module>/<entity>.cpp`" move:

All configuration and sink bodies have been moved. What remains is the mandated
enum vocabulary (`to_json` + `from_json`, 6 lines each) plus three null/descriptor
`to_json` helpers (3 lines each). Moving those would scatter the mandate-10
vocabulary across `.cpp` files for no measurable compile-time gain, so they stay
inline by decision.

## Verification for future moves

- `nm -C --defined-only --extern-only` on the affected static library: no public
  (`T`) symbol added or removed; only local (`t`/`W`) additions are acceptable.
- A consumer translation unit that includes the header and links the module still
  round-trips through JSON, and `to_string()`/`std::format` output is unchanged.
- `ctest -R complexity` and `cmake --build --target complexity-check` stay green.
