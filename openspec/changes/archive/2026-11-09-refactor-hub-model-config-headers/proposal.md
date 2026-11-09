# Proposal

## Why

The repo codifies file-per-entity in `openspec/config.yaml` ("Each top-level
enum, struct, and class lives in its own header, named snake_case of the type"),
and this week's archived changes (`refactor-*-file-per-entity`) applied it across
logging/metrics/runner/tracing. Two debts remain from the analysis:

1. `include/nexigen/hub/model.hpp` bundles **9 top-level DTOs**
   (`HubStream`, `HubAttempt`, `HubInstance`, `HubApp`, `HubSuite`, `HubRun`,
   `HubTree`, `StreamRecord`, `GapMarker`) — the largest surviving god-header.
   The earlier `refactor-hub-file-per-entity` only extracted `stream_kind.hpp`.
2. The sink/provider Config pattern is inconsistent: logging/chrono keep configs
   in separate headers (`console_logger_config.hpp`,
   `json_file_chrono_sink_config.hpp`), while tracing/metrics embed them:
   `ChromeTraceSinkConfig` in `chrome_trace_sink.hpp`,
   `JsonFileTraceSinkConfig` + `RotationPolicy` in `json_file_trace_sink.hpp`,
   `JsonFileMetricSinkConfig` in `json_file_metric_sink.hpp`, and
   `TracerProviderConfig` + `DroppedStats` in `tracer_provider.hpp` (3 entities).

## What Changes

- Split `hub/model.hpp` into 9 DTO headers under `include/nexigen/hub/`
  (`hub_stream.hpp` … `gap_marker.hpp`), one entity each; delete `model.hpp`
  (no umbrella — only 7 consumers) and update: `hub/discovery.hpp`,
  `hub/streaming.hpp`, `src/hub/{discovery,streaming,http_server}.cpp`,
  `tests/hub/hub_test_support.hpp`.
- Keep the serialization bodies in ONE compiled hub unit, renamed to match the
  existing precedent (`src/tracing/record_format.cpp`, `sink_format.cpp`):
  `src/hub/model.cpp` → `src/hub/hub_format.cpp`.
- Unify the config pattern by extracting:
  `chrome_trace_sink_config.hpp`, `json_file_trace_sink_config.hpp`
  (+ `rotation_policy.hpp` for the enum), `json_file_metric_sink_config.hpp`,
  `tracer_provider_config.hpp`, `dropped_stats.hpp`.
- No type, field, vocabulary or wire-format changes — file moves and `#include`
  updates only.

## Capabilities

Pure refactor — include graph and file layout only. `skip_specs: true`.

## Impact

- Runtime: none (cold read-model and init-time configs; no header change reaches
  a producer/sink hot path). No allocation or latency impact whatsoever.
- Compile time: slightly improved for hub consumers (they pull the DTOs they use,
  not all nine).
- Build files: header list/CMake sources update for the renamed `hub_format.cpp`
  unit; `install(DIRECTORY include/nexigen ...)` needs no change.
- Docs: `docs/nexigen-hub.md` and README layout notes reference `model.hpp` —
  update.
- Ordering: land AFTER `refactor-enum-traits-helper` — that change rewrites the
  enum glue inside `json_file_trace_sink.hpp`/`span_record.hpp`, which this
  change then moves; doing them in reverse would conflict.
