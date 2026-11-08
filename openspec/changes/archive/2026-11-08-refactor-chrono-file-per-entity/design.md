# Design

## Context

See `proposal.md` for motivation. This design covers only `nexigen::chrono`; it reuses the convention the `refactor-logging-file-per-entity` change already encoded in `openspec/config.yaml` (Section "Code Organization — File-per-entity layout").

Current state: `include/nexigen/chrono/` has 14 headers; `chrono_provider.hpp` is a "kitchen sink" of 7 top-level entities, and `stamp_source.hpp`/`time_format.hpp` co-locate a class/struct with free functions owned by a different concern. One cross-header "completed type" exists today — `JsonFileChronoSinkConfig` is declared in `json_file_chrono_sink_config.hpp` but its `to_json`/`from_json` and `std::formatter` live in `json_file_chrono_sink.hpp`. `src/chrono/clock_calibration.cpp` implements `stamp_source.hpp` (`StampSource::calibrate` + `sample_utc_offset`), a file↔header name mismatch.

Constraints that shape every decision: the serialization mandates 9–11 (every public type needs `to_string`; every enum needs `from_string` + ADL `to_json`/`from_json` + a `std::formatter` specialization), the hot-path mandates (no heap allocation, no locking, `alignas(64)`, explicit `std::memory_order`), and the namespace map (`nexigen::chrono` → `include/nexigen/chrono/`, `src/chrono/`, `tests/chrono/`).

## Goals / Non-Goals

**Goals:**
- One top-level `enum`/`struct`/`class` per header, so a type's file is findable and a consumer includes only what it uses.
- Remove the cross-header "completed type" (`JsonFileChronoSinkConfig`) and the file↔header name mismatch (`clock_calibration.cpp`).
- Keep the file-per-entity rule satisfied for chrono the same way logging already satisfies it.

**Non-Goals:**
- No behavior change, no API redesign, no new/removed types, no spec-level requirement changes (`skip_specs: true`).
- Other modules (`metrics`, `tracing`, `diagnostics`, `runner`, `hub`, `app`) are out of scope — their per-entity changes are separate.

## Decisions

### D1 — Granularity rule (unchanged from the logging exemplar)

| Kind | Home |
|---|---|
| Top-level `enum` / `struct` / `class` | its own header, named `snake_case` of the type |
| Public `concept` | `concepts/<snake_case>_concept.hpp` |
| `detail`-only `concept` | stays in `detail/` beside its owner |
| Private nested type (`SinkBinding`, `LogBinding` as detail) | `detail/` beside its owner |
| `using` alias | no own header; lives with the type it aliases or the header that needs it |
| Free function(s) | in the header of the entity they serve; a cohesive group owned by no single entity keeps one utility header |

### D2 — Target file tree (~20 headers)

```
include/nexigen/chrono/
├── clock_id.hpp                     enum ClockId (+ clock_domain<T> variable template)
├── clock_log_level.hpp              enum ClockLogLevel            (from chrono_provider)
├── timestamp.hpp                    class Timestamp
├── snapshot.hpp                     struct Snapshot               (was chrono_snapshot.hpp)
├── civil_time.hpp                   struct CivilTime + civil_from_utc_ns /
│                                    days_from_utc_ns / format_utc_iso8601_ns
│                                    (was time_format.hpp)
├── stamp_source.hpp                 class StampSource             (class only)
├── utc_offset.hpp                   sample_utc_offset / utc_from_mono_ns (utility)
├── chrono_provider_config.hpp       struct ChronoProviderConfig   (from chrono_provider)
├── chrono_provider.hpp              class ChronoProvider          (class only)
├── settings.hpp                     struct Settings + load_chrono_settings + apply_to
│                                    (was chrono_settings.hpp)
├── composite_chrono_sink.hpp        class CompositeChronoSink
├── console_chrono_sink.hpp          class ConsoleChronoSink
├── null_chrono_sink.hpp             class NullChronoSink
├── json_file_chrono_sink_config.hpp struct JsonFileChronoSinkConfig
│                                    (+ to_json/from_json/formatter moved back in)
├── json_file_chrono_sink.hpp        class JsonFileChronoSink      (sink only)
├── concepts/
│   ├── chrono_sink_concept.hpp      concept ChronoSink
│   └── clock_logger_concept.hpp     concept ClockLogger           (from chrono_provider)
└── detail/
    ├── affinity.hpp                 using pin_self/self_affinity  (forward shim)
    ├── sink_binding.hpp             struct SinkBinding            (from chrono_provider)
    └── log_binding.hpp              struct LogBinding             (from chrono_provider)
```

### D3 — Complete the half-extracted config type

`JsonFileChronoSinkConfig` keeps its home (`json_file_chrono_sink_config.hpp`); its `to_json`/`from_json` and `std::formatter` move back into that header, deleting them from `json_file_chrono_sink.hpp`. The sink header then contains only `class JsonFileChronoSink` (+ its own `to_string`/`to_json`/formatter), and includes `json_file_chrono_sink_config.hpp` for the constructor's config parameter. This also fixes the broken preamble (`#pragma once` currently sits after the `#include`) and removes the dangling `/// A sink that appends…` comment and the ~10 excess includes (`charconv`, `cstdint`, `unistd`, `cerrno`, `fcntl`, `chrono_snapshot`, `concepts`, `result`, `sink_io`) that the config struct does not use.

*Rationale:* a consumer needing `JsonFileChronoSinkConfig` includes the config header alone instead of the sink header (which drags in the write machinery). *Alternative considered:* keep the serialization in the sink header and delete the config header — rejected, the config is already a public vocabulary type consumed by `Settings`.

### D4 — Rename files to match their entity

- `chrono_snapshot.hpp` → `snapshot.hpp` (the type is `Snapshot`).
- `chrono_settings.hpp` → `settings.hpp` (the type is `Settings`, mirroring logging's `logging_settings.hpp` → `settings.hpp`).
- `time_format.hpp` → `civil_time.hpp` (the type is `CivilTime`; its three functions `days_from_utc_ns`/`civil_from_utc_ns`/`format_utc_iso8601_ns` are a cohesive civil-time/ISO group that serves it — same precedent as `log_record.hpp` carrying `record_render_fn`).
- `clock_id.hpp` keeps its name (`ClockId` + the `clock_domain<T>` variable template that reports the domain; a variable template is not a top-level type, so it stays beside the enum it annotates).

### D5 — Free-function placement

- `sample_utc_offset` + `utc_from_mono_ns` move out of `stamp_source.hpp` into `utc_offset.hpp`. They serve the consumer-side UTC-offset feature (metrics/tracing sink conversion), not the producer-owned `StampSource` — a cohesive group owned by no single entity. `sample_utc_offset` stays non-inline (`utc_offset.cpp`); `utc_from_mono_ns` stays inline (one-line pure arithmetic).
- `load_chrono_settings` + `apply_to` stay with `Settings` in `settings.hpp` (mirrors `load_logging_settings`/`check_capacity` staying with `Settings`).
- The enum serialization trios (`to_string`/`from_string`/`to_json`/`from_json`) stay with their enum (`clock_id.hpp`, `clock_log_level.hpp`) — the mandate requires per-enum ADL definitions.

### D6 — `src/chrono` translation-unit split

```
src/chrono/
├── chrono_provider.cpp          ChronoProvider implementation (no config serialization)
├── chrono_provider_config.cpp   ChronoProviderConfig::to_string + to_json/from_json (+ free to_string)
├── settings.cpp                 Settings::to_string + to_json + load_chrono_settings + apply_to
├── snapshot.cpp                 Snapshot::to_string + to_json/from_json
├── civil_time.cpp               days_from_utc_ns / civil_from_utc_ns / format_utc_iso8601_ns
├── stamp_source.cpp             StampSource::calibrate (sleeps ~2 ms — compiled, not inline)
└── utc_offset.cpp               sample_utc_offset
```

`timestamp.hpp`, `clock_id.hpp`, `clock_log_level.hpp`, `composite_chrono_sink.hpp`, `console_chrono_sink.hpp`, `null_chrono_sink.hpp`, the concepts, and `detail/` stay header-only. `src/chrono/*.cpp` is already globbed with `CONFIGURE_DEPENDS` (CMakeLists.txt:157), so no `CMakeLists.txt` edit is required.

## Risks / Trade-offs

- **Header count grows ~14 → ~20** (and `src/chrono` 5 → 7 TUs). More files to manage, but each is small and self-describing. → Mitigation: the convention makes the mapping mechanical; no file exceeds one type + its serialization helpers.
- **`#include` churn across ~40 sites** (`include/nexigen/{app,metrics,tracing,diagnostics,logging,runner}`, `src/{chrono,metrics,tracing,logging,runner,hub}`, `tests/{chrono,app,logging,diagnostics}`, and `examples/`) risks a transient broken build if a site is missed. → Mitigation: `tasks.md` tracks include-site updates per split; the full build matrix (Debug / Release `-O3 -flto` / TSAN / ASAN) catches every missed include at compile time.
- **Accidental behavior drift** if a split reorders or re-templates a definition. → Mitigation: moves are copy-preserving (git mv / verbatim body relocation); the `alignas(64)` layouts, `std::memory_order` choices, and the `static_assert(sizeof(Timestamp) == 8)` / `sizeof(Snapshot) == 16` / `sizeof(StampSource) <= 64` contracts are preserved exactly; the allocation-free test-arena gates and 100% unit tests must keep passing.
- **`snapshot.hpp`/`settings.hpp` are generic names** (unprefixed types). → Mitigation: fully-qualified includes (`<nexigen/chrono/snapshot.hpp>`) are unambiguous; this matches logging's `settings.hpp` precedent.

## Workflow follow-up

- After implementation passes review, archive the change.
- The same pattern is already applied (or stubbed) for the sibling modules; nothing further to propagate here.
