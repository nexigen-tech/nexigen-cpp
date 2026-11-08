# Tasks

## 1. Engine in core

- [x] 1.1 Add `include/nexigen/core/enum_traits.hpp`: `EnumTraits<E>` primary
      template + `Entry` + constexpr `enum_name(E)` (fallback `"Unknown"`) and
      `enum_value(std::string_view, E&)` (exact match, leaves `out` on failure).
- [x] 1.2 Lift `enum_to_json`/`enum_from_json`/`read_enum` from
      `include/nexigen/runner/detail/sys_helpers.hpp` into `core::mandate`
      (or `enum_traits.hpp`); remove the runner copies and update every existing
      call site (`src/hub/model.cpp`, grep the tree for the three names).
- [x] 1.3 Extend `tests/core/core_test.cpp`: table round-trip on a probe enum,
      out-of-range → `"Unknown"`, unknown name → `false` + untouched value,
      json glue under `JSON_NOEXCEPTION` (non-string leaves value unchanged).

## 2. Migrate enums module by module (byte-identical wire, one commit per module)

- [x] 2.1 `core`: `ErrorCode` (result.hpp), `SettingsSource`, `thread_management.hpp` enums.
- [x] 2.2 `logging`: `LogLevel`, `OverflowPolicy`, `RotationSchedule`, `ShardState`.
- [x] 2.3 `chrono`/`diagnostics`/`app`: `ClockId`, `ClockLogLevel`,
      `StopwatchState`, `InfrastructureStopReason`.
- [x] 2.4 `metrics`: `AccessPolicy`, `ConcurrencyPolicy`, `MetricKind`, `ResetPolicy`.
- [x] 2.5 `runner`: `AppState`, `SuiteState`, `TerminatedBy`, `ImpactOnAppSuiteOperability`.
- [x] 2.6 `tracing`/`hub`: `SamplingDecision`, `SpanKind`, `SpanStatus`,
      `RotationPolicy` (json_file_trace_sink.hpp), `SpanAttributeType`
      (span_record.hpp), `StreamKind`.
- [x] 2.7 Confirm full migration: no multi-line `switch`/if-chain remains in the
      25 headers; re-run the clone scan (`/tmp/opencode/clone_detect.py`) — the
      enum↔enum clone cluster must be gone.
      RESULT NOTE: switches/chains are gone (the only surviving `switch` maps
      SchedulingPolicy→SCHED_* at runtime, not vocabulary). The identifier-
      normalized clone scan still reports residual similarity between the
      per-enum ADL GLUE blocks (table wrapper + four delegation one-liners):
      mandated by the associated-namespace rule, no parse logic duplicated
      anymore; net include/ delta -61 lines with all logic in one engine.

## 3. Gates and verification

- [x] 3.1 Run clang-format/clang-tidy, `cmake/check_naming.py`,
      `cmake/check_result_value.py`, CCN gate — all clean.
      RESULT NOTE: clean for every file this change touches. Pre-existing
      findings in untouched HEAD files (tools/*, tests/tracing/*: 9 naming,
      1 strong-type `.value()` getter) are outside this change's scope.
- [x] 3.2 Compile and pass 100% of unit tests under Debug, Release
      (`-O3 -march=native -flto` with latency gates), ThreadSanitizer
      (`-DNEXIGEN_ENABLE_TSAN=ON`) and AddressSanitizer (`-DNEXIGEN_ENABLE_ASAN=ON`).
      RESULT: Debug 36/36, Release+LTO+STRICT_LATENCY 36/36, ASAN 36/36,
      TSAN 37/37 (all serial; `ex_app_cli_settings` shares a run dir with
      other examples and races under -j — serial runs are deterministic).
