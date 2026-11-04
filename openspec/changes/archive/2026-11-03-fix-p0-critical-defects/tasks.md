# Tasks

## 1. Logging: flush barrier crosses no ring ownership (P0-1)

- [x] 1.1 In `include/nexigen/logging/detail/producer.hpp`: make `bump()` a single
  `counter.fetch_add(1, std::memory_order_relaxed)`; update the
  `ShardPoolState` counter contract comment in `include/nexigen/core/shard_pool.hpp`
  (lines 33–42) from "owner stores prev+1" to "owner commits via atomic
  fetch_add; the consumer may reset cells it has just reported". Verify: unit
  build compiles under `-Werror` on both compilers.
- [x] 1.2 Replace `Producer::flush_drop_summaries()` with a worker-owned
  summary helper that reads the per-shard rate_limited/buffer_full/throttled
  cells plus the global slot_exhausted counter relaxed, builds the identical
  summary `Record`, and resets exactly the reported cells after synthesis;
  rework the old public API and its docs in `producer.hpp:240–256, 271–278`.
  Verify: no ring `try_push` reachable from a non-owner thread (grep review);
  compiles under `-Werror`.
- [x] 1.3 In `include/nexigen/logging/detail/async_logger.hpp`: remove the
  `producer_->flush_drop_summaries()` call from `flush()`
  (`async_logger.hpp:197–199`); in `run()`'s flush-request branch
  (`async_logger.hpp:134–137`) call the producer's new
  `synthesize_flush_summary()` and hand the produced record (if any) to
  `sink.handle()` before `publish_sync(req)`. Verify: manual flush with drops
  pending on a cross-thread shard produces the summary in the sink and
  contents match the spec text.
- [x] 1.4 Rework `tests/logging/logging_test.cpp`: the three flush-time blocks
  of `test_drop_visibility()` (lines ~2277–2374) move from a bare
  `flush_drop_summaries()` contract to the worker-synthesized contract (start
  the `AsyncWorker` with an in-memory sink), plus a NEW multithreaded
  flush-vs-owner race test (owner thread floods its slot with drops while the
  main thread flushes in a loop, 4+ threads). Verify: TSan build
  `-DNEXIGEN_ENABLE_TSAN=ON` runs `tests/logging/logging_test` clean; all
  other suites unchanged and green.
- [x] 1.5 Update the "Flush-time drop summary is emitted" comment to reference
  the worker-synthesized contract (producer.hpp:240–242, async_logger.hpp:197).
  Verify: no stale references to the removed public API in headers/tests.

## 2. Runner: exec-correct handshake (P0-2)

- [x] 2.1 In `src/runner/runner.cpp` child block (lines 605–616): delete the
  pre-exec `setup_status = 0` beacon write; add `fail_setup(4)` semantics after
  `execvp` failure (write the exec failure code before `child_exit(127)`).
  Verify: code review of the child path lists every pre-exec step with a
  nonzero status write.
- [x] 2.2 In the supervisor block (lines 619–645): reinterpret the handshake —
  `read_all()` true (4-byte code) or a nonzero code = setup/exec failure
  (existing failure leg unchanged: kill group, reap, `StartReport{0,
  SystemError}`, no `resource.json`), EOF = success. Verify: existing runner
  tests still pass (successful start flow unchanged).
- [x] 2.3 Add CLI-visible-start tests in `tests/runner/`: start a nonexistent
  binary (and one not executable) through the real CLI path
  (`tests/runner/cli_test.sh` or `runner_test.cpp` at the API level) and assert
  1) nonzero exit with `start failed` on stderr, 2) no `resource.json` in the
  instance dir, 3) no `exit.json`. Verify: test fails before the fix applied,
  passes after.
- [x] 2.4 Re-verify a successful start of a real binary end-to-end (existing
  smoke: handshake EOF → resource.json published → CLI prints `started:…`
  stdout). Verify: `tests/runner/runner_test.cpp` suite green including the
  exec-success scenario; no regression in `app_suite_test.cpp`.

## 3. Hub: path containment in discovery (P0-3)

- [x] 3.1 In `src/hub/discovery.cpp`: add the
  `contained(const RunRoot&, std::string_view)`-style helper (reuse
  `canonical_path()`; strict slash-boundary prefix check) and apply it in
  `scan_suite()` to `metadata.artifacts_path` and each app
  `instance_dir` (lines 375–499) before storing into the tree; skip
  `scan_attempts()` bases and `out.runner_log` derivations that fail
  containment. Verify: unit test with escaped paths shows no stored path
  outside root (inspect the JSON tree output).
- [x] 3.2 Update `tests/hub/hub_test.cpp` Fixture: build `artifacts` inside
  `root` (runner-realistic layout); add escape cases — absolute external
  `artifacts_path` (/etc), `../` escape from root, escaped `instance_dir` —
  asserting suite visible with 0 streams/no reads outside root/instance state
  unknown, and the missing-root tolerance scenario stays green. Verify:
  `tests/hub/hub_test` green; hub coverage gate ≥ 90% holds.
- [x] 3.3 Confirm no other hub module reads suite-supplied paths directly
  (streaming opens only tree paths). Verified: the only `::open` is
  `streaming.cpp:101` (`tailer.descriptor.path`, tree-stored);
  `discovery.cpp:174/184` read `resource.json`/`exit.json` from the guarded
  `instance_dir`; `discovery.cpp:399/434/441` read `suite.json` /
  `suite-state.json` / `suite-exit.json` under the enumerated `suite_dir`
  (directory listing, not suite.json content); `http_server.cpp` opens no
  files (socket-only). No constructed-path opens outside the tree remain.

## 4. Full verification matrix

- [x] 4.1 Configure+build Debug and Release (`-O3 -march=native -flto`) with
  `-DNEXIGEN_TESTS=ON`, run 100% of unit tests on both. Verified: Debug
  (GCC) 35/35 and Release (`-O3 -march=native`, LTO property unchanged —
  see note) 35/35 tests green; strict-latency Release run
  (`NEXIGEN_STRICT_LATENCY_TESTS=ON`) reports accepted median
  **10.01 ns ≤ 20 ns gate** (7 checks, 961 total, 0 failed) and the chrono
  suite 1407 checks green; target suites additionally build clean with
  Clang 21 under `-Werror`. Note: `-flto` is not actually in effect in any
  build — the existing property lands on the INTERFACE `nexigen_options`
  target (TODO P2-16, tracked separately; CI runs the same no-op).
- [x] 4.2 ThreadSanitizer build `-DNEXIGEN_ENABLE_TSAN=ON`: run
  `tests/logging/logging_test` (multithreaded flush test included) +
  `tests/core` (ShardPool). Verified: 0 TSan race reports in both. The
  first TSan run flagged a genuine race in the new worker path —
  `stamps_[i].sample()` from the worker raced the owner's `StampSource`
  `last_ns_` clamp (the removed `flush_drop_summaries()` carried the same
  hidden race) — fixed by reading the summary stamp worker-side via
  `Timestamp::now()` (`worker_stamp()`, design D1 updated); re-run clean.
- [x] 4.3 AddressSanitizer build `-DNEXIGEN_ENABLE_ASAN=ON`: run the full test
  suite. Verified: 15/15 suites green, no leak/UAF reports, including the
  exec-failure start test.
- [x] 4.4 Run `openspec validate --strict --changes` for this change. Verified:
  clean validation (re-run after the design.md stamp update).

## Workflow follow-up

- After implementation is reviewed, mark the P0-1/P0-2/P0-3 items done in
  TODO.md and proceed to P1 items in a separate change (P1-4's remaining half
  — spam-config atomicity — may pair with P1-9).
- Archiving this change should sync the two modified requirements into
  `openspec/specs/logging/multi-producer`, `openspec/specs/hub/discovery`, and
  the added requirement into `openspec/specs/runner/process-supervision`.
