# Proposal

## Why

The library currently ships no executable usage examples: `examples/` contains only a `.gitkeep`, and the API reference lives entirely in prose inside `README.md`. A new consumer must reconstruct thread topology, error handling, configuration, and teardown rules from headers and unit tests before writing a first working program, and the project has no end-to-end proof that the documented API actually links and runs in every supported build mode. Runnable, self-contained examples close that gap and give CI a permanent integration check for the public surface of `nexigen::core` and `nexigen::logging`.

## What Changes

- **CMake example integration**: add a root option `NEXIGEN_BUILD_EXAMPLES` (default `ON`), an `examples/CMakeLists.txt`, and per-module example subdirectories `examples/core/` and `examples/logging/`. Remove the `examples/.gitkeep` placeholder.
- **Four runnable example executables**:
  - `examples/core/ex_spsc_ring_buffer.cpp` — a real-time telemetry producer/consumer pipeline over a `static SpscRingBuffer<TelemetryPacket, 1024>`, demonstrating wait-free `try_push`/`try_pop`, explicit `BufferFull`/`BufferEmpty` handling through `has_value()`/`.error()` (never `.value()`), direct `ErrorCode` formatting, and a cold-path `queue.to_string()` summary.
  - `examples/logging/ex_console_logger.cpp` — `ConsoleLogger<>` initialization, multi-threaded `std::jthread` producers using the variadic convenience API, runtime `set_level()` filtering, direct `LogLevel` formatting, `flush()`, and `dropped()`/`to_string()` diagnostics.
  - `examples/logging/ex_file_logger.cpp` — `FileLogger<>` writing JSON lines under `/tmp/nexigen-examples-logs`, with `FileLogger_config` (`app_name`, `base_dir`, `file_name`, `Rotation_schedule::Hourly`, CPU `core_mask`, throttle budget), UTF-8 Cyrillic formatted messages, `flush()`, and cold-path read-back/verification of the generated JSONL file.
  - `examples/logging/ex_composite_logger.cpp` — `CompositeLogger<ConsoleLogger<>, FileLogger<>, NullLogger>` with zero virtual dispatch, global `set_level()` fan-out, per-sink configuration through `get<ConsoleLogger<>>()` / `get<FileLogger<>>()` / `get<Index>()`, multi-threaded logging, and a unified `flush()`.
- **CTest smoke coverage**: register every example as a CTest test so `ctest` verifies each executable starts, completes its scenario, and exits with status 0. Examples remain independently runnable from their build output.
- **Spec deltas**: add runnable-example requirements and WHEN/THEN scenarios to `core/spsc-ring-buffer`, `logging/console-logger`, `logging/file-logger`, and `logging/composite-logger`, plus a new `examples` capability that owns the build option, executable discovery, and CTest smoke contract.
- **Documentation**: add a `## Runnable Examples` section to `README.md` describing how to build/run all four executables (including the opt-out flag), and remove the leftover Russian build-mode notes at the very bottom of the file.
- **Verification**: build and run the complete `ctest` suite in Debug, Release (`-O3 -march=native` + LTO), TSan, and ASan configurations.

### Performance, latency, and allocation impact

- **Library runtime: none.** No header, source, or library behavior changes; all four examples are separate executables and the library remains byte-for-byte equivalent for existing consumers.
- **Hot path: unchanged.** Example loops use the same wait-free APIs; they add no synchronization, allocation, or work to library internals. The ring-buffer example performs fixed-size by-value pushes of a trivially copyable `TelemetryPacket` on a `static` queue, with no heap traffic in the producer/consumer loops.
- **Build time and binary size:** four additional translation units and executables. Static logger shard pools make each logging example's BSS in the sub-megabyte to low-megabyte range (`ConsoleLogger<>` ≈ 786 KiB, `FileLogger<>` ≈ 786 KiB, `CompositeLogger<...>` ≈ 1.5 MiB); `NEXIGEN_BUILD_EXAMPLES=OFF` removes them entirely.
- **CTest duration:** four short-lived processes (sub-second warm, longer under TSan/ASan and while the file sink fsyncs). No effect on library test timing or latency gates.
- **Release build:** `NEXIGEN_BUILD_EXAMPLES` defaults to `ON`, so default builds gain four targets; this is a build-surface addition, not an API or ABI break.

## Capabilities

### New Capabilities

- `examples`: the project-level contract for optional runnable examples — the `NEXIGEN_BUILD_EXAMPLES` option, the `examples/` layout and executable naming, standalone execution, and CTest registration as a smoke check.

### Modified Capabilities

- `core/spsc-ring-buffer`: adds the requirement that a runnable ring-buffer example demonstrates the documented wait-free push/pop, error handling, enum formatting, and cold-path diagnostic surface.
- `logging/console-logger`: adds the requirement that a runnable console example demonstrates initialization, multi-threaded producers, level filtering, enum formatting, flush, and drop/diagnostic snapshots.
- `logging/file-logger`: adds the requirement that a runnable file example demonstrates JSONL configuration, rotation, affinity, throttling, UTF-8 messages, flush, and read-back verification.
- `logging/composite-logger`: adds the requirement that a runnable composite example demonstrates virtual-free fan-out, global and per-sink level control, accessor forms, multi-threaded producers, and unified flush.

## Impact

- **Build**: root `CMakeLists.txt` gains `NEXIGEN_BUILD_EXAMPLES` and `add_subdirectory(examples)`; new `examples/CMakeLists.txt`, `examples/core/`, `examples/logging/`; four new executables linked against `nexigen::core` / `nexigen::logging` and `Threads::Threads` where threads are used.
- **Tests/CI**: four new CTest entries; the full Debug/Release/TSan/ASan matrix now also exercises the public API through real programs.
- **Documentation**: `README.md` gains a Runnable Examples section and loses the stale trailing Russian build notes.
- **Library code, public API, ABI, dependencies**: unchanged. Examples use only the existing public headers and CMake targets.
- **Files removed**: `examples/.gitkeep`.
