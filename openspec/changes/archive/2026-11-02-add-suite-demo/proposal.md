# Proposal

## Why

The project has rich per-module examples (application lifecycle, command-line
settings, metrics, tracing, diagnostics) but nothing that exercises
`nexigen-run`'s suite supervisor end to end. The suite machinery — declarative
manifest, impact policy (`failure`/`degrade`/`not_impact`), per-attempt
artifact directories, whole-suite restart, and `runner.jsonl` lifecycle
logging — is covered only by unit tests that launch `/bin/sh` stubs. There is
no runnable demonstration that several real `Infrastructure`-based applications
can be launched, supervised, restarted, degraded, and failed by `nexigen-run`
while writing logs, metrics, traces, and chrono snapshots. An operator needs a
concrete, observable scenario to validate every runner component working
together.

## What Changes

- Add a single configurable example application `ex_suite_app` under
  `examples/suite/`, built on `nexigen::app::Infrastructure`, that emulates
  long-running real-time work: it writes logs at roughly 1–2 messages per
  second, produces all four telemetry streams (`app.jsonl`, `metrics.jsonl`,
  `traces.jsonl`, `chrono.jsonl`), and records at least one instrument of every
  metric kind, with a one-second collection/reset cadence.
- Add a JSON suite manifest `suite.json` that launches three instances of the
  one binary under distinct application identities (`app`), demonstrating a
  single `command` with per-instance `env`, `args`, global settings, and local
  settings and the runner's settings-merge precedence.
- Configure the three instances with the three impact levels: a healthy
  `not_impact` worker, a `degrade` instance that exits non-zero once per minute
  (retried by the runner until its budget is exhausted, then the suite becomes
  `degraded`), and a `failure` instance that aborts after five minutes
  (triggering a whole-suite restart, then consuming the shared
  `suite_restart_attempts` budget until the suite is `failed`).
- Add `run_suite_demo.sh` and `clean_suite_demo.sh` driver/cleanup scripts plus
  a README section so the scenario is runnable by an operator and observable
  through `nexigen-run status --json`, `runner.jsonl`, `attempt-*` artifact
  directories, `resource.json`/`exit.json`, and the suite metadata files.

## Capabilities

### New Capabilities

- `examples/suite-demo`: the runnable multi-application suite demonstration —
  the configurable `ex_suite_app` binary (its roles, telemetry, and failure
  modes), the three-role suite manifest, and the observable restart / degrade /
  whole-suite-failure timeline.

### Modified Capabilities

- `examples`: extend the runnable example catalogue with `ex_suite_app`, its
  CTest smoke entry, the `run_suite_demo.sh` / `clean_suite_demo.sh` helper
  scripts, and README documentation of the new example.

## Impact

- New example sources and scripts under `examples/suite/`, a new CMake target
  registered through the existing `nexigen_add_example` helper, and a new
  manifest/settings asset set.
- No changes to library code or public APIs: the example consumes the existing
  `nexigen::app` (Infrastructure, command-line, settings, telemetry-layout) and
  `nexigen::runner` surfaces plus the `nexigen-run` CLI.
- Performance, latency, and memory allocation: no library hot-path changes. The
  example's tick loop is allocation-free and lock-free (pre-allocated metric
  instruments, deferred logging), matching the project mandates; the scenario
  itself is a manual, minutes-long demonstration with no latency budget. The
  only build-mode expectation is that `ex_suite_app` builds and its bounded
  worker-mode smoke run exits 0 under Debug, Release, ThreadSanitizer, and
  AddressSanitizer.
