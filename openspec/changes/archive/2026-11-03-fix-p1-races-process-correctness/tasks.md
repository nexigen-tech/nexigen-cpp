# Tasks

## 1. Runner: identity gate before kill (P1-5)

- [x] 1.1 In `include/nexigen/runner/instance_lifecycle.hpp`: add
  `proc_start_ticks` (`std::uint64_t{0}`) to `InstanceMetadata`; update its
  to_json/from_json/to_string (0 = absent, never assert a field) so
  resource.json round-trips; extend `app_suite.cpp` (suite metadata write at
  `:486`, read at `:506`) and the runner's resource writers to record the
  child's `/proc/<pid>/stat` starttime ticks at spawn. Verified: runner +
  suite tests green; field recorded in both `resource.json` (instance) and
  `suite.json` (suite supervisor's own ticks at spawn).
- [x] 1.2 In `src/runner/detail/sys_helpers.hpp` (or a small new reusable
  helper next to `group_alive`/`process_alive`): add
  `proc_start_ticks(pid)` reading `/proc/<pid>/stat` field 22 (split-safe scan
  after the `)` comm terminator, `sysconf(_SC_CLK_TCK)` unneeded for raw
  ticks) with an error arm; add
  `matches_recorded_identity(pid, recorded_ticks)` (recorded == live, both
  nonzero). Implemented as `proc_start_ticks()` + `identity_matches()` (mgr
  name keeps the "recorded identity" wording): 0 anchor or unreadable /proc =
  legacy trust, readable-different pair = recycled. Verified: unit pump via
  runner suite (recycling doctored-anchor test).
- [x] 1.3 Gate `Runner::stop` (`runner.cpp:885–937`): compute identity once
  before the first `kill(-pid, …)`; on mismatch take the dead-instance
  fallback (`write_fallback_exit` with `TerminatedBy::Unknown`, no signals,
  no failure); keep exact existing behavior on match and when both sides are
  0/legacy-absent. Same gate for `Runner::status`'s `running` classification
  (`runner.cpp:958`) and for the idempotent exit.json branch's group_alive
  complaint. Verified: runner_test — doctored resource.json with a
  wrong-but-live pid reports unknown instead of signaling; normal suites
  green.
- [x] 1.4 Add `tests/runner/runner_test.cpp` scenarios: (a) stop with matching
  anchor still signals normally (existing happy path green; added positive
  anchor assertion); (b) recycled identity test `test_recycled_pid_identity()`
  — doctored foreign anchor present → no kill fallout, fallback exit.json
  `terminated_by: "unknown"`; (c) status with mismatched anchor reports not
  running. Verified: suite green (308 checks, 0 failed).

## 2. Runner: waitpid outcome honesty + failed-start reaping (P1-6, P1-14)

- [x] 2.1 In `src/runner/runner.cpp` `SuperviseResult` +
  `supervise_child()`: add `status_valid` flag; set it only on waitpid
  success; error legs (≠EINTR, incl. ECHILD at `runner.cpp:287–293`) leave
  `status` untouched and `reaped=false`; thread `status_valid` through
  `attribution()` (`runner.cpp:302–315`) so the invalid case maps to
  `TerminatedBy::Unknown` and the exit writer emits `exit_code: null` /
  `signal: null`. Implemented: supervisor exit path branches on
  `status_valid` (unknown record skips `from_child_status` entirely —
  impossible `WIFEXITED(0)`). Note: no `waitpid` fault-injection hook exists
  (adding one stays out of scope); the honest path is covered by the
  normal-flow suites plus code review of the error legs. Verified:
  TSan/ASan suites green later in the matrix.
- [x] 2.2 Failed-start reap: replace the single `WNOHANG` attempt
  (`runner.cpp:818–821`, `app_suite.cpp:1427–1431`) with a bounded
  retry-then-blocking wait helper (EINTR-tolerant; ~5 s deadline) from 1.2's
  header; both launch callers use it. Implemented as
  `detail::reap_failed_supervisor()` — bounded WNOHANG drain only (no final
  blocking wait: the suite supervisor can legitimately outlive a failed
  report write), so fault injection cannot stall the launcher. Verified:
  runner_test — doctored failure path leaves no zombie (WNOHANG find-orphan
  probe after failed starts in `test_failed_exec_visible`).
- [x] 2.3 Extend `Runner` CLI-path env validation (`runner.cpp:103–109`) to
  reject NUL inside values (`value.find('\0')`); add the suite-path comment
  cross-reference. Verified: `test_start_options` — a NUL-in-value env entry
  returns `InvalidArgument` before fork (18 checks green).
- [x] 2.4 Update delta-spec scenario coverage mapping in
  `tests/runner/runner_test.cpp` comments (identity/ECHILD/no-zombie/env-NUL).
  Done: every new test block cites its spec requirement name.

## 3. Logging: spam config race + dead SPSC pair + TLS pool cache (P1-9, P1-7, P1-10)

- [x] 3.1 In `include/nexigen/logging/detail/spam_protection.hpp`: convert the
  four producer-read quota fields to per-field `std::atomic` (relaxed) inside
  `SpamProtection`; `set_config` stores each field; hot-path getters
  (`check_repeat`, `check_rate_limit`) load relaxed with unchanged logic;
  document the field-atomic/eventually-consistent semantics (removed
  "wait-free" claim). Verified: logging tests green; `-Werror` build clean.
- [x] 3.2 Delete `include/nexigen/logging/detail/spsc_shard_ring.hpp` and
  `spsc_shard_pool.hpp`; rewrite the `producer.hpp` shard() doc comment
  (lines ~316–319) to point at the live `core::ShardPool` DropNewest policy.
  Verified: zero references remain (`grep -r SpscShard(Ring|Pool)` outside
  openspec); logging + runner_logging suites green after removing the two
  dead-code test suites from logging_test.
- [x] 3.3 In `include/nexigen/core/shard_pool.hpp` `claim_slot()`: widen the
  TLS entry array 4→8; on a full cache return -1 accounting
  `slot_exhausted` WITHOUT `fetch_add` on `registration_.next_slot`; fix the
  full-map comment to state the counted-drop semantics. Verified:
  `tests/core/core_test.cpp` — new `shard_pool_tls_cache` suite: 8 distinct
  pools steady-path cached, ninth pool drops counted (exactly 1), 100
  repeated overflow edges keep the pool's registration watermark at 0
  (old burn path gone), second-thread claim takes the next pool slot
  (first TSan run exposed a harness race — thread-slot read before join —
  fixed by joining before reading; now TSan-clean).
- [x] 3.4 Add `tests/logging/logging_test.cpp` race test: one thread calls
  `set_spam_protection_config` in a loop while 4 producer threads emit (TSan
  build target — no race reports, clean summary of "no torn read" per field).
  Implemented `spam_config_race` suite (reconfigurer thread flips a tiny
  quota against an unlimited one; 3 emitter lanes through the real
  AsyncWorker; assertions on the barrier-ordered sink contents). Verified:
  Debug + TSan green (0 race reports).
- [x] 3.5 (gap close for P1-15 "destruction-while-running") Add
  `shutdown_drain_race` to `tests/logging/logging_test.cpp`: producers
  outlive the worker scope intentionally, so the AsyncWorker destructor's
  stop-request/join/final-drain races real emission; squared conservation
  (sink-delivered + tail-drained + counted buffer-full == total) and the
  post-teardown main-drain tail. Verified: Debug green (914 checks), TSan
  green (914 checks, 0 reports), ASan full suite green.

## 4. Metrics: get_meter serialization (P1-8)

- [x] 4.1 In `src/metrics/provider.cpp` `get_meter`: hoist
  `std::lock_guard<std::mutex> lock(harvest_mutex_)` around
  lookup/create/publish (alive_ fast check first); confirm the harvester
  reset sections hold the same mutex (grep review — `collect_now()` holds
  it). Verified: `tests/metrics/metrics_test.cpp` — new
  `get_meter_harvest_race` suite: 4 concurrent get_meter creators against a
  spinning `collect_now()` sweep, one published handle per identity, late
  lookup idempotent (TSan verdict comes with the 8.2 matrix).

## 5. Tracing: int8 slot invariant (P1-11)

- [x] 5.1 In `include/nexigen/tracing/tracer_provider.hpp`: add
  `static_assert(MaxThreads <= 127, ...)` at the provider scope (design D9);
  cross-check `Tracer`/`ScopedSpan` instantiations compile. Verified: tracing
  suite green (74295 checks) on GCC; Clang build check comes with 8.1.

## 6. Chrono: start guard (P1-12)

- [x] 6.1 In `include/nexigen/chrono/chrono_provider.hpp`: guard
  `start(Logger&)` — if the worker is running, return `Conflict` without
  writing `log_binding_`; document rebinding rules in the header comment.
  Verified: `tests/chrono/chrono_test.cpp` — double-start with a second
  logger returns `Conflict`, first logger keeps receiving diagnostics, second
  stays empty; suite green (1411 checks).

## 7. Hub: IPv6 listen + CLI loopback fix (P1-13)

- [x] 7.1 In `src/hub/http_server.cpp`: extend `parse_listen` (design
  bracket-form rules, ≤1-colon bare rule); in `start()`, bind `AF_INET6` with
  `IPV6_V6ONLY` per design for v6 addresses and keep the AF_INET path for v4.
  Verified: `tests/hub/hub_test.cpp` — new `http_v6` suite: `[::1]:0` binds
  and a `/` request over `::1` round-trips (HTTP/1.1 response), `[::]:0`
  dual-bind accepts an IPv4 loopback connection via the wildcard, and the
  malformed forms (`[::1]`, `[::1]8080`, `[nope]:8080`, bare `::`,
  ambiguous `::1:8080:9`) are rejected. Suite green (763 checks).
- [x] 7.2 In `tools/nexigen-hub/main.cpp` (`:115–124`): strip brackets when
  extracting the host for the loopback check (fix `::1` truncation); make
  the non-loopback warning fire on v6 wildcards. Update usage text.
  Verified: `tests/hub/hub_cli_test.sh` — `--listen [::1]:PORT` starts
  WITHOUT the warning and prints the bracketed URL; `--listen [::]:PORT`
  WARNs; the earlier `[::]`-shortcut bug (initially treated `::` as loopback)
  was caught by the new CLI assertions and fixed.

## 8. Full verification matrix

- [x] 8.1 Debug + Release builds (tests+tools+examples) with all unit tests
  green on GCC; targeted suites compile clean on Clang; strict-latency
  Release run stays within gates (TSC ≤ 20 ns). Verified: Debug 35/35,
  Release 34/35 — the single Release failure is the strict TSC gate
  (`best_accepted_ns <= 20`) INSIDE logging_test; correctness suites all
  green. Clang 21 builds clean. **Environment caveat recorded**: the gate
  measured 10.01 ns (pass) in the dedicated strict build earlier this
  session, then the host degraded deterministically to ~130 ns around
  21:36 (ollama llama-server launch window); a `git stash` A/B on the
  pristine HEAD baseline measures the SAME ~130 ns, and taskset/pinned-core
  sweeps (2,4 / 8,40 / a warm-up burst) reproduce the value — so the gate
  value is environment-bound (desktop load + `powersave` governor, no root
  to change it), not a code regression. CI explicitly disables this gate on
  shared runners for the same reason (`.github/workflows/ci.yml:41–43`,
  "Shared runners cannot guarantee the 20 ns latency budget"). Code delta
  mine-vs-baseline at equal conditions ≈ 10 ns ≈ run-to-run band.
- [x] 8.2 TSan build (`NEXIGEN_ENABLE_TSAN=ON`): logging, metrics, chrono,
  tracing, runner, core suites — zero ThreadSanitizer reports across all six
  (logging 908 checks incl. `spam_config_race` + multithreaded flush race;
  metrics 518 978 checks incl. the new `get_meter_harvest_race`; runner
  covers identity/waitpid/zombie/env-NUL; core covers the widened TLS cache;
  one TSan finding during development — the worker-side `stamps_[i].sample()`
  in flush summaries (removed `flush_drop_summaries()` carried the same race)
  — was fixed in the P0 change via `worker_stamp()` before this matrix).
- [x] 8.3 ASan build (`NEXIGEN_ENABLE_ASAN=ON`): full suite green 15/15, no
  leak/UAF reports, including recycled-PID stop, failed-start zombie-free
  probes, get_meter harvester concurrency.
- [x] 8.4 Run `openspec validate --strict --changes` for this change.
  Verified: both changes validate clean (`✓ fix-p0-critical-defects`,
  `✓ fix-p1-races-process-correctness`).
