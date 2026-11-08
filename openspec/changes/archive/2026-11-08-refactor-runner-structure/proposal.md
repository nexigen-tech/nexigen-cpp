# Proposal

## Why

`nexigen::runner` never completed the project-wide "one file per entity" refactor: the archived
`2026-11-08-refactor-runner-file-per-entity` change only split the four enums, leaving 7 structs in
`app_suite.hpp`, 2 in `instance_lifecycle.hpp`, and `StartOptions` beside `Runner` in `runner.hpp`.
The same files also carry duplicated cold-path helpers (`child_exit`, `redirect_to_null`,
`read_start_report`, env/path/CPU validation, JSON-read boilerplate) copied across
`runner.cpp` / `app_suite.cpp` / `instance_lifecycle.cpp` / `runner_logging.cpp`. This makes a type
hard to locate, pulls `nlohmann/json.hpp` into every `Runner` consumer, and lets the copies drift.

## What Changes

This is stage 1 of the runner structural refactor: **header split + helper deduplication**. It is a
pure refactor — no behavior change, no spec-level requirement change (`skip_specs: true`).

- Split the suite vocabulary into per-entity headers:
  `app_spec.hpp`, `suite_manifest.hpp`, `suite_app_metadata.hpp`, `suite_metadata.hpp`,
  `suite_exit.hpp`, `suite_app_status.hpp`, `suite_status.hpp`, `suite_paths.hpp`,
  `suite_control.hpp`.
- Split the instance vocabulary into `resource_metadata.hpp`, `exit_metadata.hpp`,
  `instance_file_io.hpp`, `instance_layout.hpp`, `instance_settings.hpp`.
- Split `StartOptions` out of `runner.hpp` into `start_options.hpp`; `runner.hpp` keeps only
  `class Runner`.
- Keep `app_suite.hpp` and `instance_lifecycle.hpp` as thin umbrella headers that only include the
  new per-entity headers, so existing consumers keep working without a source break.
- Move `StartReport` into `detail/start_report.hpp`; add `detail/cpu_validation.hpp` and
  `detail/path_utils.hpp` for the helpers that were duplicated across runner translation units.
- Deduplicate exact helpers: one `child_exit`, one `redirect_to_null`, one
  `read/write_start_report`, one env validator, one path-limit constant, one `absolute_path` /
  `safe_id` / `join_path` / instance-dir builder, one online-CPU validator, one
  `read_json_into<T>` helper.
- Move the four enum JSON definitions (`AppState`, `SuiteState`,
  `ImpactOnAppSuiteOperability`, `TerminatedBy`) beside their own headers, completing the previous
  partial split.

No translation-unit split of `app_suite.cpp` (`suite_supervisor.cpp` / `suite_control.cpp`) and no
settings-writer unification in this stage; those stay in the follow-up stage tracked by
`design.md` / `tasks.md`.

## Capabilities

Pure refactor — no spec-level behavior changes. `skip_specs: true` is set in `.openspec.yaml`.

### New Capabilities

None.

### Modified Capabilities

None.

## Impact

- **Code**: 3 "kitchen-sink" headers become 17 focused headers; `runner.hpp` no longer pulls the
  suite JSON machinery into every `Runner` consumer. Four new tiny enum-serialization TUs and three
  new detail headers. `src/runner/*.cpp` names and behaviour are otherwise unchanged; existing
  `app_suite.cpp` / `instance_lifecycle.cpp` remain the implementation TUs for this stage.
- **Dependencies**: include-graph only. `CMakeLists.txt` needs no change — `src/runner/*.cpp` is
  already globbed with `CONFIGURE_DEPENDS`.
- **Performance & memory**: zero hot-path impact. Runner is cold-path control-plane code; no
  function body, allocation, syscall, process handshake, `std::memory_order`, `alignas`, or
  serialization outcome changes. The only code movement is verbatim body relocation plus one
  `StartReport` write path that now consistently uses the existing `write_all_no_sigpipe` helper.
- **Compatibility**: non-breaking for public names. `#include <nexigen/runner/app_suite.hpp>` and
  `#include <nexigen/runner/instance_lifecycle.hpp>` still compile through the umbrella headers;
  `nexigen::runner::StartOptions` stays in `runner.hpp` via its new include. Detail helpers move
  only inside `nexigen::runner::detail`.
