# Design

## Context

See proposal.md — Why. Verified facts:

- `include/nexigen/hub/model.hpp` (169 lines) is the last god-header: 9 DTOs
  (`HubStream`, `HubAttempt`, `HubInstance`, `HubApp`, `HubSuite`, `HubRun`,
  `HubTree`, `StreamRecord`, `GapMarker`), each with a `to_string()` member
  (declared, bodies in `src/hub/model.cpp`), an inline free `to_string`
  helper, and `to_json`/`from_json` ADL declarations (bodies in model.cpp).
  It also carries two tree-query free functions (`all_streams`, `find_stream`,
  bodies in model.cpp:317/344).
- `AppState`/`SuiteState` already follow file-per-entity
  (`runner/app_state.hpp`, `runner/suite_state.hpp`); `StreamKind` already in
  `hub/stream_kind.hpp`. So the 9 DTOs reference only other entity headers —
  the split has no new type identity to settle.
- Config inconsistency: `ChromeTraceSinkConfig` (chrome_trace_sink.hpp:45),
  `JsonFileTraceSinkConfig` + `RotationPolicy` (json_file_trace_sink.hpp:58/105),
  `JsonFileMetricSinkConfig` (json_file_metric_sink.hpp:32, inline to_string),
  `TracerProviderConfig` + `DroppedStats` (tracer_provider.hpp:75/100) are all
  embedded in sink/provider headers, unlike the logging/chrono precedent.
- The 7 consumers of model.hpp are `hub/discovery.hpp`, `hub/streaming.hpp`,
  `src/hub/{model,discovery,streaming,http_server}.cpp`,
  `tests/hub/hub_test_support.hpp`.

## Goals / Non-Goals

**Goals:**
- One entity per header under `include/nexigen/hub/` (9 new), `model.hpp` gone.
- Config structs/enums extracted to own headers, matching logging/chrono.
- Byte-identical behavior: pure file/`#include` moves, no type/field/vocabulary
  change; serialization bodies stay in one compiled unit.

**Non-Goals:**
- No umbrella `model.hpp` replacement (proposal: "no umbrella").
- No change to runtime behavior, wire format, or diagnostics.
- No touching the `refactor-enum-traits-helper` outcome (already landed) beyond
  what a pure move requires.

## Decisions

1. **D1 — DTO split, verbatim, self-closing headers.** Each of the 9 headers
   carries: the struct declaration (moved verbatim), its own inline free
   `to_string` helper, and its own `to_json`/`from_json` declarations. Each
   header includes exactly the entity headers its fields name (closure):
   - `hub_stream.hpp` ← `stream_kind.hpp`, `<string>`
   - `hub_attempt.hpp` ← `hub_stream.hpp`, `<cstdint> <string> <vector>`
   - `hub_instance.hpp` ← `runner/app_state.hpp`, `hub_attempt.hpp`, `<string> <vector>`
   - `hub_app.hpp` ← `runner/app_state.hpp`, `hub_instance.hpp`, `<string> <vector>`
   - `hub_suite.hpp` ← `runner/suite_state.hpp`, `hub_stream.hpp`, `hub_app.hpp`,
     `<cstdint> <string> <vector>`
   - `hub_run.hpp` ← `hub_suite.hpp`, `hub_instance.hpp`, `<string> <vector>`
   - `hub_tree.hpp` ← `hub_run.hpp`, `<vector>`
   - `stream_record.hpp` ← `stream_kind.hpp`, `<cstdint> <string>`
   - `gap_marker.hpp` ← `<cstdint> <string>`
   The `to_json`/`from_json` declarations take `nlohmann::json&`, which needs
   the full definition, so each DTO header keeps `#include <nlohmann/json.hpp>`
   (as `model.hpp` does today).
2. **D2 — serialization unit rename.** `src/hub/model.cpp` →
   `src/hub/hub_format.cpp` (bodies unchanged; only its `#include
   <nexigen/hub/model.hpp>` becomes the 9 DTO headers). CMake source list
   updates the one path. This mirrors the existing precedent
   (`src/tracing/record_format.cpp`, `sink_format.cpp`).
3. **D3 — tree queries home.** `all_streams`/`find_stream` (return
   `std::vector<HubStream>` / `const HubStream*`, take `const HubTree&`) move
   into `hub_tree.hpp` — they are projections over the tree, and `hub_tree.hpp`
   already pulls `hub_run.hpp` transitively; it additionally includes
   `hub_stream.hpp` for the return type. Bodies stay in `hub_format.cpp`.
4. **D4 — config extraction, six headers.** Each config type moves verbatim:
   `chrome_trace_sink_config.hpp`, `json_file_trace_sink_config.hpp`,
   `rotation_policy.hpp` (enum), `json_file_metric_sink_config.hpp`,
   `tracer_provider_config.hpp`, `dropped_stats.hpp`. Direction: the sink/
   provider header now `#include`s its config header (never the reverse), so a
   consumer can include just the config. `RotationPolicy` is used by
   `JsonFileTraceSinkConfig`, so `json_file_trace_sink_config.hpp` includes
   `rotation_policy.hpp`.
5. **D5 — no umbrella, update consumers.** `model.hpp` is deleted. The 7
   consumers switch to the specific DTO headers they use:
   `discovery.hpp`/`streaming.hpp` (HubTree + the DTOs they name),
   `src/hub/*.cpp`, `hub_test_support.hpp`. Any consumer that only used
   `all_streams`/`find_stream` now gets them from `hub_tree.hpp`.
6. **D6 — verification.** The entity audit (`/tmp/opencode/entity_audit.py`)
   must show no multi-entity header. Gates + the full 4-config test matrix run
   through `scripts/validate.sh --tier ci` (now ~5 min thanks to
   speedup-validation-loop), plus `build-verify-clang` for the clang side.

## Risks / Trade-offs

- Include-order/closure mistakes surface as missing-type compile errors; the
  closure table in D1 is the source of truth — every field's type must be
  visible in the header that names it.
- Serialization bodies must still see the full DTO set (hub_format.cpp includes
  all nine + `hub_tree.hpp`), so no ADL lookup breaks.
- `nlohmann/json.hpp` in every DTO header keeps cold-path compile cost high for
  hub consumers, but PCH (speedup-validation-loop) already amortizes it.

## Open Questions

- None. Pure mechanical refactor; the proposal's ordering note
  (`refactor-enum-traits-helper` lands first) is already satisfied — the enum
  glue lives in `core/enum_traits.hpp` and `stream_kind.hpp` today.
