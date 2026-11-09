# Proposal

## Why

`logging::LogLevel` is the only severity vocabulary the program has, but it no longer belongs exclusively to the logging module: the runner suite (`app_suite`, `suite_supervisor`, `suite_manifest`, `runner_logging`), the app command line (`--log-level`), `tools/nexigen-run`, and tests outside logging all qualify it as `nexigen::logging::LogLevel`, and the companion change `add-clock-authority-core` will delete `ClockLogLevel` and bind the chrono worker logger to the shared level instead. Its implementation already delegates everything to core machinery (`core::EnumTraits` + `core::enum_name`/`enum_value`, `core::mandate` JSON pair, the generic `MandateFormatter` in `core/serialization_mandate.hpp`) — the only "logging" thing left about the type is its namespace. Moving it to `nexigen::core` as the program-wide vocabulary lands the cleanup now and unblocks the companion change, whose tasks build on `core::LogLevel`.

## What Changes

- **BREAKING**: `nexigen::logging::LogLevel` moves to `nexigen::core::LogLevel`; no `using` alias is left behind — every consumer in the tree re-qualifies (`core::LogLevel`), matching the project's clean-end-state rename practice.
- New header `include/nexigen/core/log_level.hpp` (file-per-entity): the enum, the `EnumTraits<core::LogLevel>` table, constexpr `to_string`/`from_string` (still delegating to `core::enum_name`/`enum_value`), and ADL `to_json`/`from_json` via `core::mandate`. The old header `include/nexigen/logging/log_level.hpp` is deleted, including its stale trailing doc comment (level rendering is realized by the generic delegate formatter in `core/serialization_mandate.hpp`, not by per-type formatter code).
- Mechanical re-qualification of every in-tree consumer: `include/nexigen/logging/**` + `src/logging/**`, runner headers/sources (`runner_logging`, `suite_manifest`, `start_options`, `app_spec`, `app_suite`, `suite_supervisor`, `runner`), app (`command_line`, `command_line_options`, `infrastructure`), `tools/nexigen-run/main.cpp`, `tests/logging` (15 TUs), `tests/runner`, `tests/app`, `examples/logging` (4 examples).
- Spec deltas: new capability `core/log-level` (severity vocabulary: catalogue, constexpr naming, name parsing, JSON serialization, std::formatter); `logging/log-level` trimmed — the five vocabulary requirements are REMOVED, the capability keeps the logger-threshold behavior with the one requirement text naming the type MODIFIED to `nexigen::core::LogLevel`; `app/command-line` MODIFIED so the `--log-level` requirement names `nexigen::core::LogLevel`.
- No observable behavior change: same `uint8_t` scoped enum, same strictly increasing severity order, same canonical lowercase names, same JSON string encoding, same formatter protocol (`std::format` yields the lowercase name), same relaxed-action runtime threshold mechanics, same ≈1 ns sub-threshold short-circuit.

## Capabilities

### New Capabilities

- `core/log-level`: the program-wide severity vocabulary — `nexigen::core::LogLevel` catalogue with severity ordering, compile-time lowercase naming, name parsing, JSON serialization, and direct `std::format`-ability, usable by every module without reaching into `nexigen::logging`.

### Modified Capabilities

- `logging/log-level`: retains the logger-threshold behavior (runtime level control; the sub-threshold short-circuit requirement text is untouched); the five vocabulary requirements retire to `core/log-level`, and the one requirement that names the level type by namespace now names `nexigen::core::LogLevel`.
- `app/command-line`: the "`--log-level` accepts canonical enumerator names" requirement re-names the accepted spellings' owner type to `nexigen::core::LogLevel`; behavior is unchanged.

## Impact

- **Code**: one new header (`include/nexigen/core/log_level.hpp`), one deleted header (`include/nexigen/logging/log_level.hpp`), and include/qualification swaps across `include/nexigen/{logging,app,runner}/`, `src/{logging,app,runner}/`, `tests/logging` (15 TUs), `tests/runner` (3 TUs), `tests/app` (2 TUs), `tools/nexigen-run/main.cpp`, and `examples/logging` (4 files). No `src/core/` file (the entity stays header-only, as today). Test files stay in place: `log_level_test.cpp` is logger-concept+ordering heavy and the pure vocabulary conversions are already pinned in `enum_vocabulary_test.cpp`, both re-qualified.
- **APIs**: `nexigen::logging::LogLevel` and `<nexigen/logging/log_level.hpp>` disappear (no alias); `nexigen::core::LogLevel` with the same converter set appears. Nothing else in the public surface changes.
- **Performance / latency / allocation impact**: none — this is a namespace-and-include move of an intact definition. The enum keeps its `uint8_t` underlying type (the one-byte `LogRecord` field and the lock-free `std::atomic<LogLevel>` threshold stay byte-identical, so record layout, ring-buffer payloads, and the relaxed threshold load are untouched). `to_string` stays `constexpr` over static-storage literals (no allocation, no exception path); `from_string`/`to_json`/`from_json` keep delegating to the identical `core` helpers; the generic delegate formatter keeps rendering through `to_string`. Hot-path filter comparisons and the ≈1 ns sub-threshold short-circuit are unaffected; the Release latency gates (`NEXIGEN_LOGGING_STRICT_LATENCY`: filtered ≈1 ns, accepted records ≤20 ns) must pass unchanged and serve as the proof of zero hot-path cost. Allocation: none added anywhere (cold JSON conversions keep their existing cold-path arena profile).
