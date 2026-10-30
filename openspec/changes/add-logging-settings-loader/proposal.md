# Proposal

## Why

`LoggerOptions` exists but nothing consumes it: every application still hand-builds `FileLogger_config` / `ConsoleLogger::Config`, and settings such as `max_file_size_bytes` and `flush_cpu_affinity` are never read from a deployment source. HIL operators need one predictable way to set logging policy globally for a machine and override it per application, without recompiling or wiring fields by hand.

## What Changes

- Add `nexigen::logging` settings loader: `load_logging_settings(const Settings_paths&, Logging_settings&) noexcept` reads an explicitly supplied **global** JSON file and **local** JSON file (no implicit default paths), merges them field-by-field, and applies environment-variable overrides.
- Precedence: built-in defaults < global file < local file < environment. A field absent from every source keeps the `LoggerOptions` default; invalid values are reported as diagnostics and leave the last valid value in place (best-effort, never blocking startup).
- `app_name` is a **local-only** field: it is read from the local file (and `NEXIGEN_LOG_APP_NAME`) and ignored in the global file.
- Add `Logging_settings` carrying the merged `LoggerOptions`, per-source load flags, and a diagnostics list (file kind, field, error) with the full serialization contract (member + free `to_string`, `to_json`, `std::formatter`).
- Add apply helpers that turn merged options into logger construction configs: `apply_to(const Logging_settings&, FileLogger_config&)` and the `ConsoleLogger<...>::Config` equivalent, plus a template capacity check that rejects/does not silently accept settings exceeding the logger's compile-time shard capacity or producer-slot count.
- Add `FileLogger_config::max_file_size_bytes` (default `0` = no limit, preserving existing behaviour) and implement size-based rotation in `FileLogger`: when the active file reaches the limit, the writer closes it and continues in a numbered segment (`app.1.jsonl`, `app.2.jsonl`, …) in the same period directory, hitch-free for producers.
- `LoggerOptions::flush_cpu_affinity` (a list of core ids) maps to `FileLogger_config::core_mask`; ids above 63 are a diagnostic.
- Add a runnable `ex_logging_settings` example: writes a temporary global/local pair, loads and merges it, initializes a file logger from the result, logs, flushes, and exits 0.

**BREAKING**: none. All new members default to the previous behaviour and existing `initialize(...)` overloads keep their signatures.

## Capabilities

### New Capabilities

- `logging/logging-settings`: explicit global/local JSON sources, merge precedence, environment overrides, local-only `app_name`, best-effort diagnostics, and application of merged options to logger construction configs with compile-time capacity checks.

### Modified Capabilities

- `logging/file-logger`: adds optional size-based rotation (`max_file_size_bytes`) to the existing scheduled directory rotation, with numbered segment naming and no producer-visible hitch.

## Impact

- **New code**: `include/nexigen/logging/logging_settings.hpp`, `src/logging/logging_settings.cpp`, `examples/logging/ex_logging_settings.cpp`.
- **Modified code**: `src/logging/logger_options_format.cpp` (diagnostics serialization), `include/nexigen/logging/file_logger.hpp`, `include/nexigen/logging/detail/rotation.hpp`, `src/logging/rotation.cpp`, `tests/logging/logging_test.cpp`, `examples/CMakeLists.txt`, `README.md`, `docs/architecture.md`.
- **Dependencies**: uses existing `nlohmann::json` (cold path, `JSON_NOEXCEPTION=1`) and `nexigen::core` error model; no new third-party dependency.
- **Performance**: hot-path impact is **zero**. The loader runs once before real-time execution and may allocate (`std::string`, `nlohmann::json`, diagnostics vector). The producer path and its ns-class latency gates are untouched. Size-based rotation executes only on the background writer thread at an already cold switch point; producers still observe only ring push/pop. Memory: fixed `FileLogger_config` surface gains one `std::uint64_t`; `File_sink::Settings` gains one `std::uint64_t` and one segment counter — no dynamic allocation on the writer path.
