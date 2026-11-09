# Proposal

## Why

`src/runner/app_suite.cpp` is a 1677-line cold-path translation unit that mixes three
unrelated responsibilities: manifest/scenario resolution (JSON, defaults, validation,
settings/log-level resolution), process orchestration (fork, launch, restart, impact
policy, supervisor loop), and reporting (metadata/status/exit files and `suite_status`).
The mixture makes every change touch process control, serialization, and filesystem
contracts at once, and it keeps the suite specification's own decomposition requirements
(`AppStateMachine`, split `start_suite`/`stop_suite`/`suite_status`) unimplemented.

## What Changes

- Split the suite implementation into responsibility-focused translation units:
  - **Scenario resolution** — `suite_manifest.cpp` (manifest serialization, path
    contract, validation, `load_suite_manifest`) and `suite_launch_plan.cpp`
    (application launch scenario: attempt artifact directory, settings-source merge,
    effective log level, environment, CPU set, argv, run identity).
  - **Process orchestration** — `suite_supervisor.cpp` (runtime model, explicit
    `AppStateMachine`, launch/stop, retry and suite-restart policy, supervisor loop,
    supervisor child setup) and `suite_control.cpp` (`start_suite`, `stop_suite`,
    thread preflight, conflict detection).
  - **Reporting** — `suite_reporting.cpp` (`suite.json`, `suite-state.json`,
    `suite-exit.json`, terminal forced-stop records, and `suite_status` derivation).
- Introduce narrow internal detail headers for the new cross-TU seams
  (`suite_launch_plan.hpp`, `suite_reporting.hpp`, `suite_supervisor.hpp`).
- Move the suite DTO `to_string` / `to_json` / `from_json` definitions beside the
  manifest/resolution code and keep every on-disk schema and public symbol unchanged.
- Implement the state-machine decomposition already required by
  `runner/app-suite`: explicit `on_exit_consumed`, `on_restart_timer`, and
  `on_launch_failed` transitions instead of nested branches in `advance_app`.
- Preserve the 100 ms poll interval, process-tree ordering, signal sequencing,
  restart budgets, log messages, and JSON field names byte-for-byte.

**BREAKING**: none. This is an internal translation-unit split; no public header gains
or loses a symbol and no observable file, log, or return-code contract changes.

## Capabilities

### New Capabilities

None.

### Modified Capabilities

- `runner/app-suite`: update the internal decomposition requirement so the complexity
  target applies to the new responsibility-focused translation units instead of the
  removed `src/runner/app_suite.cpp`, and keep the observable-behavior guarantees.

## Impact

- **Code**: `src/runner/app_suite.cpp` is removed and replaced by
  `suite_manifest.cpp`, `suite_launch_plan.cpp`, `suite_supervisor.cpp`,
  `suite_control.cpp`, and `suite_reporting.cpp`. Three new
  `include/nexigen/runner/detail/` headers carry the cross-TU declarations. Public
  headers and the `nexigen::runner` API stay unchanged. `CMakeLists.txt` already globs
  `src/runner/*.cpp`, so no build-system edit is required.
- **Performance & latency**: zero hot-path impact. Every moved function remains
  cold-path control-plane code (manifest parsing, process creation, polling, JSON
  files). No change to allocation counts, syscall order, poll cadence, or timing.
  The only added cost is one extra `SuiteLaunchPlan` move per launch attempt, which is
  cold path and bounded by the existing per-attempt allocations.
- **Memory**: no new persistent state. `SuiteLaunchPlan` replaces local variables that
  already existed inside `launch_app`; total live allocations are unchanged.
- **Dependencies**: none new. The code keeps nlohmann/json, POSIX process APIs, and the
  existing runner logging/instance helpers.
- **Verification**: existing `nexigen_runner_tests`, `nexigen_runner_logging_tests`, and
  `nexigen_app_suite_tests` must pass unchanged; Debug/Release/ASan/TSan and the
  lizard CCN ≤ 15 gate apply to all new TUs.
