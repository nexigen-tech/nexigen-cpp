# Proposal

## Why

The `nexigen::chrono` headers still violate the project-wide "one file per entity" convention (`openspec/config.yaml` → `Code Organization — File-per-entity layout`). The worst offender is `chrono_provider.hpp`: 289 lines holding seven unrelated top-level entities — `enum ClockLogLevel`, `struct ChronoProviderConfig`, `concept concepts::ClockLogger`, `detail::SinkBinding`, `detail::LogBinding`, and `class ChronoProvider` — so navigating to any one type means opening the whole provider and finding it among neighbours. This also causes include bloat: a consumer that only needs `ClockLogLevel` or `ChronoProviderConfig` today pulls in the full background-worker machinery (`<thread>`, `<mutex>`, `<condition_variable>`, `std::jthread`) transitively.

Two more smelly cases remain from earlier partial extractions:

- `JsonFileChronoSinkConfig` is **declared** in `json_file_chrono_sink_config.hpp` but **completed** in `json_file_chrono_sink.hpp` — its `to_json`/`from_json` and `std::formatter` live in a different header than the type. That split is also why `json_file_chrono_sink.hpp` has a broken preamble (comment → `#include` → `#pragma once`) and a dangling doc comment.
- `stamp_source.hpp` mixes `class StampSource` with the UTC-offset free functions (`sample_utc_offset`, `utc_from_mono_ns`) that serve the consumer side, not the stamp source.

`logging` and `core` were already refactored to this convention (the logging change `2026-11-08-refactor-logging-file-per-entity` is the exemplar); `chrono` is the one module whose per-entity change was only stubbed and never implemented.

## What Changes

- Split `include/nexigen/chrono/*.hpp` so each top-level `enum`/`struct`/`class` gets its own header, named `snake_case` of the type. Highlights:
  - `chrono_provider.hpp` → `clock_log_level.hpp`, `chrono_provider_config.hpp`, `concepts/clock_logger_concept.hpp`, `detail/sink_binding.hpp`, `detail/log_binding.hpp`, `chrono_provider.hpp`.
  - `chrono_snapshot.hpp` → `snapshot.hpp` (type `Snapshot`).
  - `chrono_settings.hpp` → `settings.hpp` (type `Settings`).
  - `time_format.hpp` → `civil_time.hpp` (type `CivilTime` + its civil-time/ISO functions).
  - `stamp_source.hpp` → `stamp_source.hpp` (class only) + `utc_offset.hpp` (`sample_utc_offset`/`utc_from_mono_ns`).
  - `json_file_chrono_sink.hpp` → move `JsonFileChronoSinkConfig`'s `to_json`/`from_json`/`std::formatter` back into `json_file_chrono_sink_config.hpp`, leaving the sink header to the sink alone.
- Split `src/chrono/*.cpp` into per-entity translation units:
  - `chrono_provider.cpp` → `chrono_provider.cpp` + `chrono_provider_config.cpp`.
  - `chrono_settings.cpp` → `settings.cpp`; `chrono_snapshot.cpp` → `snapshot.cpp`; `time_format.cpp` → `civil_time.cpp`.
  - `clock_calibration.cpp` → `stamp_source.cpp` (calibration) + `utc_offset.cpp` (offset snapshot).
- Update every `#include <nexigen/chrono/...>` site across `include/`, `src/`, `tests/`, and `examples/` to the new per-entity headers.
- Drop the two dangling doc comments (`clock_id.hpp`, `json_file_chrono_sink_config.hpp`) and trim the excess includes in `json_file_chrono_sink_config.hpp`.

No behavior changes: every type keeps its public name, serialization mandates (`to_string`, `std::formatter`, ADL `to_json`/`from_json`), `alignas(64)` layouts, and `std::memory_order` choices.

## Capabilities

Pure refactor — no spec-level behavior changes. `skip_specs: true` is already set in `.openspec.yaml`.

### New Capabilities

None.

### Modified Capabilities

None.

## Impact

- **Code**: ~14 chrono headers split into ~20; `src/chrono/*.cpp` split from 5 into 7 translation units; `#include` updates in `include/nexigen/{app,metrics,tracing,diagnostics,logging,runner}/*.hpp`, `src/{chrono,metrics,tracing,logging,runner,hub}/*.cpp`, `tests/{chrono,app,logging,diagnostics}/*.cpp`, and `examples/{chrono,metrics,diagnostics,app,suite,core}/*.cpp`. `CMakeLists.txt` needs no change: `src/chrono/*.cpp` is already globbed with `CONFIGURE_DEPENDS` (line 157) and headers are not listed.
- **Dependencies**: include-graph only. Consumers that need a single type (`ClockLogLevel`, `ChronoProviderConfig`, `CivilTime`, `Snapshot`) no longer pull in the provider/worker machinery transitively. No new external dependencies; `nexigen_chrono` still links `nexigen_core` + `Threads::Threads`.
- **Performance & memory**: zero hot-path impact. This is a pure header/translation-unit reorganization — no statement on any sampling or worker path changes. The `alignas(64)` `StampSource`/`ChronoProvider` layouts, the relaxed/acquire/release `std::memory_order` choices, and the `static_assert(sizeof(Timestamp) == 8)`, `static_assert(sizeof(Snapshot) == 16)`, `static_assert(sizeof(StampSource) <= 64)` contracts are preserved verbatim. No heap allocation is added or removed; the allocation-free snapshot/test-arena gates must keep passing unchanged.
- **Compatibility**: non-breaking for the public API — `nexigen::chrono::*` names are unchanged. Source-breaking only for code reaching into `nexigen::chrono::detail::` for `SinkBinding`/`LogBinding` (not public API; they were already module-internal).
