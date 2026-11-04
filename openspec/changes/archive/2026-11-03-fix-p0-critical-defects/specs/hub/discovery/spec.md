# hub/discovery delta spec

## REMOVED Requirements

### Requirement: Artifact roots come from suite metadata

**Reason**: Security contract replaced — trusting `artifacts_path` from a
runner-writable `suite.json` lets a compromised application point the
read-only hub at arbitrary files outside the run root (P0 path-escape,
code-analysis 2026-11-03 #3).
**Migration**: Replaced by the ADDED requirements "Suite-metadata read targets
are run-root contained" and "Non-contained suite metadata degrades
gracefully" in this delta, which keep the observing/tolerating behaviors but
read only real paths inside the canonical run root.

## ADDED Requirements

### Requirement: Suite-metadata read targets are run-root contained

The hub SHALL accept filesystem reading targets that originate from
`suite.json` — `artifacts_path`, per-application `instance_dir`, and every
path derived from them (suite runner log, attempt directories, stream files) —
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
  root (the runner-created default)
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
