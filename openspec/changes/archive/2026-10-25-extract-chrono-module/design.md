# Design

## Context

See proposal.md — Why. Current state: `Timestamp` is header-only in `nexigen::core` (`include/nexigen/core/timestamp.hpp`); the time machinery owned by logging is split between inline headers (`include/nexigen/logging/detail/stamp_source.hpp`, `.../time_format.hpp`) and two compiled cold-path units (`src/logging/clock_calibration.cpp`, `src/logging/time_format.cpp`). Consumers to re-point: `log_record.hpp` (field type), `detail/producer.hpp` (owns a `Stamp_source`, calls `sample()`), `file_logger.hpp` (`sample_utc_offset`/`utc_from_mono_ns`), `src/logging/json_format.cpp` and `src/logging/rotation.cpp` (civil-time + ISO-8601 formatting), plus tests (`tests/core/core_test.cpp` timestamp suite; `tests/logging/logging_test.cpp` `test_stamp` and `test_time_format` suites) and `README.md`.

Hard constraints unchanged: `-fno-exceptions`, `-fno-rtti`, C++23, zero hot-path allocation, ≤ 20 ns median Release enqueue latency, and the existing strict warning set (`nexigen_options`).

## Goals / Non-Goals

**Goals:**
- One owner for all time primitives (`nexigen::chrono`), consumable by logging, metrics, and future subsystems without reaching into another module's `detail/`.
- Byte-identical observable behavior: same clock domains, same formats, same monotonicity guarantees, same error-model surface.
- Hot path untouched: `Timestamp` operations, `Stamp_source::sample()`, and `utc_from_mono_ns` stay inline in public headers.

**Non-Goals:**
- No new behavior (no wall-clock `Timestamp` variant, no configurable format precision, no timezone support).
- No backward-compatibility shims (`core::Timestamp` alias headers) — pre-1.0, breaking move accepted.
- No changes to the SPSC ring, producer/consumer synchronization, or install/export CMake surface (none exists today).

## Decisions

### D1 — Module shape: one STATIC target `nexigen_chrono` (alias `nexigen::chrono`)

`add_library(nexigen_chrono STATIC ...)` globbed from `src/chrono/*.cpp` (`CONFIGURE_DEPENDS`, matching the logging pattern), `target_link_libraries(nexigen_chrono PUBLIC nexigen_core)`. `nexigen_core` is already `INTERFACE → nexigen_options`, so flags/`_POSIX_C_SOURCE=200809L`/`cxx_std_23` arrive transitively — exactly how `nexigen_logging` gets them today.

*Alternative considered:* header-only INTERFACE library like core. Rejected: `Stamp_source::calibrate()` sleeps ~2 ms and must not be inline in a hot-path consumer's TU (code bloat, I-cache pressure on the producer); formatting units belong compiled like their logging counterparts.

### D2 — Header/namespace layout: three public headers, `detail` level dropped

- `include/nexigen/chrono/timestamp.hpp` — `nexigen::chrono::Timestamp` (contents moved verbatim; `Result` stays `core::Result`/`core::Status`, so the header keeps including `<nexigen/core/result.hpp>`).
- `include/nexigen/chrono/stamp_source.hpp` — `nexigen::chrono::Stamp_source`, free functions `sample_utc_offset`, `utc_from_mono_ns` (moved out of `logging::detail`: they are generic clock utilities, not logger internals).
- `include/nexigen/chrono/time_format.hpp` — `nexigen::chrono::Civil_time`, `days_from_utc_ns`, `civil_from_utc_ns`, `format_utc_iso8601_ns`.

Sources: `src/chrono/clock_calibration.cpp`, `src/chrono/time_format.cpp`. Files move with `git mv` so history follows them; bodies change only in `#include` paths and the namespace block (`nexigen::logging::detail` → `nexigen::chrono`, `core::Timestamp` → `Timestamp` unqualified inside the module).

*Alternative considered:* keep `nexigen::chrono::detail` for the stamp source. Rejected — the whole point of the change is public reusability; the "producer-owned, single-writer" contract is API, not implementation.

### D3 — `std::chrono` shadowing discipline

Inside `namespace nexigen::chrono` (all headers, both `src/chrono/*.cpp` units, `tests/chrono/`), an unqualified `chrono::` name resolves to `nexigen::chrono` by enclosing-namespace lookup — and `clock_calibration.cpp` legitimately uses `std::chrono::milliseconds` for the calibration sleep. Rule for the module and its tests: standard-library time entities are always written fully qualified (`std::chrono::`, `std::this_thread::`), never pulled via `using namespace`. Existing code already qualifies (`std::this_thread::sleep_for(std::chrono::milliseconds(2))` in `clock_calibration.cpp`), so this is a style pin, not a rewrite. The `_GNU_SOURCE`/`_POSIX_C_SOURCE` feature macros keep their current providers (`nexigen_core` INTERFACE, `nexigen_logging` PUBLIC); `nexigen_chrono` inherits `_POSIX_C_SOURCE=200809L` transitively from core — the only POSIX surface chrono needs is `clock_gettime`, used inline in `timestamp.hpp`/`stamp_source.hpp`.

### D4 — Concurrency & memory layout of `Stamp_source` (unchanged, restated as module contract)

`class alignas(64) Stamp_source` with `static_assert(sizeof(Stamp_source) <= 64)` — one owned cache line, no atomics. The object is written exclusively by one producer thread (single-producer contract inherited from the logging design D6/D7); the monotonic clamp (`last_ns_`) is plain `uint64_t`, safe under that ownership. Cross-thread publication of stamps continues solely through `SpscRingBuffer<LogRecord>`'s existing acquire/release head/tail protocol — this change adds no new shared state and chooses no new `std::memory_order`. On x86-64 the fast path is `__rdtsc()` + `ns = mono_anchor_ns_ + ((delta * scale_) >> 20)` with re-anchor every 2^42 ticks; non-x86 falls back to `Timestamp::now()`. All bit-for-bit identical to today's behavior.

### D5 — Logging becomes a consumer of chrono

`nexigen_logging` links `nexigen_chrono` **PUBLIC**: `LogRecord`'s `chrono::Timestamp stamp` field appears in logging's public headers, so every downstream TU that includes `log_record.hpp` compiles against chrono's headers and must link chrono transitively (same reasoning as the existing PUBLIC link to `nexigen_core`; keeping the explicit `nexigen_core` link too documents the direct dependency). Re-pointed call sites (include-path + namespace-prefix edits only, zero logic):

- `log_record.hpp`: `core::Timestamp stamp` → `chrono::Timestamp stamp`; include `<nexigen/chrono/timestamp.hpp>`.
- `detail/producer.hpp`: includes chrono's `stamp_source.hpp`; `Stamp_source stamp_` → `chrono::Stamp_source stamp_`; `core::Timestamp` → `chrono::Timestamp`.
- `file_logger.hpp`: `sample_utc_offset`/`utc_from_mono_ns` prefixed `chrono::`; includes chrono headers.
- `json_format.cpp`, `rotation.cpp`: call `chrono::format_utc_iso8601_ns`/`chrono::civil_from_utc_ns`/`chrono::days_from_utc_ns`.
- `src/logging/clock_calibration.cpp` and `time_format.cpp` are deleted (moved to `src/chrono/`).

### D6 — Tests: dedicated chrono suite, suites moved not duplicated

`tests/chrono/chrono_test.cpp` reuses the established single-TU zero-dependency harness pattern (`NX_CHECK` + `run_suite`, arena `operator new` allocation counter from `core_test.cpp`):

- `timestamp` suite — moved verbatim from `tests/core/core_test.cpp` (`test_timestamp` + the two `static_assert`s).
- `stamp` suite — moved from `tests/logging/logging_test.cpp` (`test_stamp`: non-decreasing loop, 20 ms ±30 % fidelity, independent sources).
- `stamp_utc_offset` + `time_format` suites — the calibration-offset sampling contract and the moved `test_time_format` fixtures (byte-exact epochs, leap day, pre-epoch, year-clamp buffer safety).

`core_test.cpp` drops the timestamp suite; `logging_test.cpp` drops `test_stamp`/`test_time_format` (its latency benchmark keeps using stamps *through* producers — that stays a logging concern). New target `nexigen_chrono_tests` links `nexigen_chrono Threads::Threads` (calibrate sleeps), `TIMEOUT 600` like its siblings. `tests/CMakeLists.txt` keeps the Release-only `NEXIGEN_LOGGING_STRICT_LATENCY` gate exactly as-is: the ≤ 20 ns gate remains in `logging_test.cpp` and must pass unchanged (the moved headers keep `sample()` inline, so the producer hot path sees the same code).

*Alternative considered:* keep stamp/format tests in logging_test to avoid new test scaffolding. Rejected — tests should live with the module that owns the behavior (config namespace map).

### D7 — Spec/archival bookkeeping

Deltas authored in this change: ADDED `chrono/{timestamp,stamp-source,time-format}`; REMOVED all four `core/timestamp` requirements (Reason: relocation; Migration: new include + namespace); MODIFIED `logging/log-record` "Monotonic nanosecond stamping" and `logging/file-logger` "Nanosecond UTC timestamps via calibrated clocks" to name `nexigen::chrono`. `core/timestamp/spec.md` stays in the main specs untouched until archive, when the REMOVED delta prunes it (resulting capability becomes requirement-free; archiving deletes it).

## Risks / Trade-offs

- [Namespace lookup surprise: code inside `nexigen::chrono` writing `chrono::steady_clock` gets `nexigen::chrono`, not `std::chrono`] → D3 qualification rule; the only standard-library use in the moved units is the calibration sleep, already `std::`-qualified.
- [Missed reference to the deleted headers (docs, examples, future branches)] → grep-gate tasks: after the move, `include/nexigen/core/timestamp.hpp`, `logging/detail/stamp_source.hpp`, `logging/detail/time_format.hpp` and `core::Timestamp` must have zero hits repo-wide (excluding `openspec/changes/archive/`, which is history).
- [Hot-path regression if `sample()` stops inlining (bigger TU boundary, LTO interplay)] → `sample()` remains defined in the header included by `producer.hpp` (same translation units inline it as today; only the header path changes); the Release `NEXIGEN_LOGGING_STRICT_LATENCY` gate re-proves ≤ 20 ns on every CI config run.
- [PUBLIC chrono link leaks `-D_POSIX_C_SOURCE`/include dirs differently than before] → identical flags already flow transitively today via `nexigen_core` PUBLIC; the new hop adds nothing new (verify: `cmake --build` with `--verbose` once shows the same define set).
- Breaking move with no shims: downstream (none in-tree beyond logging/metrics placeholders) must adopt `nexigen::chrono` — accepted pre-1.0 (proposal).

## Migration Plan

1. `git mv` headers/sources into `include/nexigen/chrono/`, `src/chrono/`; rewrite namespaces/includes (D2).
2. Add `nexigen_chrono` target; re-point `nexigen_logging` (D1, D5).
3. Move test suites into `tests/chrono/chrono_test.cpp` + `tests/CMakeLists.txt` entry (D6).
4. Update `README.md` API sections.
5. Verify in all four modes — Debug, Release (latency gate), TSan (`-DNEXIGEN_ENABLE_TSAN=ON`), ASan — each `ctest` run reports `100% tests passed`.
6. Rollback strategy: pure `git revert` of the change commit — no runtime state, on-disk format, or API shim to unwind; JSON log output bytes are unchanged, so no log-compatibility concern.
