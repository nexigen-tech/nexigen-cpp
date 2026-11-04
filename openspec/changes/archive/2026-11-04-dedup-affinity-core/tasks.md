# Tasks

## 1. `core::affinity` — the one implementation (additive)

- [x] 1.1 Create `include/nexigen/core/affinity.hpp` (`namespace nexigen::core::affinity`): inline `pin_self(std::uint64_t core_mask)` + `self_affinity(std::uint64_t& mask_out)` with the private conversion helpers moved verbatim from the module copies (`mask_to_cpuset` over bits 0–63, `cpuset_to_mask` folding CPUs 0–63), zero-mask → `InvalidArgument`, pthread failures → `SystemError`, both `noexcept`/allocation-free. Header comment mirrors the `settings_io`/`fixed_writer` DETAIL DISCLAIMER (named namespace, not `core::detail`) and keeps the "callers run these ON THE THREAD TO BE PINNED (worker pins itself as its first act)" contract note. `#if !defined(_GNU_SOURCE) && defined(__linux__)` → `#error` hint (design D2).
- [x] 1.2 CMake core wiring: hoist `find_package(Threads REQUIRED)` above the `nexigen_core` target; add `Threads::Threads` and `$<$<PLATFORM_ID:Linux>:_GNU_SOURCE>` to `nexigen_core` INTERFACE (`target_link_libraries(nexigen_core INTERFACE …)` + a `target_compile_definitions` line next to the existing `_POSIX_C_SOURCE=200809L` one). Keep the module-level `PUBLIC _GNU_SOURCE` lines untouched (redundant, harmless). Confirm `cmake/nexigenConfig.cmake.in` needs no change (it already has `find_dependency(Threads)`).
- [x] 1.3 Extend `tests/core/core_test.cpp` with an `affinity_core` suite: round-trip pin/query (pin a single online core, query back equality), zero-mask rejection (`InvalidArgument`, affinity untouched), two-thread calling-thread-only independence (thread A pin X, thread B pin Y, both query their own — the TSan-relevant probe), fresh-thread unpinned query yields a non-empty representable mask, guarded absent-CPU probe (`1ULL << 63` → `SystemError` on hosts with <64 online cores; printed-note skip otherwise, design D6). No heap allocations in the suite (finite arena constraint).
- [x] 1.4 Compile + run the core suite (Debug); reconfigure only if the INTERFACE definitions require it.

## 2. Module shims (call sites unchanged)

- [x] 2.1 Rewrite `include/nexigen/chrono/detail/affinity.hpp` as a forward-only shim: updated doc comment (redirect to `nexigen::core::affinity`, keeps the pinning-convention note), `#include <nexigen/core/affinity.hpp>`, `using nexigen::core::affinity::pin_self;` + `using nexigen::core::affinity::self_affinity;` inside `nexigen::chrono::detail`.
- [x] 2.2 Same shim for `include/nexigen/logging/detail/affinity.hpp`.
- [x] 2.3 Same shim for `include/nexigen/metrics/detail/affinity.hpp`.
- [x] 2.4 Same shim for `include/nexigen/tracing/detail/affinity.hpp`.
- [x] 2.5 Rebuild all four module targets (headers-only diff so far) and run `logging_test`, `tracing_test`, `chrono_test`, `metrics_test` — the affinity suites (`self_affinity`/zero-mask/pin probes) must pass unchanged.

## 3. Remove the four copy units

- [x] 3.1 Delete `src/chrono/affinity.cpp`; verify the `CONFIGURE_DEPENDS` glob drops it at reconfigure (build succeeds).
- [x] 3.2 Delete `src/logging/affinity.cpp`, `src/metrics/affinity.cpp`, `src/tracing/affinity.cpp` the same way; rebuild all module targets.
- [x] 3.3 Run the four module suites + `nexigen_core_tests` (Debug) after the removal.

## 4. Verification matrix (project rules)

- [x] 4.1 Debug: full `ctest --test-dir build --output-on-failure` — 100% pass.
- [x] 4.2 Release: configure/build `build-release` (`-O3 -march=native -flto`); full pass. Latency gates: environment-bounded container setting stays as-is (same documented caveat as the archived dedup-sink-plumbing run).
- [x] 4.3 ThreadSanitizer: build + full `ctest` pass on `build-tsan`.
- [x] 4.4 AddressSanitizer: build + full `ctest` pass on `build-asan`.
- [x] 4.5 clang-tidy (project profile) clean on the new core header's consumers and the rewritten shims; no "second bare `detail::`" ambiguity in TUs that `using namespace nexigen::core` (module tests already compile under it).
- [x] 4.6 Coverage: `coverage` target; touched-file coverage must not drop below the 95.2% baseline.

## 5. Wrap-up

- [x] 5.1 Grep audit: `nexigen::core::affinity` is the only `cpu_set_t` conversion for the mask surface; four shim headers forward-only; `src/{chrono,logging,metrics,tracing}/affinity.cpp` gone; `runner::apply_runner_affinity`/`reset_runner_affinity` untouched (`src/runner/runner_logging.cpp` unchanged).
- [x] 5.2 `README.md`: add the `<nexigen/core/affinity.hpp>` bullet to the core building-block list (same shape as the `sink_io.hpp` bullet: cold-path contract, calling-thread semantics).
- [x] 5.3 `TODO.md`: flip P3-32 to done with the decision rationale (the isolation comment guarded sibling deps, not core deps; fixed_writer/settings_io precedent; runner flavor intentionally stays) — mirror the wording of the finished P3-2x entries.
