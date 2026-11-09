# Proposal: Extract ChildProcess Class for Child Application Supervision

## Why

The suite supervisor in `suite_supervisor.cpp` keeps a local `RuntimeApp` struct
for one child application's runtime state (PID, supervisor PID, attempt/restart
counters, lifecycle state) and reaches for `Runner` directly on every start and
stop. This scatters the "which process is this app, and how do I start/stop it"
concern across the file and gives it no named type.

The goal is to extract a `ChildProcess` class that owns that runtime state and
encapsulates the start/stop actions — a **thin wrapper over the existing
`Runner`** — so the suite supervisor works with one clear type instead of a
local struct plus ad-hoc `Runner` plumbing.

## What Changes

### New Capabilities
- **ChildProcess class**: A named type for one child application's runtime
  state and lifecycle actions (`launch` / `stop`), delegating the actual
  fork/exec/signal work to the existing `Runner`.

### Modified Capabilities
**None** - This is a pure architectural refactoring with no behavioral changes
to the public API.

## Capabilities

### New Capabilities
- **child-process-management**: A thin wrapper that owns one child application's
  runtime state and delegates start/stop to `Runner`.

### Modified Capabilities
**None** - No spec-level behavior changes; this is an implementation-level refactoring.

## Impact

### Affected Files
- **New**: `include/nexigen/runner/child_process.hpp` - Public interface
- **New**: `src/runner/child_process.cpp` - Implementation (delegates to `Runner`)
- **Modified**: `include/nexigen/runner/app_suite.hpp` - Add umbrella include
- **Modified**: `src/runner/suite_supervisor.cpp` - Replace the local `RuntimeApp`
  struct with `ChildProcess` and route start/stop through it

### Performance Impact
- **No impact on hot paths**: Child supervision runs only on cold paths
  (start/stop/status). `ChildProcess::launch`/`stop` are thin forwarding calls
  to the same `Runner` methods the supervisor already invoked.
- **Memory**: One `ChildProcess` instance per suite application (a few dozen
  bytes of value state, unchanged from the former `RuntimeApp`).
- **CPU**: Negligible — one extra non-virtual function call per launch/stop.

### Breaking Changes
- **None** - Public API of `Runner` remains unchanged; `ChildProcess` is additive.
- `src/runner/runner.cpp` is left untouched, so single-instance supervision is
  unaffected by a rollback.

### Dependencies
- No new dependencies required
- Uses existing: `AppSpec`, `AppState`, `StartOptions`, `Runner`, `ResourceMetadata`
