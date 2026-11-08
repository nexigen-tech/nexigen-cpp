# Design

## Context

See `proposal.md` for motivation. This design covers only `nexigen::logging`; the convention it establishes (Section "Convention") is the anchor reused by the follow-up per-module changes.

Current state: `include/nexigen/logging/` has 18 headers; ~10 of them hold more than one top-level type. Two namespace↔file mismatches exist today — `DroppedStats` (namespace `nexigen::logging`) is defined inside `detail/producer.hpp`, and two public names (`ThrottleConfig`, `RotationSchedule`) are `using` aliases that re-export `detail::` types. `logging_settings.hpp` forward-declares `FileLoggerConfig`, whose `apply_to` overload is then defined in `file_logger.hpp` — a type "completed" in a different header than it is declared.

Constraints that shape every decision: the serialization mandates 9–11 (every public type needs `to_string`, and every enum needs `from_string` + ADL `to_json`/`from_json` + a `std::formatter` specialization), the hot-path mandates (no heap allocation, no locking, `alignas(64)`, explicit `std::memory_order`), and the namespace map (`nexigen::logging` → `include/nexigen/logging/`, `src/logging/`, `tests/logging/`).

## Goals / Non-Goals

**Goals:**
- One top-level `enum`/`struct`/`class` per header, so a type's file is findable and a consumer includes only what it uses.
- Remove the namespace↔file mismatches and the cross-header "completed type" forward-declaration.
- Encode the "file-per-entity" rule as a project-wide convention in `openspec/config.yaml`.

**Non-Goals:**
- No behavior change, no API redesign, no new/removed types, no spec-level requirement changes (`skip_specs: true`).
- Other modules (`core`, `chrono`, `metrics`, `tracing`, `diagnostics`, `runner`, `hub`, `app`) are out of scope — they follow as separate changes reusing this convention.

## Decisions

### D1 — Granularity rule

| Kind | Home |
|---|---|
| Top-level `enum` / `struct` / `class` | its own header, named `snake_case` of the type |
| Public `concept` | `concepts/<snake_case>_concept.hpp` |
| `detail`-only `concept` | stays in `detail/` beside its owner |
| Private nested type (`Cell`, `FlushCell`, `StartCell`, `FileSink::Settings`) | stays nested — no own header |
| `using` alias | no own header; lives with the type it aliases or the header that needs it |
| Free function(s) | in the header of the entity they serve; a cohesive group owned by no single entity keeps one utility header |

*Rationale:* the top-level split is what fixes navigation and include bloat; splitting private nested cells (`alignas(64)` padding structs) would scatter implementation detail and add headers with no independent value. *Alternative considered:* split nested types too — rejected, they are private layout details.

### D2 — Target file tree (~33 headers)

```
include/nexigen/logging/
├── log_level.hpp                 enum LogLevel
├── overflow_policy.hpp           enum OverflowPolicy            (from logger_options)
├── shard_state.hpp               enum ShardState                (from logger_options)
├── spam_protection_config.hpp    struct SpamProtectionConfig    (from logger_options)
├── logger_options.hpp            struct LoggerOptions
├── settings.hpp                  struct Settings + load_logging_settings + check_capacity
│                                 (was logging_settings.hpp)
├── log_record.hpp                struct LogRecord + record_render_fn
├── format_with_location.hpp      struct FormatWithLocation
├── rotation_config.hpp           struct RotationConfig
├── rotation_schedule.hpp         enum RotationSchedule           (promoted detail::RotationKind)
├── throttle_config.hpp           struct ThrottleConfig           (promoted detail::ThrottleConfig)
├── dropped_stats.hpp             struct DroppedStats             (from detail/producer.hpp)
├── file_logger_config.hpp        struct FileLoggerConfig + apply_to(Settings, FileLoggerConfig&)
├── console_logger_config.hpp     struct ConsoleLoggerConfig + apply_to(Settings, ConsoleLoggerConfig&)
├── null_logger.hpp               class NullLogger
├── console_logger.hpp            class ConsoleLogger
├── file_logger.hpp               class FileLogger
├── composite_logger.hpp          class CompositeLogger
├── concepts/logger_concept.hpp   concept Logger
└── detail/
    ├── console_sink.hpp          class ConsoleSink
    ├── file_sink.hpp             class FileSink (+ nested Settings)
    ├── producer.hpp              class Producer + cached_thread_id()
    ├── async_worker.hpp          class AsyncWorker               (was async_logger.hpp)
    ├── level_control.hpp         class LevelControl (+ nested Cell)
    ├── spam_shard_state.hpp      struct SpamShardState
    ├── spam_protection.hpp       class SpamProtection
    ├── rotation_kind.hpp         enum RotationKind               (detail machine type)
    ├── rotation_scratch.hpp      struct RotationScratch
    ├── rotation.hpp              period_key/period_label/open_period/open_segment (utility)
    ├── arg_pack.hpp              pack_one/unpack_one/… + flags + concept Packable (utility)
    ├── bounded_sink.hpp          struct BoundedSink
    ├── format_param.hpp          struct format_param + format_param_t
    ├── json_writer.hpp           format_json_line + json_scratch_capacity (utility)
    └── affinity.hpp              using pin_self/self_affinity (utility)
```

### D3 — Promote vocabulary types out of `detail`, delete aliases

`ThrottleConfig`, `RotationKind`, and `DroppedStats` are public config/diagnostic vocabulary (used by `FileLoggerConfig` and every logger's `dropped()`), so they should be real public types, not `detail` types re-exported through `using`. The public spellings `nexigen::logging::ThrottleConfig`, `nexigen::logging::RotationSchedule`, and `nexigen::logging::DroppedStats` are preserved; only the `detail::` names disappear.

- `detail::ThrottleConfig` → `nexigen::logging::ThrottleConfig` (`throttle_config.hpp`), the `using ThrottleConfig = detail::ThrottleConfig` alias deleted.
- `detail::RotationKind` → renamed to `nexigen::logging::RotationSchedule` (`rotation_schedule.hpp`). `detail::` code (`FileSink`, `rotation.hpp`, `src/logging/rotation.cpp`) switches to `RotationSchedule`. The `using RotationSchedule = detail::RotationKind` alias deleted.
- `DroppedStats` moves to `dropped_stats.hpp`; `producer.hpp` includes it.

*Rationale:* a consumer needing `ThrottleConfig` includes `throttle_config.hpp` alone instead of `file_logger.hpp` (which drags in `Producer`/`AsyncWorker`). *Alternative considered:* keep the `detail` types and re-home the aliases — rejected, that leaves the same indirection and still couples the alias to a larger header.

### D4 — Rename files to match their entity

- `detail/async_logger.hpp` → `detail/async_worker.hpp` (the class is `AsyncWorker`).
- `logging_settings.hpp` → `settings.hpp` (the struct is `Settings`).

### D5 — Free-function placement

- `load_logging_settings` + `check_capacity` (header template) stay with `Settings` in `settings.hpp`; body in `src/logging/settings.cpp`.
- `apply_to(Settings, FileLoggerConfig&)` moves to `file_logger_config.hpp` (+ body in `src/logging/file_logger_config.cpp`); `apply_to(Settings, ConsoleLoggerConfig&)` moves to `console_logger_config.hpp`. This deletes the `struct FileLoggerConfig;` forward declaration from `settings.hpp` and removes the cross-header "completed type" smell.

### D6 — `src/logging` translation-unit split

```
src/logging/
├── spam_protection_config.cpp   SpamProtectionConfig::to_string + to_json/from_json
├── logger_options.cpp           LoggerOptions::to_string + to_json/from_json
├── settings.cpp                 Settings::to_string + to_json + load_logging_settings
├── file_logger_config.cpp       FileLoggerConfig to_json/from_json (+ apply_to)
├── console_logger_config.cpp    ConsoleLoggerConfig to_json/from_json (+ apply_to)
├── rotation.cpp                 period_key/period_label/open_period/open_segment
└── json_format.cpp              format_json_line
```

Enums and templates stay header-only (`log_level.hpp`, `overflow_policy.hpp`, `shard_state.hpp`, `rotation_schedule.hpp`, `log_record.hpp`, `producer.hpp`, …). `src/logging/*.cpp` is already globbed in CMake with `CONFIGURE_DEPENDS`, so no `CMakeLists.txt` edit is required.

### D7 — Convention (exact text to add to `openspec/config.yaml` `context:`)

```
Code Organization — File-per-entity layout (ALL modules):
- Each top-level enum, struct, and class lives in its own header, named
  snake_case of the type (LogLevel -> log_level.hpp, FileLoggerConfig ->
  file_logger_config.hpp).
- Public types go in include/nexigen/<module>/; internal/implementation types
  go in include/nexigen/<module>/detail/ under <module>::detail.
- concepts/: only public concepts (e.g. nexigen::logging::concepts::Logger);
  detail-only concepts stay in detail/ beside their owner.
- Private nested types (cache-line cells, scratch buffers, sub-settings that
  exist only to serve their owner) stay nested inside the owning type.
- using aliases get no header of their own; they live with the type they alias
  or the header that needs them.
- Free functions live in the header of the entity they serve; a cohesive group
  owned by no single entity (rotation, argument packing, JSON line formatting)
  may share one utility header.
- The .cpp of a non-inline definition sits beside its header under src/<module>/,
  named identically; split into a .cpp only when a compiled TU keeps consumer
  translation units lean.
```

## Risks / Trade-offs

- **Header count grows ~18 → ~33** (and `src/logging` 4 → 7 TUs). More files to manage, but each is small and self-describing. → Mitigation: the convention makes the mapping mechanical; no file exceeds a single type + its serialization helpers.
- **`#include` churn across ~37 sites** (`tests/logging`, `runner`, `app`, `examples`, `tests/packaging`) risks a transient broken build if a site is missed. → Mitigation: `tasks.md` tracks include-site updates per split; the full build matrix (Debug / Release `-O3 -flto` / TSAN / ASAN) catches every missed include at compile time.
- **Accidental behavior drift** if a split reorders or re-templates a definition. → Mitigation: moves are copy-preserving (git mv / verbatim body relocation); the `alignas(64)` layouts, `std::memory_order` choices, and `static_assert(sizeof(LogRecord<200>) == 256)` are preserved exactly, and the latency gate + 100% unit tests must keep passing.
- **`detail::` name removal** (`detail::ThrottleConfig`, `detail::RotationKind`, `detail::DroppedStats`) breaks any code reaching into `detail`. → Mitigation: acceptable — `detail` is not public API; the public `nexigen::logging::*` names are unchanged.
