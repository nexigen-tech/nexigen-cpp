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
| examples | `examples/<module>/` | `NEXIGEN_BUILD_EXAMPLES` option | executables |
| tests | `tests/<module>/` | `NEXIGEN_BUILD_TESTS` option | CTest suites |

`nexigen::core` is INTERFACE because it is pure templates; `chrono`, `logging` and
`metrics` are STATIC because they own compiled cold paths (calibration, the harvest
loop, sink I/O, snapshot serialisation) that must not be inlined into every
consumer. Modules never consume another module's `detail/` — the chrono extraction
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
   TSC against `CLOCK_MONOTONIC_RAW`; background workers pin themselves with
   `pthread_setaffinity_np` over a `uint64_t` core mask.
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
# ThreadSanitizer (RelWithDebInfo so the STRICT_LATENCY define stays inert)
cmake -B build/tsan -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DNEXIGEN_ENABLE_TSAN=ON
# AddressSanitizer/UBSan
cmake -B build/asan -G Ninja -DCMAKE_BUILD_TYPE=Debug -DNEXIGEN_ENABLE_ASAN=ON

cmake --build <dir> && ctest --test-dir <dir> --output-on-failure
```

All four modes must report `100% tests passed` with no sanitizer diagnostics.
TSan and ASan are mutually exclusive. Tests are zero-dependency harnesses
(`NX_CHECK` / `run_suite`, arena `operator new` counter + `mallinfo2` delta for
allocation windows); Release adds the numeric latency gates
(`NEXIGEN_METRICS_STRICT_LATENCY`), which are compiled out under Debug and
sanitizers.

## Spec-driven process

Behaviour is contract-first via [OpenSpec](https://openspec.github.io/openspec/):

1. `openspec/new change` → planning artifacts (proposal → specs/design → tasks).
2. `openspec apply` works the task list; each task carries its verification.
3. `openspec sync` merges the change's capability deltas into
   `openspec/specs/**`; `openspec archive` applies remaining deltas and retires
   the change.
4. `openspec validate --specs` gates the main-spec corpus (currently 25
   capabilities / 160 requirements / 387 scenarios).

Archived changes live in `openspec/changes/archive/` with their full planning
history. If code and specs disagree, the spec wins — fix the code or open a
change that revises the spec; never silently drift.
