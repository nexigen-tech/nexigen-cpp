# quality/code-structure Specification

## Purpose

Defines the enforceable maintainability contract of the source tree: the cyclomatic-complexity ceiling and every place it is enforced (including daemon entry points and test suites), the rule that cold-path serialization of compiled modules is built once, and the guarantee that any complexity refactor preserves observable behavior, public symbols and CLI contracts.

## Requirements

### Requirement: Complexity ceiling is enforced across the maintained source tree

Every C++ function in `include/`, `src/`, `tests/` and `tools/` SHALL have a cyclomatic complexity of at most 15 as measured by lizard. The check SHALL name each violation as `file:line` and exit non-zero when at least one violation exists. Pseudo-functions produced by parsing `alignas(...)` class declarations SHALL be filtered out, and `examples/` SHALL remain out of scope.

#### Scenario: A violating function fails the check

- **WHEN** any C++ function in `include/`, `src/`, `tests/` or `tools/` exceeds complexity 15
- **THEN** the complexity check exits non-zero and prints the offending file, line and function name

#### Scenario: Daemon entry points are in scope

- **WHEN** a `main` or daemon-runner function in `tools/` exceeds complexity 15
- **THEN** the check fails, exactly as it does for a violation under `src/`

#### Scenario: alignas pseudo-functions are not reported

- **WHEN** a type is declared with `alignas(64)` and matches the analyser's pseudo-function pattern
- **THEN** the check does not report it and does not fail because of it

#### Scenario: Examples are excluded

- **WHEN** a function under `examples/` exceeds complexity 15
- **THEN** the check still passes

### Requirement: The complexity check runs with the test suite

The complexity check SHALL be executable both as a dedicated build target and as a test-suite entry, using the same threshold and the same scanned directories in both forms, so a local `ctest` run reaches the same verdict as CI.

#### Scenario: Test suite executes the check

- **WHEN** the configured test suite is run
- **THEN** a test entry executes the complexity check over `include/`, `src/`, `tests/` and `tools/` and reports that verdict

#### Scenario: Local and CI verdicts agree

- **WHEN** the dedicated build target and the test-suite entry are run on the same tree
- **THEN** both report the same pass or fail result

#### Scenario: Missing analyser is reported as skipped

- **WHEN** lizard is unavailable on the machine
- **THEN** the check reports that it was skipped and does not claim that the tree passed

### Requirement: Complexity refactors preserve observable behavior and public surface

Every function decomposed by a complexity refactor SHALL behave identically: the same inputs produce the same outputs, error codes, emitted text and exit codes, and no public header of an affected module SHALL gain or lose a symbol. The JSON field names, nesting and accept/reject semantics of every touched serialized type SHALL be unchanged for input that was accepted before the change.

#### Scenario: Existing tests pass unchanged

- **WHEN** the existing unit, latency and integration tests of the affected modules run after decomposition
- **THEN** they pass without modification to their test bodies

#### Scenario: Public symbol set is unchanged

- **WHEN** the exported symbols of each affected compiled module are compared before and after the decomposition
- **THEN** no symbol has been added to or removed from its public surface

#### Scenario: Previously accepted JSON is still accepted

- **WHEN** a document that parsed successfully before the decomposition is parsed again
- **THEN** every field receives the same value as before the change

#### Scenario: Daemon command line is unchanged

- **WHEN** each daemon is invoked with `--help`, with `--config <path>`, without arguments, and with an unknown flag
- **THEN** it produces the same stdout, stderr and exit code as before the refactor

### Requirement: Test suites stay within the ceiling without losing coverage

After refactoring, every test function in `tests/` SHALL have complexity at most 15, the set of registered test names of each test binary SHALL be unchanged, and the total number of assertions SHALL not decrease.

#### Scenario: Post-refactor lizard report on tests

- **WHEN** lizard is run over `tests/`
- **THEN** no test function exceeds complexity 15

#### Scenario: Registered test names are preserved

- **WHEN** a refactored test binary is run
- **THEN** it executes the same set of registered tests as before the refactor, and reports success

#### Scenario: Assertions are not dropped

- **WHEN** the assertion count of a refactored test file is compared with the pre-refactor file
- **THEN** it is greater than or equal to the previous count

### Requirement: Cold-path serialization of compiled modules is built once

For every module built as a compiled library, a non-template, non-`constexpr` public definition that requires no inlining — in particular cold-path `to_string`, `to_json` and `from_json` bodies of configuration and manifest types — SHALL be defined in the module's `src/<module>/` directory rather than inline in its public header. Template code, `constexpr` code, hot-path inline functions, `std::formatter` specializations and the header-only `nexigen::core` interface target are exempt.

#### Scenario: Audit finds no eligible header definition

- **WHEN** the maintainability audit is run over the public headers of the compiled modules
- **THEN** it reports no cold-path serialization definition that is eligible to be compiled out of line and still resides in a header

#### Scenario: Header-only core stays header-only

- **WHEN** `nexigen::core` is inspected
- **THEN** it remains an interface target with no new object files and no new mandatory source file

#### Scenario: Consumers still link and round-trip

- **WHEN** a consumer translation unit includes a header whose definitions moved to `src/<module>/` and links the module
- **THEN** it compiles, links, and the affected types still round-trip through JSON with `to_string()` and `std::format` output unchanged
