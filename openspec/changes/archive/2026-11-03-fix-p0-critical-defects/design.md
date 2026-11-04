# Design

## Context

Three P0 defects (see proposal.md; locations verified against HEAD `f00e43f`):

1. `include/nexigen/logging/detail/producer.hpp:184–256` —
   `AsyncWorker::flush()` (`async_logger.hpp:199`) calls
   `Producer::flush_drop_summaries()` *on the flushing thread*, which does
   `pool_.ring(slot).try_push(...)` into shards owned by other threads. The SPSC
   invariant ("the slot's ring is `try_push`-ed only by its owner; everyone else
   only `pop`s") is broken the first time any pending drop exists on a foreign
   shard while its owner emits concurrently.
2. `src/runner/runner.cpp:605–616` — the application child writes the success
   beacon `setup_status = 0` into the handshake pipe **before** `execvp`; on a
   failed exec the supervisor has already validated the handshake, writes
   `resource.json` with a `started` PID, and the user sees nothing because
   stdout/stderr are redirected into files by then.
3. `src/hub/discovery.cpp:375–499` — `scan_suite()` trusts `artifacts_path` and
   `instance_dir` from `suite.json` (written by the runner on behalf of the
   supervised app) and builds every read target from them; the streaming layer
   later opens `tailer.descriptor.path` straight from the tree
   (`streaming.cpp:101`). Nothing constrains these paths to the canonical run
   root.

Constraints: C++23 `-fno-exceptions -fno-rtti`, `-Werror` on GCC+Clang; zero
allocations on logging hot paths; lock-free/wait-free concurrency with explicit
memory orders; `Result`/`Status` error model (never `.value()`); every hot-path
cell own a cache line against false sharing; TSan/ASan builds are CI gates and
TSC latency gates are ≤ 20 ns.

## Goals / Non-Goals

**Goals:**

- Restore the single-producer-per-shard invariant across flush without
  weakening the observable drop-visibility contract (exact summary text, one
  summary per flush, counters preserved until reported).
- Make a failed `execvp` a visible, immediate start failure with no
  `resource.json` published.
- Constrain all suite-metadata-derived read targets to the canonical run root
  at tree-construction time.
- Multithreaded tests for each restored contract (flush race via TSan, exec
  failure, path escape).

**Non-Goals:**

- P1 items: PID-recycling identity checks, ECHILD handling, TLS>4 shard pools,
  spam-config atomicity, IPv6 hub support (tracked separately in TODO.md).
- Redesigning `SpscShardRing`/seq counters (P1-7).
- Changing the summary text or `DroppedStats` serialization.

## Decisions

### D1 — Drop summaries are synthesized by the worker, never pushed into foreign rings

`AsyncWorker::flush()` drops the `producer_->flush_drop_summaries()` call. The
worker answers flush barriers already (`run()` at `async_logger.hpp:134–137`);
in that branch it now first synthesizes the summary:

- Worker scans `pool_.state(i)` cells plus `spam_state_[i].throttled_count`
  and the global `slot_exhausted` cell with relaxed loads, bounded by
  `pool_.active()`; identical trigger rule as today (`(rl|bf|th|se) != 0`,
  first match wins, one summary per flush).
- The summary record is built locally (same `Record` layout, same
  "Dropped N record(s) …" text via the same snprintf body, worker-side
  `thread_id = cached_thread_id()`, `stamp` read worker-side via
  `Timestamp::now()` — same `CLOCK_MONOTONIC_RAW` domain) and handed to
  `sink.handle(rec)` directly — the sink's single writer is the worker, so no
  new synchronization domain is created. The worker must not call
  `stamps_[i].sample()`: each `StampSource` is producer-owned single-writer
  state, and a foreign `sample()` races the owner's `last_ns_` monotonic
  clamp (caught by the new TSan flush race test; the removed
  `flush_drop_summaries()` had the same hidden race).
- After synthesis the worker resets exactly the reported cells
  (`relaxed store 0`), keeping the existing "reset only what was reported"
  retry-safety story: counters persist across flushes until they surface in a
  summary.
- `Producer::bump()` becomes `fetch_add(1, relaxed)` on the shard's own cell
  (TODO P1-4 is subsumed for the two cells the flush fix depends on). Without
  it, worker resets racing owner bumps could lose updates silently.

Producer's own summary emission (`emit_summary_if_needed` at window
transitions/pre-push on its **own** slot) is untouched. Allocation count: zero;
the worker path adds an O(active) relaxed scan per flush answer only. Memory
layouts unchanged: `ShardPoolState` stays `alignas(64)`; no new shared cells.

Alternatives considered:

- *Owner-only gate*: flush synthesizes nothing; owners emit on their next
  window transition. Rejected — regresses tested behavior (soak test expects a
  summary per flush; a stalled owner thread would hide drops indefinitely).
- *Deferred-summary slot in each shard*: flusher parks counts in shard state
  for the owner to flush. Same foreign-state write class that P1-4 wants to
  avoid; more state, no simpler contract.

### D2 — Runner handshake: EOF means exec succeeded

`run_instance_supervisor()` child path changes (`runner.cpp:605–616`):

- Remove the pre-exec `setup_status = 0` beacon. The child writes a **4-byte
  int32 status** only on failure: 1 setsid, 2 affinity, 3 setenv, 4 execvp
  (`errno` context goes to the supervisor via the same write as today).
- Supervisor side: `read_all()` returning **true** means a 4-byte code was
  delivered → failure (`setup_status != 0` guard kept as protocol belt; codes
  1 = setsid, 2 = affinity, 3 = setenv, 4 = execvp). `read_all()` returning
  **false** means EOF — the pipe closed because `execvp` consumed the
  child-side `O_CLOEXEC` fd — and is the success signal. No fd-flag changes:
  `pipe2(..., O_CLOEXEC)` is already in place (`runner.cpp:519–520`).
- Failure leg is unchanged otherwise: `kill(-child, SIGKILL)`, reap, report
  `StartReport{0, SystemError}`, exit 1. The CLI already prints
  `nexigen-run: start failed: system-error` to stderr and exits nonzero
  (`tools/nexigen-run/main.cpp:329–336`). The ordering guarantee
  "resource.json written only after handshake success" is preserved because
  `write_resource_json()` sits after the handshake read
  (`runner.cpp:647–660`).
- Remainder-risk accepted: a child SIGKILLed between `fork` and `execvp` also
  produces EOF and is reported as success (resource.json for a dead PID). The
  window is two libc calls wide; management of it belongs to P1-5/P1-6
  identity/ECHILD work.

Alternatives considered:

- *Write the beacon after a successful `exec` from a wrapper*: impossible —
  the process image is replaced.
- *Second process observing exec*: adds a process/IPC per start for a defect
  that only changes failure latency; rejected as over-engineering.

### D3 — Hub containment enforced inside Discovery at tree-build time

`scan_suite()`/`scan_attempts()` gain a containment predicate before any path
is stored into the tree:

- Helper `contained(const std::string& root, std::string_view path)`:
  `realpath(path)` into a `PATH_MAX` buffer (reusing the existing
  `canonical_path()` helper), then require strict `root + "/"` prefix
  equality (`text.size() >= prefix.size() &&
  text.compare(0, prefix.size(), prefix) == 0` — the slash-boundary prevents
  `/run-1` matching `/run-10`). The run root itself is already canonicalized
  at `initialize()` (`discovery.cpp:220–231`).
- `out.artifacts_path` is stored only when contained; `metadata.instance_dir`
  likewise (`scan_suite()` per app). `scan_attempts()` skips a base directory
  that fails containment; `out.runner_log` stays empty unless its path is
  contained through derivation from the already-contained artifacts_path.
- Contained-but-missing paths keep today's tolerance (no streams, suite
  visible), matching the retained spec scenario "Missing artifact root is
  tolerated".
- `rebuild_watches()` needs no new checks: it only watches tree-stored paths,
  which are contained by construction. External targets therefore produce no
  inotify watches and no reads — the streaming layer inherits safety because
  it only opens tree paths.

Alternatives considered:

- *Containment re-check inside the streaming layer*: defense in depth, but the
  single source of truth for read targets is the tree; duplicating at every
  consumer invites drift. The requirement text names the tree contract; the
  streaming layer is unchanged.
- *Symlink-open policy (O_NOFOLLOW)*: handles a different attack (post-rescan
  tree path swap) and deserves its own follow-up; tree-build realpath is the
  P0 containment boundary agreed in the analysis.

## Risks / Trade-offs

- [Flush summary now depends on a live worker] → A flush before the worker
  starts already returns `NotInitialized`; the summary follows the same
  lifecycle, and the never-started logger still reports drops via `dropped()`.
- [Worker reset vs owner bump] → `fetch_add` makes each increment atomic; a
  single drop crossing the reset boundary can land after a `store(0)`. This is
  diagnostic-counter noise (counters are documented as snapshots, not
  linearizations); no invariant corrupts.
- [EOF-as-success cannot distinguish pre-exec SIGKILL] → Documented
  remainder-risk, two-syscall window, addressed by P1-5/P1-6 identity work.
- [realpath per external path in discovery] → One syscall batch per
  `suite.json` on a cold path; measurements are over the HTTP path, not
  rescan paths. Symlink resolution cost is bounded by `PATH_MAX`.
- [Hub test fixtures move artifacts under the run root] → The new containment
  contract makes the current out-of-root fixture layout illegal, so hub tests
  change in the same change; the hub 90% coverage gate is only satisfied with
  the updated fixtures compiled in.

## Migration Plan

Library consumers see no API change (`Logger`, `Runner`, `Discovery` signatures
untouched; the only removals are header-internal detail APIs plus their tests).
Behavior changes are failure-path corrections that earlier callers could not
have relied on. Rollback = revert the change; no data migration, no on-disk
format change (resource.json/exit.json/suite.json/stream files unchanged).

## Open Questions

(none — all three designs verified against current sources)
