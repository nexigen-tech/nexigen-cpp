# Design

## Context

All four `src/{chrono,logging,metrics,tracing}/affinity.cpp` units were read and diffed pairwise: byte-identical modulo namespace substitution, each exporting exactly `pin_self(uint64)` + `self_affinity(uint64&)` with private `mask_to_cpuset`/`cpuset_to_mask` helpers under `cpu_set_t` (64 representable cores; the docs in each header describe the same contract). Call sites stay in the modules and tests: `logging/detail/async_logger.hpp:103,109`, `tracing/tracer_provider.hpp:527,533`, `src/chrono/chrono_provider.cpp:93,100`, `src/metrics/provider.cpp:383`, `tests/logging/logging_test.cpp:1174–1192` — all resolve as `<module>::detail::pin_self`, so shim-redirect preserves them.

Build wiring facts that shape the design:
- Sources attach via `file(GLOB ... CONFIGURE_DEPENDS src/<module>/*.cpp)` (`CMakeLists.txt:141,167,190,215`) — deleting the four `.cpp` unlinks them from targets automatically.
- `nexigen_core` is an INTERFACE target with include dirs + `JSON_NOEXCEPTION=1` + `_POSIX_C_SOURCE=200809L` INTERFACE definitions; it does **not** link `Threads` today, and `_GNU_SOURCE` is added per-module PUBLIC (`CMakeLists.txt:154`, plus logging/metrics/tracing counterparts).
- `cmake/nexigenConfig.cmake.in` already ships `find_dependency(Threads)` — shared-thread linkage is export-safe.
- `find_package(Threads REQUIRED)` currently sits inside the chrono/logging sections (`CMakeLists.txt:139,165`), after the core target block.
- The runner carries a **different** contract: `apply_runner_affinity(span<const uint32_t>, context)` validates a CPU list (online check, deduplication) and applies via `sched_setaffinity(0, …)` (`src/runner/runner_logging.cpp:298–328`) — shape, API type, and semantics differ from the uint64-mask pair.
- See proposal.md — Why.

## Goals / Non-Goals

**Goals:**

- ONE inline, header-only implementation of the uint64-mask affinity pair under `nexigen::core::affinity`, contract-preserving down to the status mapping.
- Forward-only `detail/affinity.hpp` shims in the four modules so every existing call site and module suite compiles and behaves identically.
- Core target carries its own thread-linkage and `_GNU_SOURCE` visibility so core-only consumers (e.g. `nexigen_core_tests`) build and link without extra wiring.

**Non-Goals:**

- No unification of the runner's validated CPU-list surface into core (different contract; a separate decision if ever wanted).
- No behavior changes: statuses, masks-in/drop-out semantics, calling convention docs, and test outcomes stay as-is.
- No `core::detail` — the named `nexigen::core::affinity` namespace mirrors `settings_io`/`fixed_writer` ("never a second bare `detail::` candidate").
- No new concurrency primitives, no hot-path involvement (pinning happens at worker/provider start).

## Decisions

### D1 — Placement: `nexigen::core::affinity` (named sub-namespace, flat header)

`include/nexigen/core/affinity.hpp`, `namespace nexigen::core::affinity`. The per-module copy defenses guard against **sibling→sibling** dependencies ("chrono must not depend on logging/metrics/tracing") — core is the shared base dependency all four already hold. Placement precedent: `settings_io` and `fixed_writer` documented the same call and both avoid `nexigen::core::detail` because test TUs that `using namespace nexigen::core` must never learn a second bare `detail::` candidate name.

Alternatives: keep four copies (rejected — already documenting drift between them); sink into one module's detail and re-export (rejected — creates exactly the sibling dependency the comments forbid).

### D2 — Header-only inline; INTERFACE wiring for `_GNU_SOURCE` + Threads

The implementation lives entirely inline in the header (no `src/core/affinity.cpp`; core has no cargo TU today). `pthread_setaffinity_np`/`pthread_getaffinity_np` are glibc extensions behind `_GNU_SOURCE`:
- `nexigen_core` INTERFACE gains `$<$<PLATFORM_ID:Linux>:_GNU_SOURCE>` and `Threads::Threads` (hoist `find_package(Threads REQUIRED)` above the core block; the module-level `PUBLIC _GNU_SOURCE` lines stay — redundant `#define` is harmless and functionally identical).
- The header guards the glibc-extension requirement explicitly, mirroring the `JSON_NOEXCEPTION` build-wiring proof idiom:

```cpp
#if !defined(_GNU_SOURCE) && defined(__linux__)
#error "nexigen::core::affinity requires _GNU_SOURCE (pthread_*affinity_np are glibc extensions)"
#endif
```

This keeps an accidental core-only consumer's failure mode a clear message instead of a scary "undeclared identifier" from `<pthread.h>`.

Alternatives: documented-only requirement (rejected — silent miscompiles for new consumers); moving the four functions to a `src/core/` TU (rejected — forces a pthread link at cargo-TU granularity and diverges from the header-only core idiom).

### D3 — Shims: declaration headers become forward-only aliases

Each `include/nexigen/<module>/detail/affinity.hpp` keeps its doc comment (now pointing at core, retaining the "callers pin ON the thread to be pinned" contract note) and reduces to:

```cpp
#include <nexigen/core/affinity.hpp>
namespace nexigen::<module>::detail {
using nexigen::core::affinity::pin_self;
using nexigen::core::affinity::self_affinity;
} // namespace
```

`[[nodiscard]]`-annotated free functions re-exported by `using` keep their attribute application at call sites (the attribute lives on the core declarations). The four `src/<module>/affinity.cpp` are deleted — the CONFIGURE_DEPENDS globs drop them from targets at next configure.

Alternatives: full call-site renames to `core::affinity::…` (rejected for now — churn across async_logger/tracer_provider/provider sources for zero behavior gain; optional cleanup later), header-less downstream edits (rejected — breaks `detail::` qualifier resolution at every consumer).

### D4 — Contract parity (moved verbatim)

The moved bodies keep the current semantics exactly: `pin_self(0)` → `InvalidArgument` without touching the thread; non-zero masks convert to `cpu_set_t` over bits 0–63; pthread failures → `SystemError` (no errno classification exists today, none introduced); `self_affinity` reads via `pthread_getaffinity_np` and folds CPUs 0–63 into the mask — cores ≥ 64 (legal `CPU_SETSIZE`=1024 domain) are dropped by construction exactly as the four copies do today. No EINTR retry (affinity syscalls are non-blocking). No allocations (the original copies allocated nothing).

### D5 — Runner flavor stays in runner

`apply_runner_affinity(span, context)` (validate online CPUs + dedup + `sched_setaffinity(0,…)`) and `reset_runner_affinity` are a different API shape and semantic (list-based validation with user-facing context); merging them would change behavior or create an either/or contract. Recorded as the boundary in the spec's single-implementation requirement; revisit only if runner ever wants the mask surface.

### D6 — Tests

- New core suite `affinity_core` in `tests/core/core_test.cpp` (the arena there is finite/monotonic → the new probes are allocation-free already): zero-mask rejection; two-thread calling-thread-only independence (thread A pins core X, thread B pins core Y, both query back their own); round-trip pin/query; fresh-thread query returns a non-empty representable mask; absent-CPU probe (`1ULL << 63`) asserting `SystemError` — guarded to hosts with fewer than 64 online cores (assert skipped with a printed note otherwise, mirroring the latency-gate environment pattern).
- Module suites (`logging_test` affinity suite incl. zero-mask/pin probes, plus the pin-flow suites in tracing/chrono/metrics) must pass **unchanged** through the shims.
- Verification matrix per project rules: Debug, Release (`-O3 -march=native -flto`), TSan, ASan, clang-tidy profile, coverage gate (current baseline 95.2%).

## Concurrency / memory layout

No new atomics, mutexes, or shared mutable state: the two functions operate per-calling-thread via the pthread affinity API (per-thread scheduler state). No `alignas` decisions (the rules-required memory-layout/shard/memory-order fields: **not applicable** — explicitly no concurrent component is added). TSan relevance is the two-thread independence probe.

## Migration order (implementation sequence)

1. Core header + CMake INTERFACE wiring + core tests (additive; nothing depends yet)
2. Four shim headers (call sites unchanged)
3. Delete the four `src/<module>/affinity.cpp` (globs unlink)
4. Rebuild + run module suites (affinity suites unchanged)
5. Full verification matrix
6. Wrap-up: TODO P3-32 → done with rationale; README core building-blocks bullet; grep audit (one implementation, four forwarders, runner untouched)

Each step keeps the tree green; step-3 is the only removal step.

## Risks / Trade-offs

- **Absent-CPU probe host dependence**: `1ULL << 63` is ungrantable on hosts with <64 online cores (the usual case in CI/containers); on ≥64-core hosts the probe degrades to a printed note + skip — the zero-mask and independence scenarios carry the assertion load there.
- **Interface risk — Threads on non-Linux**: `Threads::Threads` is found-unconditionally; links are no-ops where not built for. The project targets Linux (pthread `_np` calls are the contract); the header `#error` guard makes a non-Linux misuse loud instead of implicit.
- **Shim `using`-visibility**: using-declarations inside `nexigen::<module>::detail` re-export the names for all unqualified `detail::pin_self` call sites — verified pattern (`fixed_writer` shims did the same for `json_writer.hpp` aliases). The verification step compiles all four module targets plus tests before any deletion of `.cpp` files lands, so the shim substitution is proven before the removal step.
- **Trade-off — two nibbles possible**: one could rename all call sites to `core::affinity::…` and drop the shims entirely (purest, −40 more lines); rejected in this change to keep the diff reviewable and the module APIs stable while the pattern settles; the shims are trivially removable later.
