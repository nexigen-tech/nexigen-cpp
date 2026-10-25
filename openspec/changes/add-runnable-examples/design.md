# Design

## Context

See `proposal.md` — Why. Current repository state that shapes the implementation:

- `examples/` contains only `.gitkeep`; there is no `examples/CMakeLists.txt` and no example target anywhere in the build.
- Root `CMakeLists.txt` defines `NEXIGEN_BUILD_TESTS` late, calls `enable_testing()` only inside that `if()`, and adds only `tests/`. Example tests must remain visible when tests are disabled, so this ordering changes.
- `tests/CMakeLists.txt` registers each unit-test executable with a plain `add_test(NAME … COMMAND …)` and a 600 s timeout; examples follow the same CTest conventions.
- All public targets inherit `nexigen_options` (`-Werror`, `-Wall -Wextra -Wpedantic`, `-Wconversion`, `-Wsign-conversion`, `-Wold-style-cast`, `-fno-exceptions`, `-fno-rtti`, sanitizer/optimization flags) transitively, so example code must compile cleanly under those flags.
- `ConsoleLogger`/`FileLogger`/`CompositeLogger` are non-movable (shard rings + atomics); examples keep them in static storage exactly like the README's existing snippets.
- `FileLogger::initialize()` returns OK once the worker `std::jthread` is launched; pin/open outcomes are published asynchronously through `writer_started()`, `writer_start_code()`, and `writer_mask()`.
- `FileLogger` rotation creates the base directory and the hourly period directory itself (`mkdirat`), so the example does not need to pre-create `/tmp/nexigen-examples-logs`.
- `README.md` ends with a stale Russian build-mode appendix that duplicates the English build sections.

## Goals / Non-Goals

**Goals:**

- One discoverable CMake switch that adds four standalone, self-contained executables and their CTest smoke tests.
- Examples that are realistic enough to serve as copy-paste starting points for consumers while staying short and deterministic.
- File-backed examples that verify their own output on the cold path, so a green `ctest` means more than "the process did not crash".
- Identical behaviour and exit status in Debug, Release, TSan, and ASan.

**Non-Goals:**

- No changes to any library header, source, public API, ABI, or hot-path behavior.
- No installation/packaging of example binaries, no example benchmark or latency gate.
- No new test framework, fixtures, or shared helper library; each example is one `main()` TU.
- No attempt to assert that CPU pinning succeeds on every host — the result is reported, not assumed.

## Decisions

### D1. Root CMake option and test-enable ordering

Add both build options before the test/example subdirectories and enable CTest once when either feature is requested:

```cmake
option(NEXIGEN_BUILD_TESTS "Build nexigen unit tests" ON)
option(NEXIGEN_BUILD_EXAMPLES "Build nexigen runnable examples" ON)

if(NEXIGEN_BUILD_TESTS OR NEXIGEN_BUILD_EXAMPLES)
    enable_testing()
endif()

# ... targets ...

if(NEXIGEN_BUILD_TESTS)
    add_subdirectory(tests)
endif()
if(NEXIGEN_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()
```

Rationale: the existing `enable_testing()` sits inside the tests branch, so `-DNEXIGEN_BUILD_TESTS=OFF` would silently discard `add_test()` calls made by the examples. Hoisting it keeps the two options independent. `examples` is only added when the option is ON, so the four TUs are not even configured in the opt-out build.

Alternative considered: keep `enable_testing()` in the tests branch and make examples depend on tests. Rejected — examples must be runnable when unit tests are disabled.

### D2. Example target factory in `examples/CMakeLists.txt`

`examples/CMakeLists.txt` defines a small local helper and adds the two module subdirectories:

```cmake
function(nexigen_add_example name source target)
    add_executable(${name} ${source})
    target_link_libraries(${name} PRIVATE ${target} Threads::Threads)
    add_test(NAME ${name} COMMAND ${name})
    set_tests_properties(${name} PROPERTIES TIMEOUT 600 LABELS "examples")
endfunction()

add_subdirectory(core)
add_subdirectory(logging)
```

- `Threads::Threads` is requested by `find_package(Threads REQUIRED)` in the root file and is needed directly by every example (`std::jthread` producers in all four; `nexigen_logging` links it only PRIVATE to its own cold units).
- Timeouts match the existing test convention (600 s) so slow TSan/ASan runs do not flake.
- The `examples` label lets CI run only `ctest -L examples` when desired without changing registration.
- Function name/arguments are project-local; no exported CMake API is added.

### D3. `examples/core/ex_spsc_ring_buffer.cpp`

**Element layout.** A namespace-scope example type, deliberately register-sized and trivially copyable:

```
offset 0   chrono::Timestamp stamp       8 B
offset 8   uint64_t         sequence     8 B
offset 16  double           sensor_value 8 B
sizeof == 24 B, alignof == 8
static_assert(std::is_trivially_copyable_v<TelemetryPacket>);
```

The struct provides `std::string to_string() const` (cold path) naming all three fields, plus a free `to_string(const TelemetryPacket&)` mirroring the project-wide mandate. `std::format` is used only inside `to_string()` and in cold-path prints.

**Queue.** `static nexigen::core::SpscRingBuffer<TelemetryPacket, 1024> queue{};` in an anonymous namespace. Its layout is unchanged from the library: producer and consumer cells each `alignas(64)`, payload slots `alignas(64)`, index publication via release stores and acquire loads, cached peer index on the relaxed steady path. Storage is 1024 × 24 B ≈ 24 KiB plus index/payload padding, all static; no heap allocation. The example does not add atomics, shards, or memory-order changes.

**Threads.** One `std::jthread` producer and one `std::jthread` consumer; `main` joins both. The producer constructs each packet from `Timestamp::now()` (checking `has_value()`, never `.value()`), increments a sequence, and retries `try_push` immediately on `BufferFull`; the consumer retries `try_pop` on `BufferEmpty` into a default-constructed packet and validates sequence order plus sensor value. Retry/error counters are plain `std::uint64_t` locals (single-writer each), so the example itself stays lock-free.

**Cold-path reporting.** After both threads join, print packet count, retry counts, the last observed `ErrorCode` through `std::format("{}", code)`, and `queue.to_string()`. Exit 0 only when all packets arrived in order and the queue reports empty; otherwise exit non-zero. The example diagnostics use `std::fprintf` + `std::fflush` so they remain distinct from any logger stream.

### D4. `examples/logging/ex_console_logger.cpp`

- `static ConsoleLogger<> logger{};`, initialized with `ConsoleLogger<>::Config{.initial_level = LogLevel::Debug}`. Both `initialize()` and `flush()` return `Status`; the example checks `has_value()`/`.error()` and never `.value()`.
- Four `std::jthread` workers each log through `verbose`, `debug`, `info`, `warning`, `error`, and `fatal` with a thread index argument. The default shard pool gives each worker its own SPSC shard; no extra synchronization is added.
- Runtime filtering: `logger.set_level(LogLevel::Warning)`, then log `info` (filtered) and `warning` (emitted), and print `logger.level()` formatted with `std::format("{}", ...)`; this exercises the direct `LogLevel` formatter.
- After `flush()`, print `logger.dropped().to_string()` and `logger.to_string()`. Because the example produces only a few dozen records into 32 × 128 slots, drops are expected to be zero; the diagnostic print is the demonstration, not a drop assertion.
- Exit 0 iff initialize and flush succeed and the post-filter warning call was accepted. The example's own status lines go to stderr via `std::fprintf`; logger output uses the library's raw-fd routing.

### D5. `examples/logging/ex_file_logger.cpp`

**Configuration.**

```
app_name    = "nexigen-example"
base_dir    = "/tmp/nexigen-examples-logs"
file_name   = "ex_file_logger.jsonl"
initial_level = LogLevel::Debug
rotation.kind = Rotation_schedule::Hourly
core_mask   = 0x1            // request core 0
throttle.enabled = true
throttle.max_records_per_window = 4096
throttle.window_ns = 1'000'000'000  // 1 s
```

The throttle budget is intentionally generous relative to the smoke workload: the example demonstrates a valid policy configuration and prints the resulting counters, while keeping the number of file lines deterministic. It does not try to manufacture rate-limited drops (which would depend on window timing).

**Startup and affinity.** The example prints `FileLogger_config::to_string()` before initialization. After `initialize()` returns OK, it polls `writer_started()` (bounded loop with 1 ms sleeps, ≤ 2 s), then prints `writer_start_code()` formatted as `ErrorCode` and `writer_mask()`. A pinning failure is reported but does not fail the example as long as the sink opens and output verifies — core 0 may be outside a container cpuset. A sink start failure surfaces in the same code and normally also fails the later file verification.

**Workload.** Two or three `std::jthread` workers log info/warning/error records plus the required UTF-8 call `logger.debug("Прямой вызов метода debug. Давление: {:.4f}", 0.0025);`. A `flush()` follows, which drains all shards and fsyncs the active file.

**Read-back.** Before initialization, remove any previous `/tmp/nexigen-examples-logs` tree with `std::filesystem::remove_all(path, error_code)` (non-throwing overload; never the throwing overload under `-fno-exceptions`). After flush:

1. Enumerate `/tmp/nexigen-examples-logs/*/ex_file_logger.jsonl` with `std::filesystem::directory_iterator(path, error_code)`.
2. Open the file with `std::ifstream` and read lines with `std::getline` (cold path; allocation allowed).
3. Assert the expected record count is present, the JSON schema fields `"level"`, `"app":"nexigen-example"`, and `"message"` appear, and the Cyrillic sentence with `0.0025` survives verbatim.
4. Exit 0 only if all checks pass; print each mismatch and a non-zero exit otherwise.

Alternative considered: hard-code the current hour label. Rejected — the label is UTC-derived and would make the example fail across an hour boundary or timezone assumptions. Directory enumeration is robust and still cold path.

### D6. `examples/logging/ex_composite_logger.cpp`

- `static CompositeLogger<ConsoleLogger<>, FileLogger<>, NullLogger> app{};`
- Console initialized at `Debug`; file initialized at `Info` with `base_dir = "/tmp/nexigen-examples-logs/composite"`, `file_name = "ex_composite_logger.jsonl"`, hourly rotation, `core_mask = 0x1`, a generous throttle, and `initial_level = Info`.
- `app.set_level(LogLevel::Info)` demonstrates one-call fan-out. Then `app.get<FileLogger<>>().set_level(LogLevel::Warning)` raises only the file sink; the example reads the level back through `app.get<1>()` (the `FileLogger` index in the pack) and through `app.get<FileLogger<>>()` to show both accessor forms address the same object. `NullLogger` participates in fan-out but emits nothing.
- Four `std::jthread` workers log `info` and `warning` records. With the composite at `Info` and the file at `Warning`, info reaches the console only, warning reaches both sinks. No virtual dispatch is introduced: the composite's tuple fold resolves each call statically, and each async member uses its existing per-thread shard (32 shards × 128 records × 192 B, `alignas(64)`, unchanged).
- One `app.flush()` drains all members (`NullLogger` flush is trivially OK). The example prints `app.to_string()` (which nests each member's diagnostic) and both active sinks' `dropped().to_string()`.
- Cold-path verification reads `/tmp/nexigen-examples-logs/composite/*/ex_composite_logger.jsonl`: at least one `"level":"warning"` line must be present and no `"level":"info"` line may appear, proving per-sink filtering. Exit 0 iff initialization, flush, and verification succeed.

### D7. README update

Add a `## 🧪 Runnable Examples` section after the logging API/tests material and before the license. It documents:

- default `NEXIGEN_BUILD_EXAMPLES=ON` and the `-DNEXIGEN_BUILD_EXAMPLES=OFF` opt-out;
- `cmake -B build/debug ... && cmake --build build/debug`;
- direct run commands for all four build-tree executables;
- `ctest --test-dir build/debug -L examples --output-on-failure` for the smoke subset;
- the four build-mode commands already used elsewhere in the README.

Delete everything from the trailing `У вас появилось 3 режима сборки...` block through end of file, leaving the license as the final section.

### D8. Concurrency and memory summary

No library concurrency is modified. Example-level concurrent structures are exactly the existing ones:

| Component | Layout / policy |
|---|---|
| `SpscRingBuffer<TelemetryPacket, 1024>` | producer/consumer index cells `alignas(64)`, payload `alignas(64)`, release store / acquire load publication, relaxed steady-state loads, no locks/heap |
| `ConsoleLogger<>` | 32 shards × 128 `LogRecord` slots × 192 B ≈ 768 KiB static, shard index cells `alignas(64)`; consumer-only line scratch 512 B + render scratch 256 B |
| `FileLogger<>` | same shard pool ≈ 768 KiB, plus consumer-only 4 KiB JSON scratch and fixed path buffers; worker thread pinned per `core_mask` |
| `CompositeLogger<ConsoleLogger<>, FileLogger<>, NullLogger>` | tuple members ≈ 1.5 MiB static total; no vtable; per-sink level cells are relaxed snapshots |
| Example locals | plain single-writer counters; no new atomics, locks, or heap on any producer/consumer path |

Examples use static storage for queues/loggers because the types are non-movable; the process lifetime matches the object lifetime, exactly as documented for downstream users.

### D9. Verification matrix

Run the same four configurations used by the project README and record `100% tests passed` in each:

| Mode | Configure | Why |
|---|---|---|
| Debug | `-DCMAKE_BUILD_TYPE=Debug` | correctness, warnings-as-errors |
| Release | `-DCMAKE_BUILD_TYPE=Release` | `-O3 -march=native` + LTO, strict warnings |
| TSan | `-DCMAKE_BUILD_TYPE=RelWithDebInfo -DNEXIGEN_ENABLE_TSAN=ON` | data races in example thread topologies |
| ASan/UBSan | `-DCMAKE_BUILD_TYPE=Debug -DNEXIGEN_ENABLE_ASAN=ON` | bounds/leaks in file read-back and fixed buffers |

Each mode runs the full `ctest` suite (library tests + four example smoke tests). TSan and ASan remain mutually exclusive.

## Risks / Trade-offs

- **Core pinning is host-dependent** → the example reports `writer_start_code()`/`writer_mask()` and only fails on output verification, keeping CI green on restricted cpusets while still surfacing pinning problems.
- **Stale files from earlier runs** → the file-backed examples clear their own `/tmp` subtree at startup using the non-throwing `std::filesystem` overloads before initializing.
- **Parallel CTest collisions on shared temp paths** → the file example uses `/tmp/nexigen-examples-logs`, the composite example uses `/tmp/nexigen-examples-logs/composite`; neither depends on the other.
- **Async worker start is not synchronous with `initialize()`** → both file-backed examples poll `writer_started()` before flush/read-back; a bounded poll prevents a hang if the worker never starts.
- **Console/file diagnostics can interleave with example status lines** → status lines go to stderr with `std::fprintf`/flush; logger streams use their documented raw-fd routing, and CTest asserts exit status, not exact interleaving.
- **Default-on option grows default build time** → users can opt out; the four TUs are small and only link existing targets.
- **`-Werror` plus `-Wconversion`** → examples use explicit casts for sizes and counters, `std::size_t` loop bounds, and format arguments with matching types; the Debug build is the first gate.
- **`std::filesystem` under `-fno-exceptions`** → only the `error_code` overloads are used; throwing overloads are banned in the example sources.

## Migration Plan

Additive only. Existing consumers and library builds are unaffected. Rollback is deleting the `examples/` additions, the root option/subdirectory block, and the README section; no data migration or ABI transition is involved.

## Open Questions

None — the build switch, example contents, temp paths, and verification criteria are fully specified by the proposal and specs.
