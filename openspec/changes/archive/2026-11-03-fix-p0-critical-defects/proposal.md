# Proposal

## Why

The 2026-11-03 code analysis found three defects that break hard contracts of the
system: the logging `flush()` barrier pushes into SPSC rings owned by other
threads (data race / ring corruption), the runner publishes `resource.json` and
reports "started" even when `execvp` never happened (a failed start is invisible),
and hub discovery follows `artifacts_path` from a runner-writable `suite.json`
outside the run root, letting a compromised application point the read-only hub
at arbitrary files. All are P0 in [TODO.md](../../../TODO.md), items P0-1..P0-3.

## What Changes

- **logging/** — `AsyncWorker::flush()` no longer calls
  `Producer::flush_drop_summaries()` on the flushing (foreign) thread. Pending
  drop summaries are synthesized by the background worker when it answers the
  flush barrier, delivered directly to the sink, and never written into a ring
  shard they do not own. `bump()` becomes `fetch_add` so counter increments stay
  correct while the worker snapshots+resets them. Record text format, retry
  semantics (counters preserved until reported) and the `dropped()` snapshot API
  are unchanged.
- **runner/** — the child→supervisor setup handshake becomes exec-correct: the
  child writes a nonzero setup status only on failure (setsid, dup2, affinity,
  env, exec); on a successful `execvp` the `O_CLOEXEC` pipe closes and EOF means
  success. A failed `exec` now reaches the supervisor immediately, the start
  returns `SystemError` before `resource.json` is written, and the `nexigen-run`
  CLI reports "start failed" on stderr with a nonzero exit code instead of
  publishing a dead PID as started.
- **hub/** — discovery accepts filesystem paths from `suite.json` only when the
  real path resolves inside the canonical run root (`artifacts_path`,
  `instance_dir` and all derived stream/attempt paths). Non-contained paths make
  the suite/instance degrade gracefully (visible, unknown state, no streams, no
  external reads, no external inotify watches); nothing else in the hub changes.

## Capabilities

### New Capabilities

(none — all three fixes stay inside existing capability boundaries)

### Modified Capabilities

- `logging/multi-producer`: drop accounting becomes an atomic
  owner-bump / consumer-reset story; a new flush-time summary clause states that
  summaries are consumer-synthesized and never pushed into foreign shards.
- `runner/process-supervision`: adds the requirement that a failed exec fails
  the start visibly (nonzero exit, stderr diagnostic, no `resource.json`), which
  re-times the setup handshake to after a successful `execvp`.
- `hub/discovery`: replaces the "external artifact root is followed" guarantee
  with a contained-paths-only guarantee under the canonical run root.

## Impact

- Code: `include/nexigen/logging/detail/producer.hpp`,
  `include/nexigen/logging/detail/async_logger.hpp`
  (flush path + counters); `src/runner/runner.cpp` (child setup block + parent
  handshake read); `src/hub/discovery.cpp` (path sanitization at tree build),
  `include/nexigen/core/shard_pool.hpp` (comment contract of the two counters).
- Tests: multithreaded flush test (flush thread ≠ shard owner through a real
  worker), exec-failure start test, path-escape discovery tests, hub fixture
  moves `artifacts` inside the run root. TSan validation is mandatory (the two
  concurrency fixes).
- Performance/latency/allocation: hot paths unaffected — the producer hot path
  keeps `try_push` + relaxed atomics untouched except `bump()` which becomes one
  `fetch_add` on the shard's own cache line (same cost class as the current
  `store(load+1)`, one memory RMW instead of read-then-write). The worker gains
  a bounded O(active-shards) counter scan per flush answer only; no allocations,
  no locks. Runner handshake protocol: same pipe, same 4-byte writes; EOF now
  means success instead of a pre-exec `0`. Hub discovery: one realpath per
  external path on a cold path only.
