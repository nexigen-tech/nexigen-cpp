# Tasks

All moves are copy-preserving (git mv / verbatim body relocation); no behavior change. Each task verifies the `nexigen_chrono` (and, where listed, the dependent) target compiles and its tests pass.

## 1. Leaf enum: `ClockLogLevel`

- [x] 1.1 Create `include/nexigen/chrono/clock_log_level.hpp` with `enum ClockLogLevel` + its `to_string`/`from_string`/`to_json`/`from_json` (moved verbatim from `chrono_provider.hpp` lines 40–88). Drop the dangling `/// ClockLogLevel formats as its canonical name…` comment (line 269); like `ClockId`, `ClockLogLevel` formats through the shared generic `std::formatter` and has no per-type specialization. Verify `nexigen_chrono` compiles and the `ClockLogLevel` JSON/`to_string`/formatter tests pass.

## 2. detail structs + concept (from `chrono_provider.hpp`)

- [x] 2.1 Create `include/nexigen/chrono/detail/sink_binding.hpp` with `struct detail::SinkBinding` (lines 122–127). Verify `nexigen_chrono` compiles.
- [x] 2.2 Create `include/nexigen/chrono/detail/log_binding.hpp` with `struct detail::LogBinding` (lines 129–133). Verify `nexigen_chrono` compiles.
- [x] 2.3 Create `include/nexigen/chrono/concepts/clock_logger_concept.hpp` with `concept concepts::ClockLogger` (lines 108–118). Verify `nexigen_chrono` compiles.

## 3. Config struct: `ChronoProviderConfig`

- [x] 3.1 Create `include/nexigen/chrono/chrono_provider_config.hpp` with `struct ChronoProviderConfig` + free `to_string` + `to_json`/`from_json` declarations + `std::formatter` (moved from `chrono_provider.hpp` lines 90–106, 271–279). Verify `nexigen_chrono` compiles and the config round-trip tests pass.
- [x] 3.2 Create `src/chrono/chrono_provider_config.cpp` with `ChronoProviderConfig::to_string`, free `to_string`, `to_json`/`from_json` (moved from `chrono_provider.cpp` lines 131–169). Verify `nexigen_chrono` links and the config JSON round-trip tests pass.

## 4. Trim the provider to its class

- [x] 4.1 Trim `chrono_provider.hpp` to `class ChronoProvider` + its `std::formatter` (lines 137–265, 281–289); include `clock_log_level.hpp`, `chrono_provider_config.hpp`, `snapshot.hpp`, `concepts/chrono_sink_concept.hpp`, `concepts/clock_logger_concept.hpp`, `detail/sink_binding.hpp`, `detail/log_binding.hpp`. Verify `nexigen_chrono` compiles and provider tests pass.
- [x] 4.2 Trim `src/chrono/chrono_provider.cpp` to the `ChronoProvider` implementation only (remove the config serialization now in `chrono_provider_config.cpp`). Verify `nexigen_chrono` links and provider tests pass.

## 5. File renames (match the type name)

- [x] 5.1 Rename `chrono_snapshot.hpp` → `snapshot.hpp` and `src/chrono/chrono_snapshot.cpp` → `snapshot.cpp` (`struct Snapshot`). Update internal includes in `chrono_provider.hpp`, `composite_chrono_sink.hpp`, `json_file_chrono_sink.hpp`, `null_chrono_sink.hpp`, `console_chrono_sink.hpp`, `concepts/chrono_sink_concept.hpp`, `json_file_chrono_sink_config.hpp`. Verify `nexigen_chrono` compiles and the snapshot tests pass.
- [x] 5.2 Rename `chrono_settings.hpp` → `settings.hpp` and `src/chrono/chrono_settings.cpp` → `settings.cpp` (`struct Settings`). Update `include/nexigen/app/infrastructure.hpp` and `include/nexigen/app/app_settings.hpp`. Verify `nexigen_chrono` and `nexigen_app` compile and the settings-loader tests pass.
- [x] 5.3 Rename `time_format.hpp` → `civil_time.hpp` and `src/chrono/time_format.cpp` → `civil_time.cpp` (`struct CivilTime` + `days_from_utc_ns`/`civil_from_utc_ns`/`format_utc_iso8601_ns`). Verify `nexigen_chrono` compiles and the time-format tests pass.

## 6. Split the UTC-offset utility out of `stamp_source.hpp`

- [x] 6.1 Create `include/nexigen/chrono/utc_offset.hpp` with `sample_utc_offset` declaration + inline `utc_from_mono_ns` (moved from `stamp_source.hpp` lines 135–146). Verify `nexigen_chrono` compiles.
- [x] 6.2 Trim `stamp_source.hpp` to `class StampSource` + its free `to_string` (lines 44–133, 148–151). Verify `nexigen_chrono` compiles and the stamp tests pass.
- [x] 6.3 Rename `src/chrono/clock_calibration.cpp` → `stamp_source.cpp` (`StampSource::calibrate`) and create `src/chrono/utc_offset.cpp` (`sample_utc_offset`). Verify `nexigen_chrono` links and the stamp/utc-offset tests pass.

## 7. Complete `JsonFileChronoSinkConfig` in its own header

- [x] 7.1 Move `JsonFileChronoSinkConfig`'s `to_json`/`from_json` (lines 134–153) and `std::formatter` (lines 167–175) from `json_file_chrono_sink.hpp` into `json_file_chrono_sink_config.hpp`. Trim the config header's unused includes (`charconv`, `cstdint`, `unistd`, `cerrno`, `fcntl`, `chrono_snapshot`, `concepts/chrono_sink_concept`, `result`, `sink_io`) and drop the dangling `/// A sink that appends…` comment. Verify `nexigen_chrono` compiles and the config JSON round-trip tests pass.
- [x] 7.2 Trim `json_file_chrono_sink.hpp` to `class JsonFileChronoSink` + its `to_string`/`to_json`/formatter; fix the preamble so `#pragma once` is the first directive and the header comment is intact. Verify `nexigen_chrono` compiles and the JSON-file sink tests pass.

## 8. Cleanup: dangling comment in `clock_id.hpp`

- [x] 8.1 Drop the trailing `/// ClockId formats as its canonical name…` comment (lines 84–86) that references a removed formatter. Verify `nexigen_chrono` compiles and the clock-id tests pass.

## 9. External include-site updates

- [x] 9.1 Update `#include` sites in `include/nexigen/{metrics,tracing,diagnostics,logging,runner}/*.hpp` to the new per-entity headers (`civil_time.hpp`, `utc_offset.hpp`, `stamp_source.hpp`, `timestamp.hpp`) so each header includes only what it uses. Verify `nexigen_metrics`, `nexigen_tracing`, `nexigen_diagnostics`, `nexigen_logging`, `nexigen_runner` compile.
- [x] 9.2 Update `#include` sites in `src/{metrics,tracing,logging,runner,hub}/*.cpp` (metrics `provider.cpp`, tracing `sink_format.cpp`, logging `json_format.cpp`/`rotation.cpp`, runner `instance_lifecycle.cpp`/`app_suite.cpp`, hub `streaming.cpp`). Verify those targets compile and link.
- [x] 9.3 Update `#include` sites in `tests/{chrono,app,logging,diagnostics}/*.cpp` (`chrono_test.cpp` re-lists the split headers explicitly). Verify the test targets compile and link.
- [x] 9.4 Update `#include` sites in `examples/{chrono,metrics,diagnostics,app,suite,core}/*.cpp`. Verify the example targets compile.

## 10. Verification

- [x] 10.1 Build and run 100% of unit tests under Debug and Release (`-O3 -march=native -flto` with latency gates). Verify `ctest` reports no failures and the allocation-free test-arena gates (snapshot `snapshot_now`, `Timestamp::now`) still hold.
- [x] 10.2 Build and run 100% of unit tests under ThreadSanitizer (`-DNEXIGEN_ENABLE_TSAN=ON`) and AddressSanitizer (`-DNEXIGEN_ENABLE_ASAN=ON`). Verify `ctest` reports no failures and no sanitizer reports.

## Workflow follow-up

- After implementation passes review, archive the change.
- Confirm the `.openspec.yaml` `skip_specs: true` remains correct (pure refactor, no spec-level change).
