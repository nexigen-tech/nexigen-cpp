# Tasks

## 1. Paced workload and settings

- [x] 1.1 Set `"collect_interval_ns": 1000000000` in the `kMetricsSettings` and `kChronoSettings` strings and verify that after a run `./nexigen-example-settings/metrics.json` and `./nexigen-example-settings/chrono.json` contain that value
- [x] 1.2 Replace the tick-cap loop with a deadline-driven paced loop (15 s at a 10 ms tick period, ~500 ms log cadence) and verify with `/usr/bin/time` (or `date +%s`) that the example runs at least 10 s and still exits 0 well within the runner's 60-second timeout

## 2. Traces, background collection, docs

- [x] 2.1 Record the three-trace structure (startup / tick-loop / shutdown) with explicit-parent nested children, at least Internal/Producer/Consumer span kinds and int64/string attributes, and verify `./nexigen-example-output/traces.jsonl` contains at least 3 distinct trace ids, parent-child edges, at least 2 span kinds and attribute entries
- [x] 2.2 Remove the periodic `collect_now()`/`snapshot_now()` calls (keep one final pair before shutdown) and verify `metrics.jsonl` holds at least 10 metric batches and `chrono.jsonl` at least 10 timestamp snapshots produced by the background workers
- [x] 2.3 Update the README `ex_app_cli_settings` description to cover the fifteen-second paced workload, the one-second collection interval and the nested multi-trace output, and verify the rendered section matches an actual run

## 3. Self-verification and integration

- [x] 3.1 Extend the example's end-of-run artifact checks to enforce the spec floors (>=10 metric batches, >=10 chrono snapshots, >=3 trace ids) and verify a normal run exits 0 and prints the observed counts on the summary line
- [x] 3.2 Verify `nexigen_app_tests` and `ex_app_cli_settings` pass under a Release build and a TSan build (`ctest --output-on-failure`) without new warnings under `-Werror`

## Workflow follow-up

- Archive the change after the project's review requirements are satisfied.
