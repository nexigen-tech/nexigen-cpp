# Spec Delta

## MODIFIED Requirements

### Requirement: Suite manifest is validated before launch

Manifest validation SHALL complete before any process starts. A malformed document, wrong `version`, missing required field, empty `apps`, duplicate `(app, instance)`, unsafe `suite_id`/`app`/`instance`, relative `run_dir` or `artifacts_path`, non-absolute `global_settings_path`, absolute or `..`-containing `local_settings_path`, unknown impact value, unknown log level, negative count or delay, or invalid application CPU set MUST return `InvalidArgument` and start nothing. Application `cpus` SHALL parse into the shared `CpuSet` representation; duplicate, offline, negative, and unrepresentable IDs SHALL be rejected before launch.

#### Scenario: Duplicate application identity is rejected

- **WHEN** two entries share the same `app` and `instance`
- **THEN** validation fails with `InvalidArgument` and no process is created

#### Scenario: Unknown impact value is rejected

- **WHEN** an entry sets `impact_on_app_suite_operability` to a value other than `failure`, `degrade`, or `not_impact`
- **THEN** validation fails with `InvalidArgument`

#### Scenario: Unsafe identity or relative run directory is rejected

- **WHEN** `suite_id`, `app`, or `instance` contains path-unsafe characters, or `run_dir` or `artifacts_path` is not absolute
- **THEN** validation fails with `InvalidArgument`

#### Scenario: Invalid settings paths are rejected

- **WHEN** a suite or application `global_settings_path` is not absolute, or a `local_settings_path` is absolute or contains a `..` segment
- **THEN** validation fails with `InvalidArgument`

#### Scenario: Unknown log level is rejected

- **WHEN** the suite or an application sets `log_level` to a value other than the canonical lowercase logging levels
- **THEN** validation fails with `InvalidArgument`

#### Scenario: CPU index outside the online set is rejected

- **WHEN** `cpus` contains a negative index, a duplicate, or an index greater than or equal to the online CPU count
- **THEN** validation fails with `InvalidArgument`

#### Scenario: CPU above 63 is not rejected merely for its width

- **WHEN** `cpus` contains an online, permitted CPU above 63
- **THEN** validation accepts it and preserves it in the application's `CpuSet`

#### Scenario: Validation failure starts nothing

- **WHEN** any validation rule fails
- **THEN** no instance directory, artifact directory, supervisor, or child process is created

### Requirement: Manifest launch settings reach each child

Each manifest application SHALL be launched with exactly its `args`, its `env` additions, an idle timeout of `idle_timeout_s`, and CPU affinity restricted to its `CpuSet` when non-empty. Identity variables `NEXIGEN_*` SHALL take precedence over manifest `env`. An affinity request the kernel rejects SHALL be reported as that application's launch failure. Runner SHALL apply the supplied set without calculating scheduling policy or priority.

#### Scenario: Arguments are passed exactly

- **WHEN** an entry supplies arguments containing spaces, quotes, and option-like text
- **THEN** the child receives the same argument vector without shell interpretation

#### Scenario: Environment additions are visible to the child

- **WHEN** an entry supplies `env` entries
- **THEN** the child observes them in its environment

#### Scenario: Identity variables take precedence

- **WHEN** `env` also sets a `NEXIGEN_*` identity variable
- **THEN** the child observes the runner-assigned identity value

#### Scenario: CPU affinity is restricted

- **WHEN** an entry supplies a non-empty CPU set
- **THEN** the child's allowed CPU set equals that set

#### Scenario: Rejected affinity is a launch failure

- **WHEN** the kernel rejects the requested affinity at launch
- **THEN** that application is reported as failed to launch and its impact policy applies

#### Scenario: Idle timeout terminates the application

- **WHEN** an entry sets a non-zero `idle_timeout_s` and the application outlives it
- **THEN** the application is terminated through the normal stop sequence and its impact policy applies
