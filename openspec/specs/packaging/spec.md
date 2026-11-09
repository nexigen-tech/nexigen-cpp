# packaging Specification

## Purpose

Defines how nexigen-cpp is installed and consumed as a CMake package: the
header and library layout, exported targets, version compatibility, transitive
dependency discovery, and optional command-line tool installation.

## Requirements

### Requirement: Installable header and library tree

The project SHALL define install rules that place every public header under
`<prefix>/<includedir>/nexigen/` and every built module archive under
`<prefix>/<libdir>/`, using the `GNUInstallDirs` conventions.

#### Scenario: Headers are installed

- **WHEN** `cmake --install <build-dir> --prefix <prefix>` runs after a default build
- **THEN** `<prefix>/include/nexigen/` contains the core, chrono, logging, metrics, tracing, diagnostics, app, execution_manager, and hub headers

#### Scenario: Archives are installed

- **WHEN** the same install runs
- **THEN** the static archives for the eight compiled modules exist in `<prefix>/lib/`

### Requirement: Exported CMake package

The project SHALL install a `nexigenTargets.cmake` export set, a
`nexigenConfig.cmake`, and a `nexigenConfigVersion.cmake`, so an external
project can locate the package with `find_package(nexigen CONFIG REQUIRED)`.

#### Scenario: External discovery succeeds

- **WHEN** a project sets `CMAKE_PREFIX_PATH` to the install prefix and calls `find_package(nexigen 0.1 CONFIG REQUIRED)`
- **THEN** configuration succeeds and the `nexigen::core`, `nexigen::chrono`, `nexigen::logging`, `nexigen::metrics`, `nexigen::tracing`, `nexigen::diagnostics`, `nexigen::app`, `nexigen::exec`, and `nexigen::hub` targets are defined

#### Scenario: Incompatible version is rejected

- **WHEN** a project requests a version whose major or minor differs from the installed one
- **THEN** `find_package` reports an incompatible version instead of accepting it

### Requirement: Transitive dependency discovery

The installed package configuration SHALL resolve `Threads` and
`nlohmann_json` with `find_dependency` before defining the imported targets.

#### Scenario: Consumer links without manual dependency setup

- **WHEN** an external project links `nexigen::logging` and includes `<nexigen/logging/console_logger.hpp>`
- **THEN** it configures, compiles, and links without the consumer calling `find_package(Threads)` or `find_package(nlohmann_json)` itself

### Requirement: Command-line tools install when built

When `NEXIGEN_BUILD_TOOLS` is `ON`, the install rules SHALL place the
`nexigen-exec` and `nexigen-hub` executables in `<prefix>/bin/`.

#### Scenario: Tools are installed

- **WHEN** the project is configured with tools enabled and installed
- **THEN** `<prefix>/bin/nexigen-exec` and `<prefix>/bin/nexigen-hub` are executable

#### Scenario: Tools are skipped when disabled

- **WHEN** the project is configured with `-DNEXIGEN_BUILD_TOOLS=OFF` and installed
- **THEN** neither executable is produced or installed
