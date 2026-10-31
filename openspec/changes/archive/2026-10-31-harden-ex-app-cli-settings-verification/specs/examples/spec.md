# Spec Delta

## MODIFIED Requirements

### Requirement: CTest smoke coverage for every example

The test configuration SHALL register one CTest test per example executable, using the executable name as the test name, so that a normal `ctest` run builds confidence that the documented API links and executes. It SHALL additionally register argument-driven smoke variants under `<executable>_<variant>` test names when an example exposes behavior worth exercising in isolation; `ex_app_cli_settings` SHALL register `ex_app_cli_settings_help`, which runs the example with `--help`.

#### Scenario: Examples run under ctest

- **WHEN** `ctest` is run after a default build
- **THEN** each example test executes its executable and passes only if the process exits with status 0

#### Scenario: Argument-driven smoke variants run under ctest

- **WHEN** `ctest` is run after a default build
- **THEN** `ex_app_cli_settings_help` executes `ex_app_cli_settings --help` and passes only if the process exits with status 0

#### Scenario: Disabled examples register no tests

- **WHEN** `NEXIGEN_BUILD_EXAMPLES=OFF` and `ctest` is run
- **THEN** no example test or argument-driven variant appears in the test list and the remaining library tests are unaffected
