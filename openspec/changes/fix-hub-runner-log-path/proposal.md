# Proposal

## Why

`nexigen-hub` computes the suite runner log path as
`<artifacts_path>/runner.jsonl`, but `nexigen-run` writes it at
`<artifacts_path>/<suite_id>/runner.jsonl` (see
`run_suite_supervisor`'s `init_runner_logging(join_path(artifacts_path, suite_id), …)` and the
CLI's `manifest_log_target`). The mismatch makes the hub fail to open the runner stream with
`ENOENT`, which fails the initial stream sync, so the web UI and SSE subscribe to a stream that
never delivers and the browser hangs on "connecting". The suite demonstration surfaced this
because the hub had only ever been exercised against a synthetic test fixture that mirrored the
wrong path, never against a real runner suite.

## What Changes

- Fix the suite runner log stream path in `nexigen::hub` discovery to include the suite id:
  `<artifacts_path>/<suite_id>/runner.jsonl`.
- Align the hub test fixture so it writes `runner.jsonl` under
  `<artifacts_path>/<suite_id>/`, matching the real runner layout, and add an assertion that
  pins the resolved path.

## Capabilities

### Modified Capabilities

- `hub/discovery`: pin the suite runner log stream path to
  `<artifacts_path>/<suite_id>/runner.jsonl`, matching where `nexigen-run` writes it.

## Impact

- One-line change in `src/hub/discovery.cpp` plus a test-fixture and assertion update in
  `tests/hub/hub_test.cpp`. No public API, serialization, or data-model change.
- Performance, latency, and memory allocation: none — the fix changes only a cold-path path
  string built during the initial scan; no hot-path, tick, or ring code is touched.
