# Spec Delta

## MODIFIED Requirements

### Requirement: Run identity is read from a fixed environment contract

The system SHALL read run identity only from these environment variables: `NEXIGEN_RUN_ID`, `NEXIGEN_RUN_DIR`, `NEXIGEN_INSTANCE_ID`, and `NEXIGEN_INSTANCE_DIR`. Unset or empty variables SHALL leave the corresponding `Run_identity` field empty; the loader MUST NOT consult the current directory, hostname, process id, or any other implicit source. The application name is not part of run identity and SHALL be supplied separately (see `app/settings`).

#### Scenario: Complete environment populates every field

- **WHEN** all four variables are set to non-empty values
- **THEN** the loaded `Run_identity` carries exactly those run, directory, and instance values

#### Scenario: Missing variables do not fail the load

- **WHEN** no identity variable is set
- **THEN** the loader returns a `Run_identity` with empty fields and does not report an error or diagnostic

#### Scenario: Partial environment is preserved

- **WHEN** only `NEXIGEN_INSTANCE_ID` is set
- **THEN** that field is populated and the other fields remain empty

### Requirement: Instance directory is derived when not supplied

When `NEXIGEN_INSTANCE_DIR` is empty and `NEXIGEN_RUN_DIR`, `NEXIGEN_INSTANCE_ID`, and a supplied application name are all non-empty, the system SHALL derive the instance directory as `<run_dir>/<app_name>/<instance_id>`. An explicit `NEXIGEN_INSTANCE_DIR` SHALL take precedence over the derived value.

#### Scenario: Derived instance directory

- **WHEN** the run directory is `/var/nexigen/runs/r1`, the supplied app name is `controller`, and the instance is `i42`
- **THEN** the resolved instance directory is `/var/nexigen/runs/r1/controller/i42`

#### Scenario: Explicit instance directory wins

- **WHEN** `NEXIGEN_INSTANCE_DIR` is set together with the derivation inputs
- **THEN** the explicit value is kept and no derivation occurs

#### Scenario: Incomplete inputs leave the directory empty

- **WHEN** `NEXIGEN_RUN_DIR` or `NEXIGEN_INSTANCE_ID` or the application name is empty and no explicit instance directory is set
- **THEN** the resolved instance directory remains empty

### Requirement: Identity validation rejects unsafe values

The system SHALL validate a populated `Run_identity`: a present run id or instance id SHALL be non-empty, at most 128 bytes, and limited to `A-Z`, `a-z`, `0-9`, `.`, `_`, and `-`; a present run directory or instance directory SHALL be an absolute path. A completely empty identity SHALL validate successfully for observed applications that do not use the contract. The application name SHALL be validated separately where it is supplied (see `app/settings`).

#### Scenario: Safe identity validates

- **WHEN** every present id uses only the allowed characters and both directories are absolute
- **THEN** validation returns success

#### Scenario: Path separator in an id is rejected

- **WHEN** an instance id contains `..`
- **THEN** validation returns `ErrorCode::InvalidArgument`

#### Scenario: Relative directory is rejected

- **WHEN** a present run directory or instance directory is relative
- **THEN** validation returns `ErrorCode::InvalidArgument`

#### Scenario: Empty identity is valid

- **WHEN** every field is empty
- **THEN** validation returns success and no identity is imposed on the application
