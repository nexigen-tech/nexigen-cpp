# Tasks

## 1. LogFrame with pid

- [x] 1.1 Update `include/nexigen/logging/log_frame.hpp`: add `std::uint32_t pid{}` at offset 20, shrink `payload` to 225 bytes, update `to_string` to include pid, update `to_json`/`from_json`. Verify: `static_assert(sizeof==256)`, all offset static_asserts still hold, JSON round-trip test passes.
- [x] 1.2 Update `include/nexigen/logging/detail/iceoryx2_sink.hpp`: cache `sink_pid_` from `::getpid()` in `start()`, set `frame_.pid = sink_pid_` in `handle()`. Verify: existing round-trip test captures pid in the received frame.

## 2. LogDaemonConfig

- [x] 2.1 Create `include/nexigen/logging/log_daemon_config.hpp`: `LogDaemonConfig` with fields per design D8, `to_string` + formatter + JSON. Verify: round-trip + validation rejection test.

## 3. LogDaemon tool

- [x] 3.1 Create `tools/nexigen-logd/main.cpp`: `Infrastructure` + `Subscriber<LogFrame>` + `WaitSet` drain loop + formatter + batch buffer + `detail::FileSink`. The daemon's own logging uses a plain `FileLogger` (not iceoryx2). Verify: builds as `nexigen-logd` binary, `--help` prints usage.
- [x] 3.2 CMake: register `nexigen-logd` target behind `NEXIGEN_ENABLE_COM` (links `nexigen_logging_com` + `nexigen_app`). Verify: binary exists when gate ON, absentee when gate OFF.

## 4. Tests

- [x] 4.1 End-to-end test: `Iceoryx2Logger` publishes frames → `LogDaemon` subscribed → frames land in a temp directory as JSON lines, app/level/message match. Verify: temp file contains expected lines.
- [x] 4.2 Level filter test: frames below `min_log_level_storage` are dropped. Verify: only messages at or above the threshold appear in the file.

## 5. Verification matrix

- [x] 5.1 Debug gate-ON: full `ctest` green.
- [x] 5.2 Release `-O3 -march=native -flto` + `NEXIGEN_STRICT_LATENCY_TESTS=ON`: full `ctest` green.
- [x] 5.3 TSAN + ASAN gate-ON: full `ctest` green.

## Workflow follow-up

- Network sink + crash-ring — separate change.
- Archive the change after the project's review requirements are satisfieed.