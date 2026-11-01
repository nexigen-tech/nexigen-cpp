# Tasks

## 1. Extract the shared parser

- [x] 1.1 Add header-only `tools/detail/cli.hpp` exposing `nexigen::tools::parse_u64`; verify it compiles standalone
- [x] 1.2 Update `tools/nexigen-run/main.cpp` to drop its local `parse_uint` and call `nexigen::tools::parse_u64` instead; verify the tool builds
- [x] 1.3 Update `tools/nexigen-hub/main.cpp` to drop its local `parse_u64` and call the shared helper instead; verify the tool builds
- [x] 1.4 Add `target_include_directories(... PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})` for both tools in `tools/CMakeLists.txt`; verify configure succeeds

## 2. Verify

- [x] 2.1 Build both `nexigen-run` and `nexigen-hub` and run their help smoke tests (`nexigen_run_help`, `nexigen_hub_help`); verify 100% pass
- [x] 2.2 Build both tools across the build-mode matrix — Debug, Release (`-O3 -march=native -flto`), ThreadSanitizer (`-DNEXIGEN_ENABLE_TSAN=ON`), AddressSanitizer (`-DNEXIGEN_ENABLE_ASAN=ON`) — and run the tool help tests; verify no sanitizer report
- [x] 2.3 Sanity-check `nexigen-hub` still parses a numeric option and rejects a non-numeric one (e.g. `--ring-records abc` fails with usage); verify behavior is unchanged

## Workflow follow-up

- Archive the change after review requirements are satisfied (skip_specs, so no spec sync occurs).
