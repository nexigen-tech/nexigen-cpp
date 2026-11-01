# Design

## Context

`docs/project-analysis.md` records:

- four Clang 21 diagnostics under `-Werror`;
- missing `tracing`/`runner`/`hub` documentation and stale build claims;
- no `install(`, no export set, no package config.

The library consists of one INTERFACE target (`nexigen_core`) and eight STATIC
targets. All public headers live under `include/nexigen/`, and every target
already declares the install interface `$<INSTALL_INTERFACE:include>`.

## Goals / Non-Goals

**Goals:**

- GCC and Clang both build all targets with the existing warning set and
  `-Werror`; CI proves it.
- `cmake --install` produces a self-contained tree: headers, archives, tools,
  package config, version file, and targets file.
- `find_package(nexigen CONFIG REQUIRED)` works from an external CMake project
  and links every module target.
- README and architecture describe the real module set, build matrix, and
  installation path.

**Non-Goals:**

- Raising runner/hub coverage (P2).
- Logging rescan/sync failures (P2).
- Adding a portable `-march` mode or pinning the FetchContent hash (P2).
- Installing the FetchContent copy of nlohmann/json or vendoring it into the
  package.

## Decisions

### D1 — Standard GNU install layout

Use `GNUInstallDirs` with `CMAKE_INSTALL_INCLUDEDIR`, `CMAKE_INSTALL_LIBDIR`,
`CMAKE_INSTALL_BINDIR`, and `CMAKE_INSTALL_LIBDIR/cmake/nexigen` for the package
files. This keeps distributions and `DESTDIR` packaging conventional.

### D2 — One export set, namespace `nexigen::`

All nine library targets go into `nexigenTargets` with `EXPORT_NAME` matching
the alias (`core`, `chrono`, ...). Consumers therefore use the same
`nexigen::<module>` names they use in-tree; no second naming scheme.

### D3 — Version compatibility is same-minor

The project is `0.1.0`; semantic-version rules for pre-1.0 releases do not
promise compatibility across minors. `SameMinorVersion` is generated, so
`0.1.x` is accepted and `0.2` is rejected. This matters once the package reaches
consumers and the project starts a `0.2` line.

### D4 — Package config resolves dependencies explicitly

`nexigenConfig.cmake.in` calls `find_dependency(Threads)` and
`find_dependency(nlohmann_json)` before including the targets file. The static
archives carry `Threads::Threads` as a link-only dependency, and the INTERFACE
target `nexigen_core` links `nlohmann_json::nlohmann_json`; without
`find_dependency` the imported targets would be unusable.

When the project is consumed with `add_subdirectory`, the FetchContent fallback
still applies. When it is consumed as an installed package, `nlohmann_json` must
be installed on the system; this is documented in README.

### D5 — Tools install only when built

`nexigen-run` and `nexigen-hub` are installed to `bin` when
`NEXIGEN_BUILD_TOOLS=ON`. They are not part of the export set because they are
executables, not link targets.

### D6 — Clang CI pairs the compiler with libstdc++ 15

`std::expected` is a C++23 library feature; Ubuntu 24.04 ships libstdc++ 13,
which lacks it. A distro-only Clang 18 image therefore fails on the headers
even though the compiler is modern. The Clang CI job runs in the same `gcc:15`
container as the other builds and installs the distribution `clang` package
(19.x on that image), so the compiler and the standard library are both
C++23-complete. Documentation states the real requirement: a C++23 standard
library, not just a compiler version.

### D7 — Example uses the mandated free `to_string`

`ex_spsc_ring_buffer.cpp` defined the ADL free `to_string(TelemetryPacket)` but
never called it, which Clang reports as an unused function. The example now
prints one sampled packet through that function, demonstrating the intended
mandate-9 contract instead of suppressing the diagnostic.

## Risks / Trade-offs

- **Dependency availability:** installed consumers must have `nlohmann_json`
  discoverable. The README states this and `find_dependency` fails with a clear
  message.
- **Clang/libstdc++ pairing:** the build requires a C++23 standard library for
  `std::expected`. CI pairs Clang with libstdc++ 15; a Clang 18 toolchain on
  Ubuntu 24.04 (libstdc++ 13) cannot compile the headers regardless of compiler
  version.
- **Version pinning:** `SameMinorVersion` is stricter than `SameMajorVersion`;
  it is the safer default for a pre-1.0 library.
