# Tasks

## 1. Binding and vocabulary

- [x] 1.1 Add `function_group` to `LightweightStateMachineConfig` and its JSON/diagnostics.
- [x] 1.2 Validate LSM bindings and state subsets in `SmManifest::validate`.
- [x] 1.3 Append `TransitionResult::ActionFailed` and update the vocabulary/tests.

## 2. Runtime

- [x] 2.1 Add `LsmRuntime` and `EmActionSink` with EM request, Field publish, state sync, and timeout handling.
- [x] 2.2 Enforce the RootFG application-group gate before an LSM runs.
- [x] 2.3 Route TriggerIn for bound groups through the LSM, keeping UCM lock precedence and the unbound path.
- [x] 2.4 Create the runtime in `run_sm` and pass it through the arbitration loop.

## 3. Tests and docs

- [x] 3.1 Add tests for binding validation, EM action routing, publish action, success sync, recovery fallback, ActionFailed, and the unbound path.
- [x] 3.2 Update `docs/architecture.md`.
- [x] 3.3 Run Debug, Release, TSAN, ASAN and gate-OFF; run complexity/naming/result gates and `graphify update .`.
