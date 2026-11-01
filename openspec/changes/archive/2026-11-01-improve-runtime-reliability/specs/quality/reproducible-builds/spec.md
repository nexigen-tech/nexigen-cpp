# Spec Delta

## Purpose

Defines the reproducible dependency download and portable Release build
guarantees of the nexigen build system.

## ADDED Requirements

### Requirement: Downloaded dependencies are content-pinned

When the build downloads a dependency archive, it SHALL verify a cryptographic
content hash before extracting or using it.

#### Scenario: Correct archive is accepted

- **WHEN** the pinned `nlohmann/json` archive is downloaded during configuration
- **THEN** its SHA-256 digest matches the declared `URL_HASH` and configuration proceeds

#### Scenario: Tampered archive is rejected

- **WHEN** a downloaded archive's content does not match the declared hash
- **THEN** CMake configuration fails instead of extracting or building the archive

### Requirement: Portable Release mode

The build SHALL offer a portable Release mode that removes host-specific CPU
architecture flags while keeping Release optimisation and link-time
optimisation.

#### Scenario: Native mode remains the default

- **WHEN** the project is configured for Release without extra options
- **THEN** the compiler receives the native architecture flags used for dedicated test hardware

#### Scenario: Portable mode removes host-specific flags

- **WHEN** the project is configured with `-DNEXIGEN_NATIVE_ARCH=OFF` in Release
- **THEN** `-march=native` and `-mtune=native` are absent from every compiled target while `-O3` remains

#### Scenario: Portable binaries run on another CPU

- **WHEN** a portable Release build is produced on one machine and executed on a different x86-64 machine without the build host's instruction set
- **THEN** the binaries load and the unit tests pass
