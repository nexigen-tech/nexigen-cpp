# Proposal

## Why

The 2026-11-07 `cpp-project-analyzer` run (cloc + lizard) found four
production functions exactly one point above the project's `CCN ≤ 15` target:

- `nexigen::app::Infrastructure::initialize` — CCN 16
  (`include/nexigen/app/infrastructure.hpp`)
- `nexigen::logging::FileLogger::initialize` — CCN 16
  (`include/nexigen/logging/file_logger.hpp`)
- `nexigen::tracing::TracerProvider<...>::consume_loop` — CCN 16
  (`include/nexigen/tracing/tracer_provider.hpp`)
- `nexigen::app::safe_id` — CCN 16 (`src/app/telemetry_layout.cpp`)

They are the only real production-code findings above the threshold; all other
`CCN > 15` hits are tests, examples, Python helpers, or lizard `alignas`
false positives. This change brings the production surface fully under the
target.

This is a **pure behaviour-preserving refactor**: no public API, on-disk
contract, validation rule, error code, signal sequence, worker ordering, or
observable behaviour changes.

## What Changes

- **app/Infrastructure**: extract the per-component construction phases
  (chrono + logger, metrics, tracing) and the emplace/rollback guard into
  private member helpers. `initialize` becomes claim-signals → build phases →
  start watcher. Rollback semantics stay identical.
- **logging/FileLogger**: extract the `FileLoggerConfig` validation predicates
  into private static helpers. `initialize` keeps the same validation order,
  the same `InvalidArgument` mapping, and the same worker-start sequence.
- **tracing/TracerProvider**: extract the worker startup sequence, bounded
  batch delivery, flush handshake, and shutdown drain from `consume_loop`.
  The pinned-worker start order, first-error snapshot, backoff loop, and
  drain-on-join guarantees are unchanged.
- **app/telemetry_layout**: extract the allowed-character predicate from
  `safe_id`; the accepted identifier grammar is unchanged.

## Capabilities

### New Capabilities

None.

### Modified Capabilities

None. No spec-level behaviour changes; the change sets `skip_specs: true`.

## Impact

- **Code**: `include/nexigen/app/infrastructure.hpp`,
  `include/nexigen/logging/file_logger.hpp`,
  `include/nexigen/tracing/tracer_provider.hpp`,
  `src/app/telemetry_layout.cpp`.
- **Public API**: none. Only private member helpers and one file-local
  predicate are added; no public symbol changes.
- **Performance / latency / allocations**: none. All extracted code is
  cold-path (initialization, worker loop scaffolding, identity validation);
  no hot-path instruction is moved, and the worker loop's batch size,
  backoff, and publication order stay identical.
- **Dependencies / build**: none.
- **Verification**: full Debug, Release, ASan, and TSan matrices plus the
  existing coverage gates; `clang-format` and lizard must show every touched
  function ≤ CCN 15.
