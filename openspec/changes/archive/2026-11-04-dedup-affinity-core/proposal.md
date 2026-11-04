# Proposal

## Why

Four module units carry byte-identical implementations of the same two-call affinity surface — `src/{chrono,logging,metrics,tracing}/affinity.cpp` each define `pin_self(std::uint64_t core_mask)` and `self_affinity(std::uint64_t&)` declared in their `<module>::detail/affinity.hpp` (4 × ~59 lines of .cpp + 4 × ~30 lines of headers ≈ 355 lines total; the files resolved identical modulo namespace substitution). The per-module `detail/affinity.hpp` comments document the copy as isolation ("chrono must not depend on logging/metrics/tracing") — but that rationale forbids **sibling→sibling** dependencies, not a dependency on the shared base every module already links (`core/result.hpp`, `core/settings_io.hpp`, `core/sink_io.hpp`). The project already established the collapse pattern twice for exactly this shape of copy group: `core::fixed_writer.hpp` ("one implementation instead of three near-identical copies") and `core::settings_io.hpp` ("three near-identical copy groups collapse into one") — the affinity group is the missing member of that wave.

The copies are already drifting at the documentation level (each header commentary claims to mirror its own sibling, include ordering differs between copies), so the divergence risk grows with every edit.

## What Changes

- Add `include/nexigen/core/affinity.hpp` (`namespace nexigen::core::affinity`): the ONE header-only, inline implementation of the uint64-core-mask affinity surface — `pin_self(core_mask)` (zero mask → `InvalidArgument`; calling-thread-only semantics; glibc `pthread_setaffinity_np`; failure → `SystemError`) and `self_affinity(mask_out)` (cores beyond bit 63 dropped by contract; failure → `SystemError`), both `noexcept`, allocation-free, cold-path.
- Wire the build: hoist `find_package(Threads REQUIRED)` above `nexigen_core`; attach `Threads::Threads` and the Linux `_GNU_SOURCE` compile definition to the `nexigen_core` INTERFACE (the module-level `PUBLIC _GNU_SOURCE` definitions stay — redundant but harmless; `cmake/nexigenConfig.cmake.in` already performs `find_dependency(Threads)` for installed consumers).
- Convert the four `<module>/detail/affinity.hpp` declarations into forward-only shims (include core + `using` declarations, mirroring the `fixed_writer` shim precedent); delete the four `src/{chrono,logging,metrics,tracing}/affinity.cpp` (CMake `file(GLOB ... CONFIGURE_DEPENDS)` picks the removal automatically — no source-list edits).
- Keep all call sites untouched (`logging/detail/async_logger.hpp:103,109`, `tracing/tracer_provider.hpp:527,533`, `chrono_provider.cpp:93,100`, `metrics/provider.cpp:383`, and the `tests/logging` affinity suite keep calling `<module>::detail::pin_self/self_affinity`).
- Out of scope, recorded as a follow-up decision: `runner::apply_runner_affinity(span<uint32>, context)` / `reset_runner_affinity` (`src/runner/runner_logging.cpp:298–328`) is a different contract (validated CPU list, `sched_setaffinity(0, …)`), not a copy — it stays in runner.

## Capabilities

### New Capabilities
- `core/affinity`: the single, exception-free thread-affinity contract for the observability modules — uint64 core-mask pinning of the **calling thread**, affinity query with the 64-bit representability contract, error mapping (`InvalidArgument`/`SystemError`), and the single-implementation/shim boundary (`nexigen::core::affinity` is the one implementation; module `detail/affinity` headers forward only). Named sub-namespace, `fixed_writer`/`settings_io` placement precedent, no `core::detail` (the "second bare `detail::`" hazard).

### Modified Capabilities
<!-- none — module behavior (their affinity suites, worker pinning flows) is
     unchanged; the change only re-homes the implementation into core and
     turns the four module headers into forward-only shims. Runner's
     apply/reset surface is explicitly untouched.
-->

## Impact

- **Code**: `include/nexigen/core/affinity.hpp` (new, ~95 loc), 4 shim headers (~10 loc each), 4 deleted `src/*/affinity.cpp` (~236 loc), CMake core-target block (+2 lines, `find_package` hoisted). Net ≈ −230 lines, 4→1 implementations.
- **API**: no breaking changes. `<module>::detail::pin_self/self_affinity` keep their signatures and semantics through the shims; `nexigen::core::affinity::pin_self/self_affinity` become the canonical names. Runner's `apply_runner_affinity`/`reset_runner_affinity` untouched.
- **Performance / latency / allocation impact**: pinning is once-per-worker/provider-start cold-path work (never in producer loops); both functions are `noexcept`, allocation-free, syscall-only (`pthread_setaffinity_np`/`pthread_getaffinity_np`); the trace/metrics/chrono snapshot hot paths never call them. Link profile: `Threads::Threads` moves to `nexigen_core` INTERFACE — resolved the same way today for module targets; the installed package already ships `find_dependency(Threads)`.
- **Tests**: existing module affinity suites must pass unchanged (they exercise the shims); new core suite covers round-trip pin/query, zero-mask rejection, two-thread calling-thread-only independence, absent-CPU probe → `SystemError`; full Debug / Release / TSan / ASan / clang-tidy / coverage matrix per project rules.
- **Docs**: `README.md` core building-block list gains the `core/affinity.hpp` bullet (same shape as `sink_io.hpp`); `TODO.md` P3-32 flips to done with the decision rationale (the isolation comment guarded sibling deps, not core deps).
