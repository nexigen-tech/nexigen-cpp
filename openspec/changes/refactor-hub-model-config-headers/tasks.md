# Tasks

## 1. Hub DTO split

- [x] 1.1 Create 9 DTO headers (`hub_stream.hpp`, `hub_attempt.hpp`,
      `hub_instance.hpp`, `hub_app.hpp`, `hub_suite.hpp`, `hub_run.hpp`,
      `hub_tree.hpp`, `stream_record.hpp`, `gap_marker.hpp`), declarations
      moved verbatim from `model.hpp`, each with its own doc comment and
      `#include` closure. `stream_kind.hpp` also gained StreamKind's
      to_json/from_json declarations (enum-header precedent, app_state.hpp).
- [x] 1.2 Rename `src/hub/model.cpp` → `src/hub/hub_format.cpp` (bodies
      unchanged), update CMake source list. Sources are `file(GLOB …
      CONFIGURE_DEPENDS)`, so the rename is picked up on reconfigure — no
      manual source-list edit needed.
- [x] 1.3 Delete `model.hpp`; update the 7 consumers
      (`hub/discovery.hpp`, `hub/streaming.hpp`, `src/hub/{discovery,streaming,http_server}.cpp`,
      `tests/hub/hub_test_support.hpp`) to include the DTO headers they use.
- [x] 1.4 Update `docs/nexigen-hub.md` and README layout notes.
      `docs/nexigen-hub.md` holds no header-path references; README's
      `<nexigen/hub/model.hpp>` entry now describes `hub_tree.hpp` + the DTO set.

## 2. Config-header unification

- [x] 2.1 Extract `ChromeTraceSinkConfig` → `chrome_trace_sink_config.hpp`.
- [x] 2.2 Extract `JsonFileTraceSinkConfig` → `json_file_trace_sink_config.hpp`
      and `RotationPolicy` → `rotation_policy.hpp`.
- [x] 2.3 Extract `JsonFileMetricSinkConfig` → `json_file_metric_sink_config.hpp`.
- [x] 2.4 Extract `TracerProviderConfig` → `tracer_provider_config.hpp` and
      `DroppedStats` → `dropped_stats.hpp`.
- [x] 2.5 Update includes across `src/tracing`, `src/metrics`, tests, examples;
      verify provider/sink headers include their configs (not vice versa).
      Direction is sink/provider → config (never reverse); existing consumers
      keep working transitively; serialization bodies stay in
      src/tracing/{sink_format,record_format}.cpp unchanged.

## 3. Gates and verification

- [x] 3.1 Re-run the entity audit (`/tmp/opencode/entity_audit.py`): no header
      under `include/` holds >1 top-level entity outside the accepted
      passkey/concepts/detail exceptions. The four sink/provider headers no
      longer appear as multi-entity (they were 2–3 before); the new
      `rotation_policy.hpp` carries only `RotationPolicy + EnumTraits`, the
      same accepted enum-vocabulary pattern as every other enum header.
- [x] 3.2 Run clang-format/clang-tidy, naming and result-value guards, CCN gate.
      clang-format clean on the working set; complexity rc=0 at threshold 20;
      naming/result-value gated (baseline dirt — tools/* naming,
      record_format_test .value() — notices only). clang-tidy skipped (no
      changed src/*.cpp TUs; this change is header-only).
- [x] 3.3 Compile and pass 100% of unit tests under Debug, Release
      (`-O3 -march=native -flto` with latency gates), ThreadSanitizer
      (`-DNEXIGEN_ENABLE_TSAN=ON`) and AddressSanitizer (`-DNEXIGEN_ENABLE_ASAN=ON`).
      VALIDATION GREEN (ci-tier): Debug/Release/ASAN/TSAN all 37/37;
      matrix wall ~9 min (Release 364 s / ASAN 325 s / TSAN 521 s in
      parallel), ccache 99.65% cacheable.
