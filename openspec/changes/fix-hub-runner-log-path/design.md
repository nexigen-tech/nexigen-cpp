# Design

## Context

`nexigen-run` writes its suite runner log under the suite's artifact directory:
`<artifacts_path>/<suite_id>/runner.jsonl` (both the suite supervisor and the CLI
`nexigen-run` use `join(artifacts_path, suite_id)` as the log directory). The hub's
discovery (`src/hub/discovery.cpp`, `read_suite`) builds the runner stream descriptor with
the correct stream id (which already carries `suite_id`) but the wrong path:

```cpp
out.runner_log.path = join(out.artifacts_path, "runner.jsonl");  // missing <suite_id>
```

The hub test fixture (`tests/hub/hub_test.cpp` `Fixture`) writes `runner.jsonl` at
`artifacts/runner.jsonl` to match this wrong path, so the bug has never been exercised against
the real runner layout. The `hub_cli_test.sh` smoke only checks `--help`, missing `--run-dir`,
and the non-loopback warning; it never discovers a real runner suite.

## Goals / Non-Goals

**Goals:**

- Make the hub's runner log stream path match where `nexigen-run` actually writes it, so the
  runner stream opens and the SSE stream flows (browser stops hanging on "connecting").
- Pin the behavior with a spec scenario and a regression assertion.

**Non-Goals:**

- No change to the runner's log location, the stream identity scheme, or any other stream path.
- No change to the HTTP/SSE framing or ring behavior.

## Decisions

### D1 — Resolve the runner log path as `<artifacts_path>/<suite_id>/runner.jsonl`

Change `read_suite` in `src/hub/discovery.cpp` to build the path with the suite id:

```cpp
out.runner_log.path = join(join(out.artifacts_path, out.suite_id), "runner.jsonl");
```

- *Rationale:* this is exactly where `nexigen-run` writes the file; the stream id already
  encodes `suite_id`, so the path was simply missing one segment.
- *Alternative considered:* change the runner to write `artifacts_path/runner.jsonl`. Rejected:
  the runner deliberately namespaces its log under the suite id (multiple suites can share one
  artifacts root), and that layout is already specified in `runner/logging`.

### D2 — Align the test fixture with the real runner layout

Change the `Fixture` constructor to write `runner.jsonl` at
`<artifacts>/<suite_id>/runner.jsonl` (i.e. `artifacts + "/hil/runner.jsonl"`), and add an
assertion in `test_discovery` that `suite.runner_log.path` equals that exact path.

- *Rationale:* the fixture should model the real on-disk layout, and the new assertion pins the
  path so a regression is caught.
- *Alternative considered:* leave the fixture at the old path and assert only the id. Rejected:
  that would keep testing a path the runner never produces.

## Risks / Trade-offs

- [Fixture drift] The fixture previously encoded the wrong layout; updating it to the real one
  removes the mismatch rather than masking it. → Mitigation: the new path assertion ties the
  fixture to the fixed behavior.
- [No other callers of `runner_log.path`] Only discovery builds this path; the streaming layer
  opens whatever path discovery reports, so the one-line fix is sufficient. → Mitigation:
  verified by the updated `test_discovery` and the streaming tests that already sync the tree.

## Migration Plan

Not applicable — a bug fix with no persisted-format or API change. Deployed hubs rebuild and
restart; no state migration is needed.

## Open Questions

None.
