# Tasks

## 1. Clang `-Werror` cleanliness

- [x] 1.1 Mark the unused formatter parameters in
  `include/nexigen/metrics/meter.hpp` and
  `include/nexigen/metrics/null_metric_sink.hpp` as `[[maybe_unused]]`.
  Verify: Clang compiles both headers with `-Werror`.
- [x] 1.2 Silence the intentional negative-probe members in
  `tests/metrics/metrics_test.cpp` with `[[maybe_unused]]`. Verify: Clang
  compiles the metrics suite with `-Werror`.
- [x] 1.3 Call the ADL `to_string(TelemetryPacket)` free function in
  `examples/core/ex_spsc_ring_buffer.cpp` instead of leaving it unused. Verify:
  Clang builds the example and it still exits 0.

## 2. Install and export

- [x] 2.1 Include `GNUInstallDirs`, install all public headers, and install the
  nine module targets into an export set named `nexigenTargets` with namespace
  `nexigen::`. Verify: a fresh `cmake --install` populates `include/nexigen`
  and `lib`.
- [x] 2.2 Add `cmake/nexigenConfig.cmake.in`, generate
  `nexigenConfigVersion.cmake` with `SameMinorVersion`, and install the package
  files under `lib/cmake/nexigen`. Verify: the files exist in the prefix.
- [x] 2.3 Resolve `Threads` and `nlohmann_json` via `find_dependency` in the
  config file. Verify: an external consumer links `nexigen::logging` without
  manual dependency setup.
- [x] 2.4 Install `nexigen-run` and `nexigen-hub` to `bin` when
  `NEXIGEN_BUILD_TOOLS=ON`. Verify: both executables appear in the prefix and
  are absent with the option `OFF`.

## 3. Packaging smoke test

- [x] 3.1 Add `tests/packaging/consumer/` — a standalone CMake project that
  calls `find_package(nexigen CONFIG REQUIRED)` and builds a program using
  `nexigen::logging` and `nexigen::metrics`. Verify: configure, build, and run
  succeed against a scratch install prefix.
- [x] 3.2 Document the smoke-test command in the packaging docs.

## 4. CI

- [x] 4.1 Add a Clang job to `.github/workflows/ci.yml` that runs in the
  `gcc:15` container with the distribution `clang` package and `-Werror`.
  Verify: the job configures, builds, and runs `ctest` with 100% passing.
- [x] 4.2 Run `actionlint` on the updated workflow.

## 5. Documentation

- [x] 5.1 Add `nexigen::tracing`, `nexigen::runner`, and `nexigen::hub` API
  sections to `README.md` with the public entry points, links to their detailed
  docs, and one minimal usage example each.
- [x] 5.2 Add an install/consumption section to `README.md`: install command,
  `find_package` example, dependency note (`nlohmann_json`), and the packaging
  smoke-test command.
- [x] 5.3 Remove stale README claims: the `io_uring` mention (unused), the
  "heap-only" infrastructure wording, and the module list that omits the newest
  subsystems.
- [x] 5.4 Refresh `docs/architecture.md`: packaging section, Clang support, and
  the verification matrix.
- [x] 5.5 Add P1 completion notes to `docs/project-analysis.md`.

## 6. Verification

- [x] 6.1 Build and test with GCC Debug, Release, TSan, and ASan; 100% of CTest
  tests must pass.
- [x] 6.2 Build and test with Clang Debug under `-Werror`; 100% of CTest tests
  must pass.
- [x] 6.3 Install to a scratch prefix and build/run the consumer smoke test.
- [x] 6.4 Run `openspec validate improve-distribution --strict` and
  `openspec validate --specs`.

## Workflow follow-up

- [x] 7.1 Archive the change after verification, then continue with P2
  (runner/hub coverage, control-plane error logging).
