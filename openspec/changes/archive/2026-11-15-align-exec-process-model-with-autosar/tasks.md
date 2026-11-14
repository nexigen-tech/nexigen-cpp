# Tasks

## 1. Vocabulary and manifest types

- [x] 1.1 Add `ProcessState` (`Idle`, `Starting`, `Running`, `Terminating`, `Terminated`) with `to_string`/`from_string`, ADL JSON, and `std::formatter`; add round-trip tests.
- [x] 1.2 Add the `StateResult` vocabulary (`Success`, `InProgress`, `OperationFailed`, `OperationCanceled`, `UnexpectedTermination`, `InvalidTransition`, `InvalidArgument`) with the full conversion set and tests; extend `StateAck` with `result`.
- [x] 1.3 Replace `AppSpec.function_groups` with one `function_group` and a non-empty `startup_configs` list of `StateDependentStartupConfig` (state, args, env, startup/termination timeouts, restart attempts/delay); add `ExecutionDependency` (process + `Running`/`Terminated`). Add JSON round-trip tests.
- [x] 1.4 Extend manifest validation: exactly one Function Group per process; every startup config names an existing state of that group; `Off` is rejected; duplicate state configs are rejected; cross-group and cross-state dependencies, mutual dependencies, and cycles are rejected. Verify with focused validation tests.
- [x] 1.5 Migrate existing suite manifests, examples, and test fixtures to the per-state schema; add a migration note to `docs/architecture.md`.
- [x] 1.6 Add `FunctionGroupConfig.initial_state` (JSON round-trip, default first non-`Off`, unknown state rejected); after suite start start the processes configured for the initial state. Add tests for explicit, default, and unknown values.

## 2. Process table and readiness

- [x] 2.1 Introduce a cold-path `ProcessTable` (or extend `ExecutionStateTracker`) storing termination behavior, reporting mode, `ProcessState`, startup config, and attempt; pre-size it from the manifest.
- [x] 2.2 Implement reporting vs non-reporting: reporting processes stay `Starting` until `ReportExecutionState(Run)`; non-reporting processes enter `Running` after creation; reports from non-reporting processes are rejected and logged. Add unit tests.
Suite-supervisor unification (COM-gated `nexigen_exec_control`, readiness
inbox in the supervisor, and folding `nexigen-em` into that loop) is deferred to
the follow-up `unify-exec-suite-control-path` change.

## 3. Transition engine

- [x] 3.1 Build the `state -> [process]` table in `FunctionGroupRegistry` from validated startup configurations; add a test for state-specific process sets.
- [x] 3.2 Implement `FunctionGroupTransition` with the terminate/restart/start phases and dependency order per phase; track current/requested state, per-process targets, deadlines, and restart budgets.
- [x] 3.3 Implement startup/termination timeouts: startup retries up to the configured attempts then stops with `OperationFailed`; termination timeout force-kills and continues; every timeout is logged.
- [x] 3.4 Implement the failure state: stop the transition, set the group to `Undefined`, and report `OperationFailed`/`UnexpectedTermination`; success sets the requested state current.
- [x] 3.5 Implement single-flight transitions and cancellation: a new validated request cancels the in-flight transition (the old `InProgress` is superseded); same-state requests are ignored with `Success`; `StateClient` reports `InProgress` while running.
- [x] 3.6 Update `nexigen-sm` to retry `InProgress` and surface terminal transition failures; keep the existing `SafeState` priority path intact.

## 4. Execution dependencies

- [x] 4.1 Implement state-qualified dependencies: wait for `Running` or `Terminated` within the current transition; terminate in reverse dependency order; do not treat an already self-terminated process as an error.
- [x] 4.2 Enforce same-group/same-requested-state scope and reject missing requested-state dependencies with a visible startup failure.
- [x] 4.3 Replace the dependency deadline derived from `idle_timeout_s` with the dependency’s configured startup timeout; add tests for timeout, retry, and missing-dependency paths.

## 5. Documentation and verification

- [x] 5.1 Update Doxygen comments (`/// @brief`, `@param`, `@return`, `@tparam`) in every touched `include/` header.
- [x] 5.2 Update `docs/architecture.md` module map and Mermaid diagrams for the `nexigen-em` control path, process states, and state→process mapping; note the suite-supervisor unification follow-up.
- [x] 5.3 Add transition/Undefined/cancellation tests, readiness-gated dependency tests, non-reporting process tests, and one-Function-Group/no-`Off` validation tests.
- [x] 5.4 Compile and pass 100% of unit tests under Debug, Release (`-O3 -march=native -flto` with latency gates), ThreadSanitizer (`-DNEXIGEN_ENABLE_TSAN=ON`), and AddressSanitizer (`-DNEXIGEN_ENABLE_ASAN=ON`).
- [x] 5.5 Run `clang-format`, the naming/complexity/result-value gates, and `graphify update .`; confirm no hot-path allocation or latency regression.

## 6. Verification follow-ups

- [x] 6.1 Complete daemon lifecycle tracking: `on_reaped` moves `Terminating` to `Terminated`; poll `exit.json` for every started process, not only oneshots. Add daemon-stop and daemon-death tests.
- [x] 6.2 Reverse the suite supervisor termination order by dependencies and scope the readiness-inbox requirement to the `nexigen-em` path; record the suite unification as `unify-exec-suite-control-path`.
- [x] 6.3 Add explicit tests for termination-timeout-to-`Off` and same-state `SetState` success.
- [x] 6.4 Store the startup configuration and attempt in the process-table record; document `OperationCanceled` as reserved for the future request-id/async-ack protocol; scaffold the follow-up change proposal.
