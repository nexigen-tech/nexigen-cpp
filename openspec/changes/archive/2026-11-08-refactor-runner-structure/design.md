# Design

## Context

See `proposal.md` for motivation. This design covers stage 1 only: header-per-entity plus
deduplication of the helpers that are currently copied across `src/runner/*.cpp`. It reuses the
file-per-entity convention in `openspec/config.yaml` (section "Code Organization — File-per-entity
layout") and the umbrella-header migration strategy already used by the chrono/logging refactors.

Current state:

- `include/nexigen/runner/app_suite.hpp` (214 lines) holds `AppSpec`, `SuiteManifest`,
  `SuiteAppMetadata`, `SuiteMetadata`, `SuiteExit`, `SuiteAppStatus`, `SuiteStatus`, their
  serialization declarations, path helpers, and the free suite control API.
- `include/nexigen/runner/instance_lifecycle.hpp` (129 lines) holds `ResourceMetadata`,
  `ExitMetadata`, JSON file I/O, instance directory helpers, and settings generation.
- `include/nexigen/runner/runner.hpp` holds `StartOptions` and `Runner`.
- `include/nexigen/runner/detail/sys_helpers.hpp` holds `StartReport` plus five unrelated helper
  groups (time, fd I/O, process identity, JSON readers, enum JSON).
- `src/runner/app_suite.cpp` / `runner.cpp` / `instance_lifecycle.cpp` / `runner_logging.cpp`
  contain byte-identical copies of several helpers.

Constraints: cold path only, `-fno-exceptions`, `Result`/`Status`, no behavior change, tests must
pass on Debug/Release/ASan/TSan, and the include graph must not create cycles.

## Goals / Non-Goals

**Goals:**
- Every top-level `enum`/`struct`/`class` in `nexigen::runner` has its own header, named
  `snake_case` of the type.
- `runner.hpp` contains only `class Runner`; `StartOptions` has `start_options.hpp`.
- `app_suite.hpp` and `instance_lifecycle.hpp` survive only as thin umbrella headers so external
  consumers do not break.
- One implementation of each duplicated helper, in `detail/`, with no behavior change.
- The four enum JSON definitions live beside their enums, completing the previous partial split.

**Non-Goals:**
- No split of `app_suite.cpp` into validation/supervisor/control TUs (stage 2).
- No unification of `generate_settings` and `write_suite_settings` (stage 2).
- No validation-pipeline redesign, no `Runner::status` API change, no liveness/identity behavior
  changes, no test-harness refactor.
- No new spec-level behavior; `skip_specs: true`.

## Decisions

### D1 — Target header tree

```
include/nexigen/runner/
├── app_state.hpp                         enum AppState                    (unchanged)
├── suite_state.hpp                       enum SuiteState                  (unchanged)
├── impact_on_app_suite_operability.hpp   enum ImpactOnAppSuiteOperability (unchanged)
├── terminated_by.hpp                     enum TerminatedBy                (unchanged)
├── start_options.hpp                     struct StartOptions              (from runner.hpp)
├── runner.hpp                            class Runner only
├── app_spec.hpp                          struct AppSpec                   (from app_suite.hpp)
├── suite_manifest.hpp                    struct SuiteManifest + load/validate
├── suite_app_metadata.hpp                struct SuiteAppMetadata
├── suite_metadata.hpp                    struct SuiteMetadata
├── suite_exit.hpp                        struct SuiteExit
├── suite_app_status.hpp                  struct SuiteAppStatus
├── suite_status.hpp                      struct SuiteStatus + suite_status()
├── suite_paths.hpp                       suite_dir_path / attempt_dir_path
├── suite_control.hpp                     start_suite / stop_suite
├── app_suite.hpp                         umbrella only
├── resource_metadata.hpp                 struct ResourceMetadata           (from instance_lifecycle.hpp)
├── exit_metadata.hpp                     struct ExitMetadata
├── instance_file_io.hpp                  read/write_json_file + read_json_into<T>
├── instance_layout.hpp                   instance_file_path / create_instance_dir / create_artifact_dir
├── instance_settings.hpp                 generate_settings
├── instance_lifecycle.hpp                umbrella only
├── runner_logging.hpp                    logging facade + affinity          (unchanged for stage 1)
└── detail/
    ├── start_report.hpp                  struct StartReport
    ├── cpu_validation.hpp                online_cpu_count / cpus_within_online
    ├── path_utils.hpp                    path/id/join helpers + max_path_bytes
    └── sys_helpers.hpp                   time/fd/process/json helpers (StartReport moved out)
```

`app_suite.hpp` and `instance_lifecycle.hpp` are compatibility umbrellas: they contain only
`#include` directives plus their existing module-level doc comment. New code inside the module
includes the narrow header it uses.

### D2 — Entity headers keep the serialization mandate

Each moved struct header carries its own `to_string()` member, free `to_string`, ADL
`to_json`/`from_json` declarations, and the include of `<nlohmann/json.hpp>` it needs. Definitions
stay in the existing implementation TUs for stage 1; only the four enum definitions move to new
per-enum TUs because that is the residual half-finished split from the previous change. The generic
`std::formatter` from `core/serialization_mandate.hpp` continues to cover every runner type —
no per-type formatter is added or removed.

### D3 — Helper ownership

| Helper | New home | Rationale |
|---|---|---|
| `child_exit` | `src/runner/coverage_flush.hpp` (`detail::child_exit`) | already owns coverage flushing before `_exit`; both fork paths need it |
| `redirect_to_null` | `detail/sys_helpers.hpp` | small fd helper beside the other fd I/O |
| `StartReport` | `detail/start_report.hpp` | top-level detail struct gets its own header |
| `read_start_report` / `write_start_report` | `detail/sys_helpers.hpp` | uses `read_all` / `write_all_no_sigpipe` / `reap_failed_supervisor` already there |
| `validate_env_entries` | `detail/sys_helpers.hpp` | one `Status`-returning env contract |
| `absolute_path`, `safe_id`, `join_path`, `instance_dir_path`, `max_path_bytes` | `detail/path_utils.hpp` | cohesive path/id group used by suite and instance TUs |
| `online_cpu_count`, `cpus_within_online` | `detail/cpu_validation.hpp` | one online-CPU probe and one membership check |
| `read_json_into<T>` | `instance_file_io.hpp` | generic public-I/O helper; keeps error behavior (`false` on missing/invalid) |
| enum JSON (`AppState`, `SuiteState`, `Impact`, `TerminatedBy`) | `app_state.cpp`, `suite_state.cpp`, `impact_on_app_suite_operability.cpp`, `terminated_by.cpp` | definitions beside their headers |

`path_utils.hpp` intentionally keeps the runner-local `safe_id` even though
`src/app/telemetry_layout.cpp` has a private copy: cross-module promotion is a separate app/runner
change and would expand this stage's blast radius.

### D4 — Umbrella headers, not consumer churn

`app_suite.hpp` becomes:

```cpp
#pragma once
#include <nexigen/runner/app_spec.hpp>
#include <nexigen/runner/suite_manifest.hpp>
#include <nexigen/runner/suite_app_metadata.hpp>
#include <nexigen/runner/suite_metadata.hpp>
#include <nexigen/runner/suite_exit.hpp>
#include <nexigen/runner/suite_app_status.hpp>
#include <nexigen/runner/suite_status.hpp>
#include <nexigen/runner/suite_paths.hpp>
#include <nexigen/runner/suite_control.hpp>
```

`instance_lifecycle.hpp` similarly includes the five instance headers. This keeps
`tools/`, `tests/hub`, and `src/hub` compiling while the module moves to narrow includes.
`runner.hpp` drops `app_suite.hpp` / `instance_lifecycle.hpp` and includes `start_options.hpp` +
`suite_manifest.hpp`, so a `Runner` consumer no longer pulls the full suite serialization surface.

### D5 — No behavior change

All moves are verbatim body relocations. The only intentional semantic adjustments are:

1. `StartReport` writes in `runner.cpp` switch from `write_all` to the existing
   `write_all_no_sigpipe` contract already used by `app_suite.cpp`. This is a robustness fix to the
   already-documented handshake invariant, not an API change.
2. `read_json_into<T>` reproduces the existing `read_json_file` + `from_json` `false`/untouched
   behavior of the call sites it replaces.
3. `detail::instance_dir_path` reproduces the existing
   `run_dir + "/" + app + "/" + instance` concatenation in `app_suite.cpp` exactly (no trailing-slash
   normalization), while `runner.cpp` keeps its existing trailing-slash trim.

## Risks / Trade-offs

- **Include churn / missed include** → umbrella headers preserve all existing include sites; only
  the runner implementation TUs switch to narrow includes. Every target is rebuilt in the matrix.
- **Copy-paste drift during header split** → each struct declaration is moved verbatim (same field
  order, same defaults, same doc comments). `sizeof`/layout contracts are unchanged because no field
  changes. Tests already assert JSON round-trips for every DTO.
- **Enum JSON move** → the four functions are tiny and already covered by the existing enum
  serialization tests in `app_suite_test.cpp` and `runner_test.cpp`.
- **More headers, same TUs** → stage 1 deliberately does not split `app_suite.cpp`; the next stage
  will do the TU split. This keeps the review focused and the diff mechanical.
- **Detail header growth** → `sys_helpers.hpp` loses `StartReport` and the duplicate helpers but
  remains the cohesive fd/time/process/JSON utility group; the new `cpu_validation.hpp` and
  `path_utils.hpp` are narrow and independently includable.

## Workflow follow-up

- After implementation passes the full build matrix, archive `refactor-runner-structure`.
- Stage 2 (separate change): `app_suite.cpp` → `suite_manifest.cpp` / `suite_supervisor.cpp` /
  `suite_control.cpp`; unify the two settings writers; one validation pipeline.
