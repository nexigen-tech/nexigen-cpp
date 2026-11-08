# Proposal

## Why

The `nexigen::logging` headers are "kitchen sink" files: several unrelated types co-locate in a single header, so navigating to a type means opening a 300–500 line file and finding it among neighbours. `logger_options.hpp` holds two enums and two structs; `file_logger.hpp` holds a sink, two config structs, the logger class, and two re-export aliases. This also causes include bloat — a consumer that only needs `FileLoggerConfig` or `LogLevel` today includes the whole async engine (`Producer`, `AsyncWorker`, `Rotation`) transitively. The project-wide convention "one file per entity" removes both problems, and this change applies it to logging as the exemplar for the other modules.

## What Changes

- Split `include/nexigen/logging/*.hpp` so each top-level `enum`/`struct`/`class` gets its own header. Highlights:
  - `logger_options.hpp` → `overflow_policy.hpp`, `shard_state.hpp`, `spam_protection_config.hpp`, `logger_options.hpp`.
  - `console_logger.hpp` → `detail/console_sink.hpp`, `console_logger_config.hpp`, `console_logger.hpp`.
  - `file_logger.hpp` → `detail/file_sink.hpp`, `rotation_config.hpp`, `file_logger_config.hpp`, `file_logger.hpp`.
  - `detail/producer.hpp` → `dropped_stats.hpp` (public), `detail/throttle_config.hpp`, `detail/producer.hpp`.
  - `detail/rotation.hpp` → `detail/rotation_kind.hpp`, `detail/rotation_scratch.hpp`, `detail/rotation.hpp` (free-function utility).
  - `detail/spam_protection.hpp` → `detail/spam_shard_state.hpp`, `detail/spam_protection.hpp`.
  - `detail/arg_pack.hpp` → `detail/bounded_sink.hpp` + `detail/arg_pack.hpp` (pack/unpack utility + `Packable` concept stays in `detail/`).
  - `format_with_location.hpp` → `format_with_location.hpp` + `detail/format_param.hpp`.
- Promote three public vocabulary types out of `detail` and delete their re-export aliases:
  - `detail::ThrottleConfig` → `nexigen::logging::ThrottleConfig` (`throttle_config.hpp`).
  - `detail::RotationKind` → `nexigen::logging::RotationSchedule` (`rotation_schedule.hpp`); the public `RotationSchedule` alias becomes the enum itself.
  - `DroppedStats` moves from `detail/producer.hpp` to public `dropped_stats.hpp` (its namespace is already `nexigen::logging`).
- Rename files to match their entity: `detail/async_logger.hpp` → `detail/async_worker.hpp` (class `AsyncWorker`); `logging_settings.hpp` → `settings.hpp` (struct `Settings`, which also hosts `load_logging_settings` and `check_capacity`).
- Split `src/logging/*.cpp` into per-entity translation units (`spam_protection_config.cpp`, `logger_options.cpp`, `settings.cpp`, `file_logger_config.cpp`, `console_logger_config.cpp`) keeping `rotation.cpp` and `json_format.cpp`.
- Update all `#include <nexigen/logging/...>` sites across `tests/logging`, `runner`, `app`, `examples`, and `tests/packaging`.
- Add the project-wide "file-per-entity layout" convention to `openspec/config.yaml` `context:` (the anchor that later per-module changes reuse).

No behavior changes: every type keeps its public name, serialization mandates (`to_string`, `std::formatter`, ADL `to_json`/`from_json`), `alignas(64)` layouts, and memory-order choices. The only source-level change is that `detail::ThrottleConfig`, `detail::RotationKind`, and `detail::DroppedStats` cease to exist under those names — all three were re-exported to public `nexigen::logging::*` names that are preserved.

## Capabilities

Pure refactor — no spec-level behavior changes. `skip_specs: true` is set in `.openspec.yaml`.

### New Capabilities

None.

### Modified Capabilities

None.

## Impact

- **Code**: ~18 logging headers split into ~33; 4 `src/logging/*.cpp` split into ~7; `#include` updates in `include/nexigen/{runner,app}/*.hpp`, `src/{runner,logging}/*.cpp`, `tests/{logging,runner,app,packaging}`, and `examples/*`. `CMakeLists.txt` needs no change: headers are not listed and `src/logging/*.cpp` is already globbed with `CONFIGURE_DEPENDS`.
- **Dependencies**: include-graph only. Consumers that need a single config type or `LogLevel` no longer pull in the async engine transitively. No new external dependencies; `nexigen_logging` still links `nexigen_core` + `nexigen_chrono`.
- **Performance & memory**: zero hot-path impact. This is a pure header/translation-unit reorganization — no statement on any producer, consumer, or worker path changes. The `alignas(64)` `LogRecord`/`Producer`/`LevelControl`/`SpamShardState` layouts, the relaxed/acquire/release `std::memory_order` choices, and the `static_assert(sizeof(LogRecord<200>) == 256)` contract are preserved verbatim. No heap allocation is added or removed; the ≤ 20 ns Release enqueue latency gate must keep passing unchanged. Splitting cold-path `to_string`/`to_json` bodies into more TUs may marginally change link-time layout but not runtime behavior.
- **Compatibility**: non-breaking for the public API — `nexigen::logging::ThrottleConfig`, `RotationSchedule`, and `DroppedStats` keep their names. Source-breaking only for code reaching into `nexigen::logging::detail::` for those three types (not public API, no shims retained).
