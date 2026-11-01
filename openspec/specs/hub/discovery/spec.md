# hub/discovery Specification

## Purpose

Defines how `nexigen-hub` discovers run roots, suites, applications, instances, attempts, and artifact streams from the filesystem, maintains a live tree through inotify, and derives read-only lifecycle state from existing runner metadata.

## Requirements

### Requirement: Run roots are explicit and repeatable

`nexigen-hub` SHALL accept one or more `--run-dir` values and SHALL discover runs only below those roots. It MUST NOT consult the current directory, environment variables, or any implicit source. Duplicate roots SHALL be collapsed, and a missing, relative, or non-directory root SHALL fail startup with `InvalidArgument`.

#### Scenario: One run root is scanned

- **WHEN** the hub starts with one absolute `--run-dir`
- **THEN** only suites and instances below that root are reported

#### Scenario: Several roots are merged

- **WHEN** the hub starts with two different run roots
- **THEN** the tree contains entries from both and duplicate roots appear once

#### Scenario: Invalid root fails startup

- **WHEN** a root is relative, missing, or not a directory
- **THEN** startup returns `InvalidArgument` and no HTTP listener is opened

#### Scenario: No implicit discovery

- **WHEN** no `--run-dir` is supplied
- **THEN** the hub reports a usage error and does not scan the current directory

### Requirement: Initial scan builds the run tree

On startup the hub SHALL recursively scan every run root and build a tree of run → suite → app → instance → attempt. It SHALL read suite metadata from `<run_dir>/.suite/<suite_id>/suite.json`, instance metadata from `resource.json`/`exit.json`, and attempt directories from the suite `artifacts_path`.

#### Scenario: Existing suite is discovered

- **WHEN** a run root contains `.suite/hil/suite.json`
- **THEN** the tree contains one `hil` suite with its recorded applications

#### Scenario: Instances and attempts are attached

- **WHEN** an application instance has `resource.json` and attempt directories
- **THEN** the tree reports the instance and every discovered attempt in order

#### Scenario: Unrelated files are ignored

- **WHEN** a run root contains files that are not part of a suite or instance layout
- **THEN** they do not create tree entries or streams

### Requirement: Inotify keeps the tree live

The hub SHALL watch each run root, suite metadata directory, instance directory, and artifact attempt tree with inotify. Creates, deletes, and moves SHALL update the tree without restarting the hub. When the inotify queue overflows, the hub SHALL rescan the affected roots and reconcile the tree.

#### Scenario: New suite appears

- **WHEN** a new `.suite/<suite_id>/suite.json` is created after startup
- **THEN** the suite appears in the tree without a restart

#### Scenario: New attempt appears

- **WHEN** a new `attempt-0002` directory is created
- **THEN** it is attached to the instance and its streams become available

#### Scenario: Removal is reflected

- **WHEN** a suite or instance directory is removed
- **THEN** its tree entries and streams disappear

#### Scenario: Queue overflow triggers a rescan

- **WHEN** inotify reports `IN_Q_OVERFLOW`
- **THEN** the hub scans the watched roots again and reconciles additions and removals

### Requirement: Artifact roots come from suite metadata

The hub SHALL read `artifacts_path` from each `suite.json` and watch that root even when it lies outside the run root. Missing, unreadable, or invalid artifact metadata SHALL leave the suite visible with no streams rather than failing the hub.

#### Scenario: External artifact root is followed

- **WHEN** `suite.json` records an artifact root outside the run directory
- **THEN** the hub discovers and tails attempts below that root

#### Scenario: Missing artifact root is tolerated

- **WHEN** the recorded artifact root does not exist
- **THEN** the suite remains in the tree with no streams and no startup failure

#### Scenario: Artifact root changes are observed

- **WHEN** an attempt directory is created after the suite was discovered
- **THEN** the hub attaches it after the inotify notification or a rescan

### Requirement: State is derived from runner files

The hub SHALL report suite and application state from `suite-state.json`, `suite-exit.json`, `resource.json`, and `exit.json`, using the same state vocabulary as the runner. It SHALL report `unknown` when no usable record exists and MUST NOT infer state from process names or command lines.

#### Scenario: Running suite is reported

- **WHEN** suite state and instance metadata indicate live applications
- **THEN** the suite and those applications are reported `running`

#### Scenario: Failed application is reported

- **WHEN** suite state records a permanently failed application
- **THEN** that application is reported `failed` and the aggregate state is reported as recorded

#### Scenario: Stopped suite is reported

- **WHEN** `suite-exit.json` records `stopped`
- **THEN** the aggregate state is `stopped`

#### Scenario: Missing records are unknown

- **WHEN** no readable state or exit record exists for an instance
- **THEN** its state is `unknown`

### Requirement: Discovery is read-only

The hub SHALL NOT create, modify, rename, or delete files under run roots or artifact roots. It SHALL only scan, watch, and read them.

#### Scenario: Run tree is unchanged by observation

- **WHEN** the hub scans and watches a populated run tree
- **THEN** no new, modified, or deleted files appear under the run or artifact roots

#### Scenario: No control actions are performed

- **WHEN** an operator views a suite or stream
- **THEN** the hub does not start, stop, or signal any supervised process

### Requirement: Discovery types follow the serialization contract

The hub discovery structures (`Hub_stream`, `Hub_attempt`, `Hub_instance`, `Hub_app`, `Hub_suite`, `Hub_run`, `Hub_tree`) and the `Stream_kind` enum SHALL provide `to_string` member/free functions, `std::formatter`, and non-throwing ADL `to_json`/`from_json` under `JSON_NOEXCEPTION=1`. Hostile JSON SHALL leave targets unchanged.

#### Scenario: Stream kind round-trips through text and JSON

- **WHEN** every `Stream_kind` value passes through `to_string`, formatter, `to_json`, and `from_json`
- **THEN** the original value and canonical lowercase name are recovered

#### Scenario: Tree structures round-trip

- **WHEN** populated hub tree structures pass through `to_json` then `from_json`
- **THEN** every field equals its original value

#### Scenario: Hostile tree JSON is ignored

- **WHEN** `from_json` receives an array or wrong field types
- **THEN** the target keeps its previous values and no exception escapes

### Requirement: Discovery is cold path only

Scanning, inotify handling, state derivation, and tree updates SHALL execute in the hub process. They MUST NOT be called from an application tick, producer, snapshot, or sink callback and MUST NOT add work to application hot paths.

#### Scenario: Application hot paths are untouched

- **WHEN** a supervised application runs its tick loop
- **THEN** no hub scan, inotify read, or state derivation occurs in that loop

#### Scenario: Hub is a separate process

- **WHEN** the hub is running
- **THEN** its pid differs from every application and runner supervisor pid
