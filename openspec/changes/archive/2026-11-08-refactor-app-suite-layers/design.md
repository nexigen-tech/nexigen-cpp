# Design

## Context

`src/runner/app_suite.cpp` (1677 lines) currently owns every suite concern:

- DTO serialization (`to_string`, `to_json`, `from_json`) for `AppSpec`, `SuiteManifest`,
  `SuiteAppMetadata`, `SuiteMetadata`, `SuiteExit`, `SuiteAppStatus`, `SuiteStatus`.
- Manifest validation and `load_suite_manifest`.
- Runtime supervision (`RuntimeApp`, `RuntimeSuite`, launch, retry, suite restart,
  `advance_app`, `run_suite_supervisor`).
- Detached control (`start_suite`, `stop_suite`, thread preflight, fork/child setup).
- Reporting (`suite.json`, `suite-state.json`, `suite-exit.json`, `suite_status`,
  forced-stop finalization).

The existing `runner/app-suite` spec already requires an explicit `AppStateMachine` and
the decomposition of `start_suite`, `stop_suite`, and `suite_status`, but the
implementation still keeps the old monolithic shape. This change performs the
translation-unit split without changing observable behavior.

## Goals

- One translation unit per responsibility, each small enough to review independently.
- Scenario resolution separated from process control: the supervisor asks a resolver
  for a ready launch plan instead of assembling `StartOptions` inline.
- Reporting isolated so every writer of `suite.json` / `suite-state.json` /
  `suite-exit.json` lives in one place and the supervisor never builds JSON directly.
- Implement the spec's `AppStateMachine` transitions.
- Keep every public header, on-disk schema, log line, signal sequence, and return code
  identical.

## Non-Goals

- No changes to `Runner`, the CLI, the manifest schema, or the suite spec's behavior.
- No new public symbols.
- No change to the 100 ms poll cadence, the restart policy, or the process-tree shape.
- No concurrency or hot-path work; the suite remains cold-path only.

## Target layout

```
src/runner/
├── suite_manifest.cpp     scenario resolution: DTO serialization, path contract,
│                          validation, load_suite_manifest
├── suite_launch_plan.cpp     scenario resolution: one AppSpec + attempt -> SuiteLaunchPlan
├── suite_supervisor.cpp   process orchestration: runtime model, AppStateMachine,
│                          launch/stop policy, supervisor loop, child setup
├── suite_control.cpp      process orchestration: start_suite/stop_suite control,
│                          thread preflight, conflict check, fork handshake
└── suite_reporting.cpp    reporting: suite.json / suite-state.json / suite-exit.json,
                           terminal forced-stop records, suite_status derivation

include/nexigen/runner/detail/
├── suite_launch_plan.hpp  struct SuiteLaunchPlan + resolve_suite_launch()
├── suite_reporting.hpp    report builders/writers used by supervisor and control
└── suite_supervisor.hpp   run_suite_supervisor() + shared poll interval
```

`src/runner/app_suite.cpp` is deleted. `CMakeLists.txt` already globs
`src/runner/*.cpp` with `CONFIGURE_DEPENDS`, so the build picks up the new files without
an edit.

## Responsibilities

### D1 — Scenario resolution

`suite_manifest.cpp` keeps all manifest-facing code:

- The JSON field readers used only by suite DTO deserialization.
- All suite DTO `to_string` / `to_json` / `from_json` definitions.
- `suite_dir_path` / `attempt_dir_path`.
- The validation pipeline (`validate_suite_manifest`, per-field helpers) and
  `load_suite_manifest`.
- Validation limits (`max_apps`, `max_restart_attempts`, `max_delay_seconds`).

`suite_launch_plan.cpp` turns one application and one attempt into a launch plan:

```cpp
namespace nexigen::runner::detail {

struct SuiteLaunchPlan {
    std::string artifact_dir{};
    StartOptions options{};
    app::RunIdentity identity{};
    std::vector<std::string> command{};
};

[[nodiscard]] SuiteLaunchPlan resolve_suite_launch(const SuiteManifest& manifest,
                                                   const AppSpec& spec,
                                                   std::uint32_t attempt,
                                                   std::string_view instance_dir);
}
```

The resolver owns exactly the rules previously inlined in `launch_app`:

1. `artifact_dir = attempt_dir_path(artifacts_path, suite_id, app, instance, attempt)`.
2. Global settings: application path replaces the suite path when non-empty.
3. Local settings: resolved against the manifest directory.
4. Log level: application value, else suite value, else empty (inherit).
5. `env` and `cpus` copied from the application spec.
6. `command` = command plus arguments, with no shell interpretation.
7. `RunIdentity` = suite `run_id` / `run_dir`, application instance and instance dir.

`launch_app` then only increments the attempt, calls the resolver, invokes
`Runner::start`, and updates runtime state. This is the "scenario resolution" seam.

### D2 — Process orchestration

`suite_supervisor.cpp` owns the runtime model and the supervisor loop. The runtime
types stay private to the TU. The file keeps these behavior-critical pieces unchanged:

- `RuntimeApp` / `RuntimeSuite` fields and defaults.
- Launch ordering, stop ordering (per-instance stop, then process-group SIGKILL),
  `reap_supervisors`, retry and suite-restart budgets.
- `run_suite_supervisor` startup order: reset affinity, apply control-thread spec,
  write `suite.json`, launch all apps, write the start report, initialize logging,
  log initial launches, apply launch failures, then poll.
- 100 ms poll interval and stop-request handling.

The only structural change is the explicit state machine:

```cpp
class AppStateMachine {
public:
    AppStateMachine(RuntimeSuite& suite, RuntimeApp& app) noexcept;
    [[nodiscard]] bool advance(std::uint64_t now);

private:
    [[nodiscard]] bool on_exit_consumed(const ExitMetadata& exit);
    [[nodiscard]] bool on_restart_timer(std::uint64_t now);
    void on_launch_failed();
    // ...
};
```

`advance()` dispatches on `Running` / `Restarting`; `on_exit_consumed` logs the exit,
transitions to `Exited` for code 0, or delegates to the impact policy for abnormal
termination; `on_restart_timer` launches and falls back to `schedule_retry_or_fail`;
`on_launch_failed` logs and schedules the retry. The existing free functions
(`schedule_retry_or_fail`, `trigger_suite_restart`, `finish_failed`,
`finish_suite_stop`, `handle_abnormal`, `apply_launch_failures`) remain the policy
implementation.

`suite_control.cpp` owns everything around the supervisor process:

- `preflight_suite_threads`, `suite_is_running`, `create_suite_pipes`,
  `run_suite_supervisor_child` (setsid + `/dev/null` redirect), `start_suite`;
- `stop_suite` phases: exit-file early return, metadata read, stop-request write,
  grace computation, exit wait, identity-gated fallback signaling, instance stop,
  terminal reporting;
- `suite_poll_interval_ms` is declared once in `detail/suite_supervisor.hpp` and shared
  by control and supervisor.

The child entry point calls `detail::run_suite_supervisor`, declared in
`detail/suite_supervisor.hpp`; its translation unit lives in `suite_supervisor.cpp`.

### D3 — Reporting

`suite_reporting.cpp` owns every write/derive operation:

- `build_suite_metadata`
- `write_suite_status_file(const SuiteStatus&, state_path)`
- `write_suite_exit_file(const SuiteExit&, exit_path)`
- `write_suite_metadata_file(const SuiteMetadata&, suite_dir) -> bool`
- `write_terminal_suite_state` (forced stop)
- `suite_status` and its helpers (`derive_suite_state`, `live_status_to_app_state`,
  `merge_app_status`)

The supervisor builds a `SuiteStatus` from its private runtime model and hands it to the
reporting writer; it no longer touches `nlohmann::json` directly. `write_suite_exit`
in the supervisor keeps its semantic order (set `suite.state`, write exit, write
status). `write_suite_metadata_file` returns `bool`; the supervisor keeps the
`StartReport` + `child_exit(1)` failure path, so process-exit policy stays in
orchestration.

The public `suite_status()` implementation moves to the reporting TU. It keeps the same
sticky `Failed` / `Restarting` merge and the same live-status mapping.

### D4 — Declaration seams

The new detail headers declare only what crosses a translation unit:

- `suite_launch_plan.hpp`: `SuiteLaunchPlan` and `resolve_suite_launch`.
- `suite_reporting.hpp`: report builders/writers (public suite structs only; no runtime
  types leak out).
- `suite_supervisor.hpp`: `run_suite_supervisor` and `suite_poll_interval_ms`.

No `RuntimeApp` / `RuntimeSuite` type crosses a TU, so orchestration state stays private.
All detail functions live in `nexigen::runner::detail` and are not installed in the
public umbrella headers.

### D5 — Behavior preservation

The move is mechanical except for the explicit state-machine extraction:

1. Same JSON field names, order of writes, and atomic-write helper.
2. Same log events, levels, and formatted text.
3. Same signal order: `Runner::stop` then `kill(-pgid, SIGKILL)` on remaining groups.
4. Same retry timing (`monotonic_now_ns() + delay * nanoseconds_per_second`).
5. Same start handshake: `suite.json` first, then start report, then logging.
6. Same stop phases and identity gate before fallback signals.
7. Same public API and umbrella headers.

## Concurrency / memory layout

No new concurrent or atomic component is introduced, so there is no `alignas(64)`,
memory-order, or shard-sizing decision in this change. The existing single-threaded
supervisor loop and its 100 ms sleep are unchanged. `SuiteLaunchPlan` is a stack value
per cold-path launch; its vectors are built from the same strings that `launch_app`
already built inline, so allocation counts are unchanged.

## Risks / Trade-offs

| Risk | Mitigation |
|---|---|
| Behavior drift while moving code between TUs | Move bodies verbatim; run the existing suite tests unchanged; diff JSON/log output with the pre-refactor binary. |
| Hidden dependency through the anonymous namespace | Every cross-TU symbol is explicit in a detail header; no runtime type leaks. |
| Include bloat from `suite_reporting.hpp` | The header only needs public suite structs and forward declarations; `nlohmann::json` stays inside the reporting TU. |
| Coverage drops because code moved | The coverage target is unchanged; all new TUs are exercised by the same `nexigen_app_suite_tests`, and `suite_manifest.cpp` keeps the manifest tests. |
| Lizard regression in a new file | Keep policy functions small; run lizard on all five TUs before finishing. |
