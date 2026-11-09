# Design

## Context

The entity is a 94-line header (`include/nexigen/logging/log_level.hpp`) that is already fully delegated to core machinery: enum declared in `nexigen::logging`, the `EnumTraits` vocabulary table in `nexigen::core`, `to_string`/`from_string` delegating to `core::enum_name`/`enum_value` (machinery from the archived `refactor-enum-traits-helper`), and the ADL JSON pair delegating to `core::mandate::enum_to_json`/`enum_from_json`. There is no per-type formatter code: rendering is realized by the generic delegate formatter (`MandateFormatter`) in `core/serialization_mandate.hpp`; the trailing doc comment at lines 92–94 of the old header is a stale pointer to that mechanism and goes away with the file. No `src/core/` file is needed — the entity stays header-only.

The consumer sweep is wide but mechanical: 43 files qualify `logging::LogLevel` (logging headers/sources, runner headers/sources and its 3 test TUs, app command-line and `infrastructure.hpp`, `tools/nexigen-run`, `tests/app`, `examples/logging`), plus unqualified uses in the 15 `tests/logging` TUs. Two live spec texts name the type: the `logging/log-level` capability itself and the app/command-line requirement on `--log-level`. The companion change `add-clock-authority-core` is the only in-flight change referencing `LogLevel` (its tasks build on `core::LogLevel`); the other in-flight changes (`extract-child-process-class`, `refactor-hub-model-config-headers`, `refactor-settings-table-parsing`) touch nothing here.

## Goals / Non-Goals

**Goals:**
- Clean end-state: `nexigen::core::LogLevel` everywhere, no alias, no duplicate vocabulary names.
- Deltas that mirror exactly the textual movement: the vocabulary requirements re-home to `core/log-level`; logger behavior stays in `logging/log-level`; the CLI requirement tracks the type.
- Gate-provable zero hot-path cost: same byte-wide enum, same constexpr naming, same ≈1 ns filtered-call budget under the Release latency gates.

**Non-Goals:**
- No type reshaping: enumerators, names, underlying type, converter signatures, and JSON encoding keep their exact shapes.
- No test relocation: `tests/logging/log_level_test.cpp` stays (it is logger-concept-and-ordering heavy, i.e. logging behavior); the pure vocabulary protocol remains pinned in `tests/logging/enum_vocabulary_test.cpp`. Neither moves to `tests/core/`.
- No `ClockLogLevel` work: that vocabulary (and its header) is deleted by `add-clock-authority-core`, which lands after this change.
- No serializer/README/docs rework beyond code and comment references needed to compile and pass the grep gate.

## Decisions

**D1 — 1:1 header move.** Create `include/nexigen/core/log_level.hpp` with the same internal structure re-qualified: enum in `namespace nexigen::core`; `EnumTraits<nexigen::core::LogLevel>` (same seven-entry table, lowercase names, `Unknown` rendering for garbage casts via `core::enum_name`); constexpr `to_string`/`from_string` delegating to `core::enum_name`/`enum_value`; ADL `to_json`/`from_json` delegating to `core::mandate`. Delete `include/nexigen/logging/log_level.hpp` in the same commit. Alternative considered: keeping the header in logging and aliasing — rejected (see D2).

**D2 — No alias.** `nexigen::logging::LogLevel` is not preserved as `using LogLevel = core::LogLevel;`: all consumers are in-tree and the change updates them in one pass, the compiler plus the deleted header enforce completeness, and a lingering alias would leave the vocabulary effectively owned by two namespaces — contradicting the single-authority cleanup this change exists for, and contradicting the project's rename precedent (`ChronoProvider` → `Provider` moved alias-free).

**D3 — `logging/log-level` is trimmed, not retired.** The capability keeps the logger-threshold behavior: *Runtime level control* is MODIFIED (its signature text now names `nexigen::core::LogLevel`), and *Sub-threshold calls short-circuit* stays untouched (its text is type-free — verified) — both thresholds semantics belong to logging and never moved. The five vocabulary requirements (catalogue, naming, parsing, JSON, formatter) REMOVED here are ADDED in `core/log-level` with only the qualification re-worded and the parser's existing `constexpr` recorded.

**D4 — app/command-line reworded, behavior untouched.** The `--log-level` requirement and its "matching value" scenario name `nexigen::core::LogLevel`; acceptance of the same seven lowercase spellings and the `InvalidArgument` rejection path are carried verbatim.

**D5 — Tests stay in place, re-qualified.** The core-side test harness is a single-TU per module convention (`tests/core/core_test.cpp` has its own embedded harness/main), so relocating test files would churn CMake and suite wiring for zero coverage delta. Existing constexpr/static_assert pins (`to_string(LogLevel::Warning) == "warning"`, `std::formattable`, round-trips) re-run unchanged in the `nexigen_logging_tests` target after qualification.

**D6 — Landing order.** This change lands first; `add-clock-authority-core` lands after and consumes `core::LogLevel` (worker logger binding), per its tasks. Until the companion lands, chrono keeps `ClockLogLevel` and is untouched by this change.

## Risks / Trade-offs

- [A 60-file mechanical sweep can miss a TU] → the deleted header + no-alias switch makes any miss a compile error, and the task list carries an explicit grep gate (zero hits for `logging::LogLevel` / `nexigen/logging/log_level.hpp` under `include/`, `src/`, `tests/`, `examples/`, `tools/`).
- [Spec texts drift beyond the move] → the delta budget is enforced by construction: only requirement texts that literally name the qualified type are MODIFIED/REMOVED; `Sub-threshold calls short-circuit` was text-checked type-free and stays out of the delta.
- [CLI parsing behavior regression] → `from_string` delegation chain is identical; `tests/app/command_line_test.cpp` (29 re-qualified match sites) re-validates the seven spellings and the rejection diagnostics.
- [Stale doc comments referencing `logging::LogLevel` in unrelated headers] → the grep gate targets code paths and include directives; pure comment mentions inside chrono headers belong to the companion change's scope and are left for it.

## Migration Plan

1. Land this change as one compiling unit: new core header → consumer sweep → old header deletion → CMake include-graph check → full gates.
2. The companion change lands next and deletes `ClockLogLevel` onto `core::LogLevel`.
3. Rollback = revert this change alone; there are no wire-format, data-format, or settings-format consequences, and no external API consumers outside the tree.

## Open Questions

None.
