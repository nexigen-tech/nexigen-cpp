# nexigen Architecture

How the `nexigen` ecosystem is put together and the invariants every module and
contributor must keep. For per-module usage, start with
[`metrics-guide.md`](metrics-guide.md); the authoritative behaviour contract lives
in the OpenSpec capabilities under [`openspec/specs/`](../openspec/specs).

## Module map

| Namespace | Directory | Build target | Kind |
|---|---|---|---|
| `nexigen::core` | `include/nexigen/core/`, `tests/core/` | `nexigen_core` (INTERFACE) | header-only |
| `nexigen::chrono` | `include/nexigen/chrono/`, `src/chrono/` | `nexigen_chrono` | STATIC |
| `nexigen::logging` | `include/nexigen/logging/`, `src/logging/` | `nexigen_logging` | STATIC |
| `nexigen::metrics` | `include/nexigen/metrics/`, `src/metrics/` | `nexigen_metrics` (alias `nexigen::metrics`) | STATIC |
| `nexigen::tracing` | `include/nexigen/tracing/`, `src/tracing/` | `nexigen_tracing` (alias `nexigen::tracing`) | STATIC |
| `nexigen::diagnostics` | `include/nexigen/diagnostics/`, `src/diagnostics/` | `nexigen_diagnostics` (alias `nexigen::diagnostics`) | STATIC |
| `nexigen::app` | `include/nexigen/app/`, `src/app/` | `nexigen_app` (alias `nexigen::app`) | STATIC |
| `nexigen::runner` | `include/nexigen/runner/`, `src/runner/` | `nexigen_runner` (alias `nexigen::runner`) | STATIC |
| `nexigen::hub` | `include/nexigen/hub/`, `src/hub/` | `nexigen_hub` (alias `nexigen::hub`) | STATIC |
| examples | `examples/<module>/` | `NEXIGEN_BUILD_EXAMPLES` option | executables |
| tests | `tests/<module>/` | `NEXIGEN_BUILD_TESTS` option | CTest suites |

`nexigen::core` is INTERFACE because it is pure templates; `chrono`, `logging`,
`metrics`, `tracing`, `diagnostics`, `app`, `runner`, and `hub` are STATIC because
they own compiled cold paths (calibration, the harvest loop, sink I/O, snapshot
serialisation, process supervision, HTTP/SSE serving) that must not be inlined into
every consumer. Modules never consume another module's `detail/` — the chrono extraction
established that rule (a module needing shared machinery promotes it or
duplicates it, keeping dependency arrows one-way: `metrics → chrono/core`,
`logging → chrono/core`).

`nexigen::logging` also owns the cold-path deployment surface:
`logging_settings.hpp` reads an explicitly supplied global JSON file, then a local
JSON file, then `NEXIGEN_LOG_*` environment overrides, merging field by field into
`LoggerOptions` with best-effort diagnostics. `FileLogger` adds optional
size-based segments (`app.jsonl` → `app.1.jsonl` → …) on top of the existing
period-directory rotation. Both are initialization-time features: producers never
observe the loader, and the writer performs a segment switch only at an already
cold rotation point.

`nexigen::app` is the application layer above them: `Infrastructure<Policy, ...>` is a
heap-only service owned by the application (not a base class). It owns the logger,
metric/tracing sinks and providers in a lifetime-safe order, loads per-subsystem
settings pairs, claims process-global signal ownership, blocks `SIGINT`/`SIGTERM`
before any worker starts, runs a `signalfd` watcher, and shuts down through one flush
path that logs the stop reason. A required worker-thread attribute failure follows
the same reverse-order rollback as existing initialization failures; explicit
best-effort partial application remains non-fatal and visible in worker reports.
`make_infrastructure<Policy>()` is the only
construction path; a second initialized infrastructure receives
`ErrorCode::Conflict`. The hot path is unchanged: `run()` performs one acquire load
per tick, and the watcher only records a reason. Dependency arrows stay one-way:
`app → logging, metrics, tracing, chrono, core`.

`nexigen::runner` owns detached per-instance and per-suite supervision: instance
directory layout, generated settings, fork/exec with `setsid`, signal escalation,
restart policy, and JSON-lines runner logging. `nexigen::hub` is a read-only
observatory over explicit run roots: inotify discovery, bounded stream rings with
replay/gap markers, and an epoll HTTP/SSE server with an embedded web UI. Neither
module adds work to the application tick, producer, snapshot, or sink paths; both
depend on `core`/`chrono`/`runner` in one direction only.

## The hard mandates

1. **C++23, no compiler extensions** (`-std=c++23`).
2. **No exceptions, no RTTI** — global `-fno-exceptions -fno-rtti`; never
   `try/catch/throw`, `dynamic_cast` or `typeid`. JSON compiles with
   `JSON_NOEXCEPTION=1`.
3. **Error model** — `nexigen::core::Result<T, E = ErrorCode>` / `core::Status`
   over `std::expected`. Branch on `has_value()`, dereference with `*res` /
   `res->`, read `.error()`. **Never `.value()`** — it aborts under
   `-fno-exceptions`.
4. **Zero heap allocation on hot paths** — producer loops, worker loops, harvest
   and sink callbacks never touch the allocator. All capacity is pre-allocated
   once at initialisation (explicitly allowed and expected).
5. **Lock-free / wait-free hot paths** — no mutexes, spinlocks or condition
   variables on hot paths. Multi-producer state uses per-thread sharding
   (`thread_local` slot registration) with `std::atomic` and explicit memory
   orders; shared atomic state is `alignas(64)` to kill false sharing.
6. **Linux high-resolution time & affinity** — `nexigen::chrono` calibrates the
   TSC against `CLOCK_MONOTONIC_RAW`; background workers pin themselves through
   the shared cold-path CPU machinery. `core::CpuSet` represents up to 1024 CPUs
   inline; `core::affinity` retains the legacy uint64 mask adapter for callers
   that only need CPUs 0–63. `ThreadSpec` adds a 15-byte name, Other/FIFO/RR
   policy and priority, and best-effort/required application. Workers apply a
   copied spec to themselves before sink startup; a required failure restores
   earlier changes and prevents the main loop, while a best-effort failure is
   exposed in `ThreadApplyReport` without stopping the worker. Reports are
   published once through release/acquire startup readiness, never per record.
7. **Deferred formatting** — variadic format arguments are packed cold-side;
   never format on a producer hot path.
8. **Naming** — PascalCase types, `snake_case` functions/fields, trailing
   `snake_case_` private members; aggressive `noexcept`/`constexpr`/`[[nodiscard]]`.

## Serialization contract (all modules)

Every public class/struct/snapshot/config exposes a cold-path
`std::string to_string() const` **and** a free `to_string(const T&)`; every
public value type provides non-throwing ADL `to_json`/`from_json`
(`JSON_NOEXCEPTION=1`); every enum additionally gets `constexpr to_string` /
`from_string`; every public type is usable in `std::format` via a
`std::formatter` specialization that delegates to `to_string()`. Types whose
reconstruction is factory- or I/O-bound (instruments, composite sinks) provide
`to_json` and deliberately **no** `from_json` — the absence is compile-time
pinned in the test TU. `std::string` and `nlohmann::json` are cold-path tools
only; serialisation bodies live in compiled units (`src/*/…_format.cpp`) so they
never bloat consumer TUs.

## Hot path vs cold path

| | Hot path (per-record / per-message) | Cold path (init, diagnostics, harvest edge) |
|---|---|---|
| Allocation | never | allowed (`std::string`, `nlohmann::json`) |
| Synchronisation | wait-free atomics only | may take the control-plane mutexes |
| Failure reporting | pre-checked `Status` returns | `core::Result` branching |
| Cost budget | ns-class, measured by `*_STRICT_LATENCY` gates | unbounded |

Single-threaded instrument/log recorders use plain storage under a documented
single-writer contract; multi-threaded variants pay a locked or relaxed RMW per
record to stay exact under any thread interleaving.

## Build, test and verification matrix

```bash
# Debug (development, correctness only)
cmake -B build/debug -G Ninja -DCMAKE_BUILD_TYPE=Debug
# Release (production: -O3 -march=native + LTO, latency gates active)
cmake -B build/release -G Ninja -DCMAKE_BUILD_TYPE=Release
# Portable Release: -O3 + LTO without host-specific -march/-mtune
cmake -B build/portable -G Ninja -DCMAKE_BUILD_TYPE=Release -DNEXIGEN_NATIVE_ARCH=OFF
# Shared CI runners: optimized code without the ns-class gate
cmake -B build/release-ci -G Ninja -DCMAKE_BUILD_TYPE=Release \
      -DNEXIGEN_STRICT_LATENCY_TESTS=OFF
# ThreadSanitizer (RelWithDebInfo so the STRICT_LATENCY define stays inert)
cmake -B build/tsan -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DNEXIGEN_ENABLE_TSAN=ON
# AddressSanitizer/UBSan
cmake -B build/asan -G Ninja -DCMAKE_BUILD_TYPE=Debug -DNEXIGEN_ENABLE_ASAN=ON
# Clang (requires a C++23 standard library: std::expected)
cmake -B build/clang -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_COMPILER=clang++

cmake --build <dir> && ctest --test-dir <dir> --output-on-failure
```

`NEXIGEN_STRICT_LATENCY_TESTS` (default `ON`) controls the ns-class gates in
Release and RelWithDebInfo. CI disables them because shared runners cannot
guarantee the 20 ns budget; run the default on dedicated hardware before a
performance-sensitive release. `NEXIGEN_NATIVE_ARCH` (default `ON`) adds
`-march=native`/`-mtune=native`; set it to `OFF` for binaries that must run on
other x86-64 hosts.

Coverage instrumentation and the aggregated report target:

```bash
cmake -B build/coverage -G Ninja -DCMAKE_BUILD_TYPE=Debug -DNEXIGEN_ENABLE_COVERAGE=ON
cmake --build build/coverage --target coverage
# overall gate: -DNEXIGEN_COVERAGE_FAIL_UNDER=95
```

`coverage` cleans `.gcda`, builds and runs the eleven unit-test binaries, and prints
line coverage per module via `cmake/coverage_report.py` (stdlib only). CLI smoke
tests are excluded because they exercise subprocesses rather than instrumented
library code. The target is mutually exclusive with ASan/TSan, enforces
`runner >= 90` and `hub >= 90`, and flushes gcov counters from forked runner
supervisors before `_exit()`/`exec`, so control-plane work is measured.

Current unit-test line coverage is **95.2%** (≈11 191 / 11 753 lines):

| Scope | Coverage |
|---|---:|
| Original seven modules | 95.9% (7 341 / 7 651) |
| `runner` | 93.0% (≈2 280 / 2 452) |
| `hub` | 95.2% (≈1 570 / 1 650) |

The project target is **≥95%**. CI enforces the **≥95%** project gate
(`-DNEXIGEN_COVERAGE_FAIL_UNDER=95`) plus the 90% runner and hub module gates.
Remaining uncovered lines are concentrated in platform-specific calibration
branches, sink open/read failure paths, `std::to_chars` error arms, and
retry/fork failure arms that cannot be triggered without fault injection. The
measured baseline and the P4 backlog are recorded in
[`project-analysis.md`](project-analysis.md).

`nexigen_hub_tests` also runs a fixed-seed malformed-request corpus against the
live server. `cmake/check_result_value.py` enforces the `Result::value()` ban in
CI; it strips comments/literals and allowlists only known non-Result receivers.
The Clang CI job runs `.clang-tidy` (`bugprone-*`, `performance-*`,
`portability-*`, and clang-analyzer including the cplusplus/portability opt-ins,
excluding the intentional-padding check) over every `src/*.cpp` translation
unit with warnings treated as errors.

Optional verification depth:

- `-DNEXIGEN_ENABLE_FAULT_INJECTION=ON` enables test-only cold-path hooks for
  runner `pipe2`/`fork`/`/dev/null` and hub `socket`/`bind`/`epoll`/`eventfd`.
  The Debug and coverage CI builds use it; production builds compile the checks
  to constant `false`.
- `-DNEXIGEN_BUILD_FUZZERS=ON` (Clang only) builds
  `nexigen_http_parse_fuzz`, a libFuzzer harness over the pure helpers in
  `include/nexigen/hub/detail/http_parse.hpp`. CI runs a 20 000-iteration smoke
  over `tests/fuzz/corpus/http_parse`.
- `NEXIGEN_RT_INTEGRATION_TESTS=1 ./tests/nexigen_core_tests` enables the
  `rt_integration_opt_in` suite on an explicitly configured RT host with
  `CAP_SYS_NICE`. It verifies real thread names, affinity, FIFO and RR priority.
  Without the variable the suite prints a skip and requires no root or
  `CAP_SYS_NICE`; ordinary CI therefore stays unprivileged.

All five GCC modes (Debug, Release, portable Release, TSan, ASan) must report
`100% tests passed` with no sanitizer diagnostics; the Clang job must do the same
under `-Werror`. TSan and ASan are mutually exclusive.
GitHub Actions runs the same matrix, plus the coverage gate and OpenSpec
validation, from `.github/workflows/ci.yml`. Tests are zero-dependency harnesses
(`NX_CHECK` / `run_suite`, arena `operator new` counter + `mallinfo2` delta for
allocation windows); Release adds the numeric latency gates
(`NEXIGEN_METRICS_STRICT_LATENCY`), which are compiled out under Debug and
sanitizers.

## Install and package

The build exports every module target under the `nexigen::` namespace and installs
a CMake package:

```bash
cmake --install build/release --prefix /opt/nexigen
```

The prefix receives `include/nexigen/`, the eight static archives, the
`nexigen-run`/`nexigen-hub` tools, and
`lib/cmake/nexigen/{nexigenConfig,nexigenConfigVersion,nexigenTargets}.cmake`.
`nexigenConfig.cmake` resolves `Threads` and `nlohmann_json` with
`find_dependency`; installed consumers therefore link `nexigen::logging`,
`nexigen::metrics`, etc. without repeating dependency discovery. Compatibility is
same-minor (`0.1.x`).

Build-only flags (`-Werror`, LTO, sanitizers) are attached through
`$<BUILD_INTERFACE:nexigen_options>` and never leak into consumer targets. The
`nexigen::core` JSON dependency is also re-attached by the package config, which
keeps the export valid when nlohmann/json came from FetchContent.

`tests/packaging/smoke_test.sh <build-dir> [prefix]` installs into a scratch
prefix and builds `tests/packaging/consumer/`, a standalone CMake project that
uses only `find_package(nexigen CONFIG REQUIRED)`.


## Spec-driven process

Behaviour is contract-first via [OpenSpec](https://openspec.github.io/openspec/):

1. `openspec/new change` → planning artifacts (proposal → specs/design → tasks).
2. `openspec apply` works the task list; each task carries its verification.
3. `openspec sync` merges the change's capability deltas into
   `openspec/specs/**`; `openspec archive` applies remaining deltas and retires
   the change.
4. `openspec validate --specs` gates the main-spec corpus (currently 59
   capabilities / 411 requirements / 1 103 scenarios).

Archived changes live in `openspec/changes/archive/` with their full planning
history. If code and specs disagree, the spec wins — fix the code or open a
change that revises the spec; never silently drift.
