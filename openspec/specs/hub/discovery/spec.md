# hub/discovery Specification

## Purpose

Defines how `nexigen-hub` discovers run roots, suites, applications, instances, attempts, and artifact streams from the filesystem, maintains a live tree through inotify, and derives read-only lifecycle state from existing execution_manager metadata.

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

### Requirement: Suite-metadata read targets are run-root contained

The hub SHALL accept filesystem reading targets that originate from
`suite.json` — `artifacts_path`, per-application `instance_dir`, and every
path derived from them (suite execution_manager log, attempt directories, stream files) —
only when the target's real path (resolved with `realpath`) lies inside the
canonical run root owned by the suite. A target that does not exist or
resolves outside the run root SHALL NOT be opened, watched, or read.

#### Scenario: External artifact root is rejected

- **WHEN** `suite.json` records an artifact root outside the run directory
  (absolute elsewhere or escaping via `..`)
- **THEN** the suite is discovered with no streams from that root, the hub
  neither reads nor watches anything outside the run root, and the hub stays
  up

#### Scenario: Escaped instance directory is rejected

- **WHEN** a suite application's `instance_dir` resolves outside the run root
- **THEN** the instance is reported with state unknown, its
  `resource.json`/`exit.json` are not read, and the suite remains visible

#### Scenario: Contained artifact root is still followed

- **WHEN** the recorded artifact root resolves to a real path inside the run
  root (the execution_manager-created default)
- **THEN** the hub discovers and tails attempts below that root

#### Scenario: Artifact root changes are observed

- **WHEN** an attempt directory is created after the suite was discovered and
  the current artifact metadata is contained in the run root
- **THEN** the hub attaches it after the inotify notification or a rescan

### Requirement: Non-contained suite metadata degrades gracefully

When a `suite.json` reading target is missing or resolves outside the run
root, the hub SHALL degrade gracefully: the suite SHALL remain visible in the
tree in its recorded state, an affected instance SHALL report unknown state,
no streams SHALL attach, and the hub SHALL NOT fail startup.

#### Scenario: Missing artifact root is tolerated

- **WHEN** the recorded artifact root does not exist
- **THEN** the suite remains in the tree with no streams and no startup
  failure

### Requirement: State is derived from execution_manager files

The hub SHALL report suite and application state from `suite-state.json`, `suite-exit.json`, `resource.json`, and `exit.json`, using the same state vocabulary as the execution_manager. It SHALL report `unknown` when no usable record exists and MUST NOT infer state from process names or command lines.

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
- **THEN** its pid differs from every application and execution_manager supervisor pid

### Requirement: Suite execution_manager log path follows the execution_manager layout

The hub SHALL resolve the suite execution_manager log stream to `<artifacts_path>/<suite_id>/execution_manager.jsonl`, the location `nexigen-exec` writes it to, so the execution_manager stream opens and is followed rather than failing with a missing file.

#### Scenario: ExecutionManager log resolves under the suite artifact directory

- **WHEN** a suite records `artifacts_path` and `suite_id`, and `nexigen-exec` has written `<artifacts_path>/<suite_id>/execution_manager.jsonl`
- **THEN** the hub reports the execution_manager stream with that exact path and follows it

#### Scenario: ExecutionManager log stream is discoverable and openable

- **WHEN** the hub discovers a suite whose execution_manager log exists under the suite artifact directory
- **THEN** the stream kind `execution_manager` is present with a non-empty path ending in `<suite_id>/execution_manager.jsonl`, and the initial stream sync succeeds without a missing-file failure

#### Scenario: Missing suite id keeps the execution_manager log unset

- **WHEN** a suite has no recorded `suite_id` or no recorded `artifacts_path`
- **THEN** no execution_manager log stream is reported for that suite
