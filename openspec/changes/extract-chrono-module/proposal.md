# Proposal

## Why

Time handling is currently split across three places: the `Timestamp` value type lives in `nexigen::core`, while the calibrated stamp source, UTC-offset clock calibration, and civil-time/ISO-8601 formatting are private details of `nexigen::logging` (`detail/stamp_source.hpp`, `detail/time_format.hpp`, `src/logging/clock_calibration.cpp`, `src/logging/time_format.cpp`). Every other planned subsystem (metrics, HIL scheduler, sim clocks) needs these same primitives, so they must become a shared, reusable module. The project's namespace map already reserves `nexigen::chrono -> include/nexigen/chrono/, src/chrono/, tests/chrono/`; this change fills it.

## What Changes

- **BREAKING** Move `Timestamp` from `include/nexigen/core/timestamp.hpp` (`nexigen::core`) to `include/nexigen/chrono/timestamp.hpp` (`nexigen::chrono`). The old header is deleted with no forwarding alias — all includes and namespace references are updated in place.
- Promote the logging time internals into public, reusable `nexigen::chrono` API:
  - `detail/stamp_source.hpp` → `include/nexigen/chrono/stamp_source.hpp` (`nexigen::chrono::Stamp_source`, `sample_utc_offset`, `utc_from_mono_ns`).
  - `detail/time_format.hpp` → `include/nexigen/chrono/time_format.hpp` (`nexigen::chrono::Civil_time`, `days_from_utc_ns`, `civil_from_utc_ns`, `format_utc_iso8601_ns`).
  - `src/logging/clock_calibration.cpp` → `src/chrono/clock_calibration.cpp`; `src/logging/time_format.cpp` → `src/chrono/time_format.cpp`.
- New CMake static library target `nexigen_chrono` (alias `nexigen::chrono`) built from `src/chrono/*.cpp`, linked to `nexigen::core` and `nexigen_options`; `nexigen_logging` now links `nexigen::chrono` and consumes `chrono::Timestamp`, `chrono::Stamp_source`, and the chrono time-formatting API.
- New dedicated test target `nexigen_chrono_tests` (`tests/chrono/chrono_test.cpp`) holding the relocated timestamp suite, the stamp/calibration suite (moved out of `tests/logging/logging_test.cpp`), and the time-format fixtures; core and logging tests are updated accordingly.
- OpenSpec deltas: new `chrono/timestamp`, `chrono/stamp-source`, `chrono/time-format` capabilities; `core/timestamp` requirements REMOVED (relocated); `logging/log-record` and `logging/file-logger` requirements MODIFIED to reference `nexigen::chrono`.
- README updated: `Timestamp` documented under a new `nexigen::chrono` API section instead of `nexigen::core`.

Behavior of every moved entity is unchanged — this is a relocation of ownership, not a redesign.

## Capabilities

### New Capabilities

- `chrono/timestamp`: monotonic nanosecond point-in-time value type (`CLOCK_MONOTONIC_RAW`), ordering, saturating arithmetic, hot-path safety — relocated from core with identical requirements.
- `chrono/stamp-source`: producer-owned calibrated monotonic stamp source (TSC fast path with monotonic clamp) plus cold-path UTC offset sampling/conversion, reusable by any subsystem.
- `chrono/time-format`: allocation-free UTC civil-time decomposition and fixed-width ISO-8601 nanosecond formatting.

### Modified Capabilities

- `core/timestamp`: all requirements REMOVED — the capability relocates to `nexigen::chrono` (no backward-compatible alias is kept).
- `logging/log-record`: "Monotonic nanosecond stamping" now sources its clock domain from `nexigen::chrono` instead of `nexigen::core`.
- `logging/file-logger`: "Nanosecond UTC timestamps via calibrated clocks" now uses `nexigen::chrono`'s clock calibration (UTC-offset sampling) and time formatting instead of private logging details.

## Impact

- **Code**: `include/nexigen/core/timestamp.hpp`, `include/nexigen/logging/detail/{stamp_source,time_format}.hpp`, `src/logging/{clock_calibration,time_format}.cpp` move (git mv) into `include/nexigen/chrono/` and `src/chrono/`; include/namespace updates in `log_record.hpp`, `detail/producer.hpp`, `file_logger.hpp`, `src/logging/json_format.cpp`, `src/logging/rotation.cpp`; `CMakeLists.txt` (new target + logging link), `tests/CMakeLists.txt` (new chrono test target); `tests/core/core_test.cpp`, `tests/logging/logging_test.cpp`, new `tests/chrono/chrono_test.cpp`; `README.md`.
- **Dependencies**: `nexigen_logging` gains a PUBLIC dependency on `nexigen::chrono`; `nexigen_chrono` depends on `nexigen::core` (for `Result`/`Status`/`ErrorCode`) and `nexigen_options`. No new external dependencies.
- **Performance & memory**: zero hot-path impact. `Timestamp`, `Stamp_source::sample()`, and `utc_from_mono_ns` stay inline in headers; the `alignas(64)` single-cache-line layout of `Stamp_source` and the `static_assert(sizeof(Timestamp) == 8)` are preserved. No heap allocation is added or removed on any path; the ≤ 20 ns Release enqueue latency gate must keep passing unchanged. One extra static library is produced (two cold-path translation units, `calibrate()` and formatting), which adds negligible link-time size; out-of-line call sites (writer/consumer threads only) are unaffected.
- **Compatibility**: source-breaking for downstream users of `nexigen::core::Timestamp` (renamed include path + namespace); acceptable pre-1.0, no shims retained. ABI changes for `nexigen_logging` symbols (moved namespace), which the project does not promise stable.
