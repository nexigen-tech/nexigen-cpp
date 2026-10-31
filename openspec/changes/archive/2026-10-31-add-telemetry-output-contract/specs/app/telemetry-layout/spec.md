# Spec Delta

## Purpose

Defines the run/instance identity a nexigen application receives from its environment and the rules that resolve metrics, tracing, and chrono sink paths from explicit settings or that identity, so a local observatory can attribute and locate every stream without per-application code.

## ADDED Requirements

### Requirement: Run identity is read from a fixed environment contract

The system SHALL read run identity only from these environment variables: `NEXIGEN_RUN_ID`, `NEXIGEN_RUN_DIR`, `NEXIGEN_APP_NAME`, `NEXIGEN_INSTANCE_ID`, and `NEXIGEN_INSTANCE_DIR`. Unset or empty variables SHALL leave the corresponding `Run_identity` field empty; the loader MUST NOT consult the current directory, hostname, process id, or any other implicit source.

#### Scenario: Complete environment populates every field

- **WHEN** all five variables are set to non-empty values
- **THEN** the loaded `Run_identity` carries exactly those run, directory, app, and instance values

#### Scenario: Missing variables do not fail the load

- **WHEN** no identity variable is set
- **THEN** the loader returns a `Run_identity` with empty fields and does not report an error or diagnostic

#### Scenario: Partial environment is preserved

- **WHEN** only `NEXIGEN_APP_NAME` and `NEXIGEN_INSTANCE_ID` are set
- **THEN** those two fields are populated and the other fields remain empty

### Requirement: Instance directory is derived when not supplied

When `NEXIGEN_INSTANCE_DIR` is empty and `NEXIGEN_RUN_DIR`, `NEXIGEN_APP_NAME`, and `NEXIGEN_INSTANCE_ID` are all non-empty, the system SHALL derive the instance directory as `<run_dir>/<app_name>/<instance_id>`. An explicit `NEXIGEN_INSTANCE_DIR` SHALL take precedence over the derived value.

#### Scenario: Derived instance directory

- **WHEN** the run directory is `/var/nexigen/runs/r1`, the app is `controller`, and the instance is `i42`
- **THEN** the resolved instance directory is `/var/nexigen/runs/r1/controller/i42`

#### Scenario: Explicit instance directory wins

- **WHEN** `NEXIGEN_INSTANCE_DIR` is set together with the three derivation inputs
- **THEN** the explicit value is kept and no derivation occurs

#### Scenario: Incomplete inputs leave the directory empty

- **WHEN** `NEXIGEN_RUN_DIR` or `NEXIGEN_INSTANCE_ID` is empty and no explicit instance directory is set
- **THEN** the resolved instance directory remains empty

### Requirement: Identity validation rejects unsafe values

The system SHALL validate a populated `Run_identity`: a present run id, app name, or instance id SHALL be non-empty, at most 128 bytes, and limited to `A-Z`, `a-z`, `0-9`, `.`, `_`, and `-`; a present run directory or instance directory SHALL be an absolute path. A completely empty identity SHALL validate successfully for observed applications that do not use the contract.

#### Scenario: Safe identity validates

- **WHEN** every present id uses only the allowed characters and both directories are absolute
- **THEN** validation returns success

#### Scenario: Path separator in an id is rejected

- **WHEN** an app name contains `/` or an instance id contains `..`
- **THEN** validation returns `ErrorCode::InvalidArgument`

#### Scenario: Relative directory is rejected

- **WHEN** a present run directory or instance directory is relative
- **THEN** validation returns `ErrorCode::InvalidArgument`

#### Scenario: Empty identity is valid

- **WHEN** every field is empty
- **THEN** validation returns success and no identity is imposed on the application

### Requirement: Sink paths resolve from settings, then instance identity

The system SHALL resolve metrics, tracing, and chrono sink paths independently. For each signal, a non-empty explicit settings sink path SHALL win. Otherwise, when a validated instance directory is available, the resolver SHALL use `<instance_dir>/metrics.jsonl`, `<instance_dir>/traces.jsonl`, or `<instance_dir>/chrono.jsonl` respectively. Otherwise the resolved path SHALL be empty, meaning the helper does not configure that sink.

#### Scenario: Explicit settings path wins

- **WHEN** metrics settings set `sink.path` to `/tmp/run/metrics.jsonl` and an instance directory is also available
- **THEN** the resolved metrics path is `/tmp/run/metrics.jsonl`

#### Scenario: Instance fallback fills an absent path

- **WHEN** no signal has an explicit path and the instance directory is `/tmp/run/controller/i42`
- **THEN** the resolved paths are `/tmp/run/controller/i42/metrics.jsonl`, `/tmp/run/controller/i42/traces.jsonl`, and `/tmp/run/controller/i42/chrono.jsonl`

#### Scenario: Signals resolve independently

- **WHEN** tracing has an explicit path while metrics and chrono do not
- **THEN** tracing keeps its explicit path and metrics and chrono use their instance fallbacks

#### Scenario: No identity and no explicit path yields empty paths

- **WHEN** the identity is empty and no sink path is configured
- **THEN** every resolved path is empty and the application remains free to choose its own sink policy

### Requirement: Invalid identity fails path resolution

`resolve_telemetry_paths` SHALL return `ErrorCode::InvalidArgument` and SHALL leave the output paths unmodified when the supplied `Run_identity` does not validate. The resolver MUST NOT create directories, open files, or write bytes.

#### Scenario: Invalid identity is rejected before resolution

- **WHEN** the identity contains a relative run directory or an unsafe id
- **THEN** the resolver returns `InvalidArgument` and no path is populated

#### Scenario: Resolution has no I/O side effects

- **WHEN** valid identity and settings are resolved against a non-existent directory
- **THEN** the call succeeds with derived path strings and no directory or file is created

### Requirement: Identity and resolved paths are serializable

`Run_identity` and `Telemetry_paths` SHALL provide member and free `to_string()`, non-throwing ADL `to_json`/`from_json` under `JSON_NOEXCEPTION=1`, and a `std::formatter` specialization whose rendering equals `to_string()`.

#### Scenario: Run identity round-trips through JSON

- **WHEN** a populated `Run_identity` passes through `to_json` then `from_json`
- **THEN** every identity field equals its original value

#### Scenario: Telemetry paths round-trip through JSON

- **WHEN** a populated `Telemetry_paths` passes through `to_json` then `from_json`
- **THEN** the metrics, traces, and chrono paths equal their originals

#### Scenario: Hostile JSON leaves identity unchanged

- **WHEN** `from_json` receives a non-object, wrong field types, or extra fields
- **THEN** the target keeps its previous values, no exception escapes under `JSON_NOEXCEPTION=1`, and no field is corrupted

#### Scenario: Values format through std::format

- **WHEN** `std::format("{}", value)` is evaluated for a loaded `Run_identity` and a resolved `Telemetry_paths`
- **THEN** each result equals the corresponding `to_string()` text

### Requirement: Telemetry layout is cold path only

Identity loading, validation, path resolution, and all serialization SHALL execute before workers start and SHALL NOT be called from a tick, producer, or sink callback. The loader and resolver MAY allocate `std::string`; steady-state execution SHALL retain the existing zero-allocation and lock-free guarantees.

#### Scenario: Tick path remains untouched

- **WHEN** an application resolves its layout before initialization and then runs its tick loop
- **THEN** the tick loop performs no identity lookup, path resolution, JSON access, or allocation attributable to this capability
