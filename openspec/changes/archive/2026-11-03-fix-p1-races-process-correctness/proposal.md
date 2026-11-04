# Proposal

## Why

The 2026-11-03 code analysis (P1 items, TODO.md) found races and
process-correctness defects that remain after the P0 change: `Runner::stop` and
`Runner::status` trust a PID from a runner-writable `resource.json` without
verifying the process identity (PID-recycling can direct SIGKILL at an
unrelated process), a `waitpid` error (ECHILD) is reported as a successful reap
with a stale status, a failed start leaves an unreaped zombie, several data
races remain in metrics/logging/chrono instrumentation, dead duplicate SPSC
machinery (analysis #7) contradicts the shipped spec, and hub cannot listen on
IPv6 while its CLI mis-parses `::1`.

## What Changes

- **runner/** — `resource.json` gains a `proc_start_ticks` identity anchor
  (Linux `/proc/<pid>/stat` field 22, recorded at spawn). `stop`/`status`
  re-derive the live process's start ticks and only act when both match;
  records missing the field keep today's legacy behavior. `waitpid` outcomes
  other than EINTR no longer fabricate a successful reap — an unreapable error
  drains into a `terminated_by: "unknown"` exit record instead of a stale zero
  status. The failed-start path reaps the supervisor child (bounded blocking
  waitpid after the single WNOHANG attempt). Environment values are validated
  for embedded NUL on the CLI path exactly like the suite manifest path already
  does.
- **logging/** — `SpamProtection::set_config` is re-typed as a field-atomic
  reconfiguration: the hot-path quota fields become atomics, removing the
  data race with producers; the requirement text stops claiming "wait-free"
  and describes the honest per-field-atomic, eventually-consistent semantics.
  The dead `SpscShardRing`/`SpscShardPool` header pair (unwired duplicate of
  `core::ShardPool` machinery, analysis #7) is deleted together with its
  contradictory specs (`logging/spsc-shard-ring`, `logging/spsc-shard-pool`).
  `core::ShardPool`'s TLS cache is widened 4→8 pools and cache exhaustion stops
  burning a global registration slot per record — overflow drops are counted
  through the existing `slot_exhausted` cell; the misleading "keeps producing"
  comment is corrected.
- **metrics/** — `MetricProvider::get_meter` publication and lookup are
  serialized with the harvester under `harvest_mutex_` (cold path only).
- **tracing/** — a `static_assert(MaxThreads <= 127)` pins the int8 slot
  invariant that `this_thread_slot()` truncation relies on (analysis #11).
- **chrono/** — a second `start(Logger&)` on an already-started provider is
  rejected with `Conflict` instead of racing the worker's `log_binding_` loads.
- **hub/** — the HTTP server accepts IPv6 listeners: bracketed forms
  (`[::1]:8080`, `[::]:8080`) and bare IPv6 literals with an explicit port
  notation rule; `::` binds dual-stack (`IPV6_V6ONLY=0`), a specific IPv6
  address binds `AF_INET6` with `IPV6_V6ONLY=1`, IPv4 behavior is unchanged.
  The `--listen` loopback warning check no longer truncates `::1` to `::`.

## Capabilities

### New Capabilities

(none — all fixes stay inside existing capability boundaries)

### Modified Capabilities

- `runner/process-supervision`: signals and reaping gain the identity gate, a
  no-fabrication rule for unreapable child errors, and supervisor reaping on
  failed starts; the environment contract additionally rejects NUL in values.
- `runner/instance-lifecycle`: `resource.json` records the new
  `proc_start_ticks` identity anchor.
- `logging/spam-protection`: replaces the "wait-free quota update" requirement
  with field-atomic, race-free reconfiguration semantics.
- `logging/multi-producer`: the registration requirement gains the bounded
  TLS-cache-exhaustion semantics (counted drop, no per-record slot burn).
- `logging/spsc-shard-ring` + `logging/spsc-shard-pool`: requirements removed
  together with the dead code they describe.
- `metrics/metric-provider`: meter creation is serialized with harvesting.
- `hub/http-api`: the listener requirement covers IPv6 addresses and correct
  loopback classification.
- `chrono/chrono-provider`: the optional worker logger requirement gains the
  double-start guard.

## Impact

- Code: `src/runner/runner.cpp` + `src/runner/app_suite.cpp` (identity, reap,
  zombie, env validation), `include/nexigen/runner/instance_lifecycle.hpp`
  (proc_start_ticks field), `include/nexigen/logging/detail/spam_protection.hpp`
  (field atomics), deletion of
  `include/nexigen/logging/detail/spsc_shard_ring.hpp` /
  `spsc_shard_pool.hpp`, `include/nexigen/core/shard_pool.hpp` (TLS cache
  width + overflow semantics), `src/metrics/provider.cpp` (harvest mutex),
  `include/nexigen/tracing/tracer_provider.hpp` (static_assert),
  `include/nexigen/chrono/chrono_provider.hpp` (start guard),
  `src/hub/http_server.cpp` + `tools/nexigen-hub/main.cpp` (IPv6).
- Tests: PID-recycling identity test (mismatched `proc_start_ticks` refuses to
  signal), ECHILD-status test (fault injection survives other suites), failed-
  start zombie assertion (no stray zombies in the test process), get_meter ×
  harvester concurrency test (TSan), >4-pools-per-thread drop accounting test,
  IPv6 listen tests (hub API + CLI), spam reconfiguration race test (TSan),
  chrono double-start test.
- Performance/latency/allocation: all producer/tracer/meter hot paths keep a
  single relaxed load per taken branch as today — spam quota fields swap
  `bool`/`uint64_t` plain reads for relaxed atomic loads of the same size
  (~0 ns delta, no fences); `claim_slot()` steady path is unchanged
  (identical TLS hit path, cache array 4→8 entries ≈ +64 bytes/thread,
  stack-resident); `stop`/`status` pay one extra `/proc` read per call on the
  cold path; `get_meter` takes the existing cold-path mutex on creation only;
  IPv6 adds no per-request cost; zero new hot-path allocations.
