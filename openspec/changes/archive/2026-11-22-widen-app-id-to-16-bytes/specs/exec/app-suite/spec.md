# Spec Delta

## MODIFIED Requirements

### Requirement: Suite manifest has a fixed schema

The manifest SHALL be a JSON object with `version: 1`, `suite_id`, `run_id`, absolute `run_dir` and `artifacts_path`, optional absolute `global_settings_path`, optional `execution_manager_name`, `log_level`, `suite_restart_attempts`, `suite_restart_delay_s`, and a non-empty `apps` array. Each entry SHALL define its identity, command and arguments, impact and restart policy, timeouts, an optional `app_id`, and optional environment, affinity, settings paths, and log level. When `app_id` is omitted the application name SHALL be used as the identifier.

#### Scenario: Complete manifest is accepted

- **WHEN** every required suite and application field is supplied with a valid value
- **THEN** the manifest is accepted and each application carries exactly the supplied command, arguments, environment, CPU list, timeouts, restart counts, settings paths, log level, impact level, and identifier

#### Scenario: Optional fields default deterministically

- **WHEN** an application omits `env`, `cpus`, `global_settings_path`, `local_settings_path`, `log_level`, and `app_id`
- **THEN** it launches with no additional environment entries, no CPU restriction, the suite global settings path, no local settings source, the suite log level, and its application name as identifier

#### Scenario: Explicit identifier overrides the application name

- **WHEN** an entry sets `app: control_loop` and `app_id: CN_LOOP_1`
- **THEN** the resolved application identifier is `CN_LOOP_1`, not `control_loop`

#### Scenario: ExecutionManager name is optional

- **WHEN** the manifest omits `execution_manager_name`
- **THEN** the execution_manager uses its default execution_manager name for its own telemetry

#### Scenario: Unknown fields are ignored

- **WHEN** the manifest contains fields the execution_manager does not recognize
- **THEN** parsing succeeds and known fields keep their supplied values

### Requirement: Suite manifest is validated before launch

Manifest validation SHALL complete before any process starts. A malformed document, wrong `version`, missing required field, empty `apps`, duplicate `(app, instance)`, duplicate `app_id`, unsafe `suite_id`/`app`/`instance`, relative `run_dir` or `artifacts_path`, non-absolute `global_settings_path`, absolute or `..`-containing `local_settings_path`, unknown impact value, unknown log level, negative count or delay, invalid `app_id`, or invalid application CPU set MUST return `InvalidArgument` and start nothing. Application `cpus` SHALL parse into the shared `CpuSet` representation; duplicate, offline, negative, and unrepresentable IDs SHALL be rejected before launch.

#### Scenario: Duplicate application identity is rejected

- **WHEN** two entries share the same `app` and `instance`
- **THEN** validation fails with `InvalidArgument` and no process is created

#### Scenario: Duplicate identifier is rejected

- **WHEN** two entries resolve to the same `app_id`
- **THEN** validation fails with `InvalidArgument` and no process is created

#### Scenario: Identifier longer than sixteen bytes is rejected

- **WHEN** an entry sets `app_id` to a 17-byte value, or omits it while its application name is
  longer than 16 bytes
- **THEN** validation fails with `InvalidArgument`, the offending entry is named, and the value is
  not truncated

#### Scenario: Identifier with forbidden characters is rejected

- **WHEN** an `app_id` contains a space, quote, backslash, slash, dot, control byte, or non-ASCII
  byte
- **THEN** validation fails with `InvalidArgument`

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

Each manifest application SHALL be launched with exactly its `args`, its `env` additions, an idle timeout of `idle_timeout_s`, and CPU affinity restricted to its `CpuSet` when non-empty. The resolved identifier SHALL be delivered to the child as `--app-id <id>` in argv, next to `--app-name <app>`. Identity variables `NEXIGEN_*` SHALL take precedence over manifest `env`. An affinity request the kernel rejects SHALL be reported as that application's launch failure. ExecutionManager SHALL apply the supplied set without calculating scheduling policy or priority.

#### Scenario: Arguments are passed exactly

- **WHEN** an entry supplies arguments containing spaces, quotes, and option-like text
- **THEN** the child receives the same argument vector without shell interpretation

#### Scenario: Environment additions are visible to the child

- **WHEN** an entry supplies `env` entries
- **THEN** the child observes them in its environment

#### Scenario: Resolved identifier is passed via argv

- **WHEN** an entry resolves to identifier `CN_LOOP_1`
- **THEN** the child's argv contains `--app-id CN_LOOP_1` and `--app-name <app>`

#### Scenario: Identity variables take precedence

- **WHEN** `env` also sets a `NEXIGEN_*` identity variable
- **THEN** the child observes the execution_manager-assigned identity value

#### Scenario: CPU affinity is restricted

- **WHEN** an entry supplies a non-empty CPU set
- **THEN** the child's allowed CPU set equals that set

#### Scenario: Rejected affinity is a launch failure

- **WHEN** the kernel rejects the requested affinity at launch
- **THEN** that application is reported as failed to launch and its impact policy applies

#### Scenario: Idle timeout terminates the application

- **WHEN** an entry sets a non-zero `idle_timeout_s` and the application outlives it
- **THEN** the application is terminated through the normal stop sequence and its impact policy applies

## ADDED Requirements

### Requirement: Identifier uniqueness is scoped to the suite

Identifier uniqueness SHALL be enforced only inside one suite. Suite admission SHALL NOT consult
other suites, SHALL NOT reserve identifiers globally, and SHALL NOT fail because another running
suite uses the same identifier. Two suites MAY reuse an identifier; their streams are then
indistinguishable to a daemon, which is an operator choice rather than an admission error.

#### Scenario: Duplicate identifiers fail suite validation

- **WHEN** two application entries of the same manifest resolve to `CN_LOOP_1`
- **THEN** validation fails with `InvalidArgument` and no supervisor or child is started

#### Scenario: Another suite may reuse the identifier

- **WHEN** suite A is running with `CN_LOOP_1` and suite B is admitted with its own `CN_LOOP_1`
- **THEN** suite B starts normally and no `Conflict` is reported
