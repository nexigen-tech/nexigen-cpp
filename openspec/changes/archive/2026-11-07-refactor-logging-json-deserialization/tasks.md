# Tasks

## 1. Baseline and scaffolding

- [x] 1.1 Baseline recorded: `sanitize_document` 47, `apply_environment` 22, `LoggerOptions::from_json` 32, `FileLoggerConfig::from_json` 27, `ConsoleLoggerConfig::from_json` 10.
- [x] 1.2 Debug build green before refactor.

## 2. Decompose `sanitize_document`

- [x] 2.1–2.10 Per-field helpers extracted in `src/logging/logging_settings.cpp`.
- [x] 2.11 `sanitize_document` reduced to an orchestrator (CCN 2); Debug logging tests pass.

## 3. Decompose `apply_environment`

- [x] 3.1–3.6 Per-variable helpers extracted.
- [x] 3.7 `apply_environment` reduced to an orchestrator (CCN 1); Debug logging tests pass.

## 4. Decompose `logger_options_format.cpp` `from_json` overloads

- [x] 4.1–4.8 Reusable best-effort field readers extracted (`read_*`).
- [x] 4.9 `LoggerOptions::from_json` reduced to an orchestrator (CCN 4).
- [x] 4.10 `FileLoggerConfig::from_json` reduced to an orchestrator (CCN 12) with strict `try_read_*` helpers for throttle/rotation.
- [x] 4.11 `ConsoleLoggerConfig::from_json` reduced to an orchestrator (CCN 6).
- [x] 4.12 Debug logging tests pass.

## 5. Extend unit tests

- [x] 5.1 Rejection-path coverage for `sanitize_document` fields exercised through existing `test_logging_settings_loader` / `test_logging_settings_full_coverage` plus the refactor-specific suite.
- [x] 5.2 New `test_logging_json_rejections` suite added for invalid `LoggerOptions`, `FileLoggerConfig`, `ConsoleLoggerConfig`, and `SpamProtectionConfig` JSON shapes.
- [x] 5.3 Debug build passes all new checks.

## 6. Verification and quality gates

- [x] 6.1 Release build compiles; all logging suites pass except the pre-existing `NEXIGEN_LOGGING_STRICT_LATENCY` ≤20 ns gate, which reports ~130 ns in this sandbox and is unrelated to the cold-path JSON refactor.
- [x] 6.2 TSan build passes all logging tests.
- [x] 6.3 ASan build passes all logging tests.
- [x] 6.4 clang-format check passes (`clang-format --dry-run --Werror` on changed files).
- [x] 6.5 lizard confirms all refactored functions/helpers below CCN 15:
  - `sanitize_document` 2
  - `apply_environment` 1
  - `LoggerOptions::from_json` 4
  - `FileLoggerConfig::from_json` 12
  - `ConsoleLoggerConfig::from_json` 6
  - all helpers ≤ 5.

## Workflow follow-up

- [ ] Archive the change after the project's review requirements are satisfied.
