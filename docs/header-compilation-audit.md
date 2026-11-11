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

69 inline `to_json`/`from_json` definitions remain across 35 headers.

| Module | Headers | Definitions | Inline lines | Largest remaining header |
|---|---:|---:|---:|---|
| `tracing` | 7 | 13 | 39 | `span_record.hpp` (6) |
| `chrono` | 6 | 11 | 50 | `json_file_sink_config.hpp` (19) |
| `metrics` | 9 | 19 | 129 | `metric_frame.hpp` (35) |
| `phm` | 4 | 8 | 24 | enum pairs only (6 each) |
| `logging` | 3 | 6 | 18 | enum pairs only (6 each) |
| `sm` | 2 | 4 | 12 | enum pairs only (6 each) |
| `com` | 2 | 4 | 12 | enum pairs only (6 each) |
| `app` | 1 | 2 | 6 | `infrastructure_stop_reason.hpp` |
| `diagnostics` | 1 | 2 | 6 | `stopwatch_state.hpp` |
| `exec` | 0 | 0 | 0 | moved by this change |
| **Total** | **35** | **69** | **296** | |

`nexigen::core` is reported as exempt: `add_library(nexigen_core INTERFACE)` at
`CMakeLists.txt:156`.

## What this change moved

| Header | Destination | Why |
|---|---|---|
| `logging/iceoryx2_logger_config.hpp` | `src/logging/iceoryx2_logger_config.cpp` | 50-line deserializer, CCN 19 |
| `logging/log_frame.hpp` | `src/logging/log_frame.cpp` | 57-line deserializer, CCN 17 |
| `logging/log_daemon_config.hpp` | `src/logging/log_daemon_config.cpp` | daemon config, cold path |
| `metrics/metric_daemon_config.hpp` | `src/metrics/metric_daemon_config.cpp` | daemon config, cold path |

Both moved headers keep only declarations, so the public symbol set is unchanged
(verified with `nm -C --defined-only --extern-only` before/after).

## Follow-up candidates (not in this change)

Ordered by payoff — each is a mechanical "declaration in header, definition in
`src/<module>/<entity>.cpp`" move:

1. `include/nexigen/metrics/metric_frame.hpp` — 35 inline lines.
2. `include/nexigen/metrics/json_file_metric_sink.hpp` — 30 inline lines.
3. `include/nexigen/metrics/iceoryx2_metric_sink_config.hpp` — 26 inline lines.
4. `include/nexigen/chrono/json_file_sink_config.hpp` — 19 inline lines.
5. `include/nexigen/chrono/console_sink.hpp`, `metrics/console_metric_sink.hpp` — 10 lines each.

The 24 remaining enum pairs (6 inline lines each: `to_json` + `from_json`) are
deliberately deferred: moving them would scatter the mandated enum vocabulary
(mandate 10) across `.cpp` files for a marginal compile-time gain.

## Verification for future moves

- `nm -C --defined-only --extern-only` on the affected static library: no public
  (`T`) symbol added or removed; only local (`t`/`W`) additions are acceptable.
- A consumer translation unit that includes the header and links the module still
  round-trips through JSON, and `to_string()`/`std::format` output is unchanged.
- `ctest -R complexity` and `cmake --build --target complexity-check` stay green.
