# examples/suite-demo Specification

## Purpose
Defines the runnable multi-application suite demonstration that launches several `Infrastructure`-based applications through `nexigen-run` and exercises the suite supervisor's restart, degrade, and whole-suite-failure policy end to end.

## Requirements

### Requirement: Single configurable suite application executable

The project SHALL provide a self-contained executable `ex_suite_app` under `examples/suite/` with its own `main` and no test-harness dependency, that initializes `nexigen::app::Infrastructure` through the command-line settings path. In its default worker mode it SHALL run a bounded workload and exit 0, and it SHALL be registered as a CTest smoke test named after the executable and labeled `examples`.

#### Scenario: Default worker mode runs to exit zero

- **WHEN** `ex_suite_app` is built and launched directly (or via CTest) with a valid settings directory and no failure-mode environment
- **THEN** it completes a bounded workload, writes its telemetry streams, and exits 0

#### Scenario: Self-contained and harness-free

- **WHEN** `ex_suite_app` is run outside any test runner
- **THEN** it creates its own Infrastructure from the supplied settings, requires no fixture or pre-existing file, and depends only on the public `nexigen` API

#### Scenario: Failure to initialize is visible

- **WHEN** the supplied settings cannot be loaded or the Infrastructure fails to initialize
- **THEN** the executable reports the failure on the error stream and exits non-zero

### Requirement: Roles and failure modes are environment-driven

The application SHALL select its behavior from environment variables set by the runner without changing its identity: a worker role SHALL run until stopped, a degrade role SHALL terminate with a non-zero exit code after its interval, and a failure role SHALL die by signal after its interval. The intervals SHALL be configurable so a manual run uses full durations and a smoke run can compress them.

#### Scenario: Degrade role exits with an error code

- **WHEN** a degrade-role instance runs past its configured interval
- **THEN** the process terminates with a non-zero exit code rather than a signal

#### Scenario: Failure role dies by signal

- **WHEN** a failure-role instance runs past its configured interval
- **THEN** the process terminates by a signal, recording a null exit code and the signal in `exit.json`

#### Scenario: Worker role runs until stopped

- **WHEN** a worker-role instance runs without a failure interval
- **THEN** it keeps ticking and is terminated only by an operator stop or a signal from the runner

#### Scenario: Intervals are configurable

- **WHEN** the environment supplies explicit interval values
- **THEN** the degrade and failure roles use those values instead of their defaults

### Requirement: Telemetry workload covers all streams and metric kinds

While running, the application SHALL write all four telemetry streams — application logs, metric batches, traces, and chrono snapshots — and SHALL record at least one instrument of every metric kind (counter, gauge, descriptive statistics, explicit-bucket histogram, exponential histogram, and state gauge). Log emission SHALL average one to two messages per second, and metric collection SHALL occur once per second with the window reset between collections.

#### Scenario: All four streams are written

- **WHEN** an instance runs to its configured termination
- **THEN** `app.jsonl`, `metrics.jsonl`, `traces.jsonl`, and `chrono.jsonl` each contain records produced by that instance

#### Scenario: Every metric kind appears in the harvest

- **WHEN** the recorded metric batches are inspected
- **THEN** the harvested records include at least one counter, gauge, descriptive-statistics, explicit-histogram, exponential-histogram, and state-gauge record

#### Scenario: Metrics reset once per second

- **WHEN** the instance runs with a one-second collection interval
- **THEN** each successive metric batch reports the values recorded since the previous collection rather than an unbounded cumulative total for the reset instruments

#### Scenario: Log rate is steady

- **WHEN** the instance runs for several seconds
- **THEN** the number of emitted log records is consistent with one to two messages per second

### Requirement: Settings drive launch through the command line

Each application SHALL load its settings through the standard command-line options, pointing at the settings the runner generates for its instance so the four sinks resolve to the attempt artifact directory, the log root is that directory, and the application name in emitted records equals the instance's `app` identity.

#### Scenario: Instance settings directory is supplied on the command line

- **WHEN** the runner launches an application from the manifest
- **THEN** the child receives `--global-settings <instance_dir>/settings` and initializes from the merged files the runner wrote there

#### Scenario: Application name and directories follow the identity

- **WHEN** two instances of the one binary run under different `app` names
- **THEN** their log records carry distinct application names and their streams land in distinct attempt and instance directories

### Requirement: Suite manifest exercises all three impact levels

The demonstration SHALL ship a suite manifest that launches three instances of the single binary under distinct `app` names and impact levels — one not-impact, one degrade, and one failure — each carrying the command-line settings argument, a suite-level global settings source, and per-application local settings so the merge precedence is observable.

#### Scenario: Manifest names three distinct applications

- **WHEN** the manifest is loaded by `nexigen-run start --config`
- **THEN** exactly three applications are launched under distinct app names with the three distinct impact levels

#### Scenario: Global and local settings merge is exercised

- **WHEN** the suite global settings and a per-application local settings source both configure a field
- **THEN** the merged value seen by that application reflects the local override

#### Scenario: Manifest is accepted by the runner

- **WHEN** `nexigen-run start --config` is invoked with the shipped manifest
- **THEN** the suite supervisor starts and reports all three applications running

### Requirement: Impact policy produces the documented timeline

The demonstration SHALL make the suite supervisor's policy observable: a degrade failure SHALL be retried a limited number of times and then leave the application failed and the suite degraded while others keep running; a failure crash SHALL stop the whole suite, relaunch every application into new attempt directories, consume the shared restart budget, and finally fail the suite when that budget is exhausted.

#### Scenario: Degrade restart is observed

- **WHEN** the degrade instance exits with an error
- **THEN** the runner schedules a retry and relaunches that application into a new attempt directory, leaving earlier attempts intact

#### Scenario: Degrade exhaustion degrades the suite

- **WHEN** the degrade instance exhausts its restart budget
- **THEN** that application is reported failed, the suite state is degraded, and the remaining applications keep running

#### Scenario: Failure crash restarts the whole suite

- **WHEN** the failure instance dies by signal
- **THEN** every application is stopped and relaunched into new attempt directories after the restart delay

#### Scenario: Exhausted suite budget fails the suite

- **WHEN** the failure instance crashes again after the shared restart budget is consumed
- **THEN** the suite is stopped and recorded as failed

### Requirement: Driver and cleanup scripts run and reset the scenario

The project SHALL provide `examples/suite/run_suite_demo.sh` and `examples/suite/clean_suite_demo.sh`. The driver SHALL start the suite through `nexigen-run`, and the cleanup script SHALL remove the generated run, artifact, and suite metadata directories idempotently.

#### Scenario: Driver starts the suite

- **WHEN** `run_suite_demo.sh` is invoked
- **THEN** it builds the example if needed and starts the suite supervisor via `nexigen-run start --config`

#### Scenario: Cleanup removes generated state

- **WHEN** `clean_suite_demo.sh` is invoked after a run
- **THEN** the generated run, artifact, and suite metadata directories are removed

#### Scenario: Cleanup is idempotent

- **WHEN** the cleanup script runs and the generated directories are already absent
- **THEN** it exits successfully without reporting an error
