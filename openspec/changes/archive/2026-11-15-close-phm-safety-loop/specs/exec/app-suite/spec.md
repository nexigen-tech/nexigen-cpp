# Spec Delta

## MODIFIED Requirements

### Requirement: Restarts follow abnormal termination only

Application restarts SHALL be triggered only by abnormal termination — a non-zero exit code, death by signal, or an accepted PHM recovery request. Exit code 0 and an operator-issued `stop` SHALL never restart. For `degrade` and `not_impact` applications the supervisor SHALL retry up to `restart_attempts` times, waiting `restart_delay_s`, reusing the control instance directory with a new attempt artifact directory; a PHM recovery restart SHALL NOT consume that budget.

#### Scenario: Abnormal exit triggers a delayed retry

- **WHEN** a `degrade` or `not_impact` application exits with a non-zero code or a signal and retries remain
- **THEN** the supervisor waits `restart_delay_s` and launches the application again in the same control instance directory with a new attempt artifact directory

#### Scenario: Exhausted per-application retries mark the application failed

- **WHEN** the application has consumed all `restart_attempts`
- **THEN** it is reported `failed` and is not launched again until the suite is restarted

#### Scenario: Normal exit does not restart

- **WHEN** an application exits with code 0 on its own
- **THEN** it is reported `exited` and is not restarted

#### Scenario: Operator stop does not restart

- **WHEN** an operator stops an application or the whole suite
- **THEN** no restart occurs for any stopped application

#### Scenario: Retries create new artifact files

- **WHEN** an application is restarted after a failure
- **THEN** the new attempt writes a new `stdout.log`, `stderr.log`, `app.jsonl`, `metrics.jsonl`, `traces.jsonl`, and `chrono.jsonl` in a new attempt directory, rewrites `resource.json`, and leaves the previous attempt's files unchanged

#### Scenario: PHM restart bypasses the normal-exit rule

- **WHEN** the supervisor accepts a PHM restart request for an application that exited with code 0
- **THEN** it relaunches that application in a new attempt and does not consume `restart_attempts`

## ADDED Requirements

### Requirement: PHM recovery requests are served

The suite supervisor SHALL serve `RecoveryRequest`/`RecoveryResponse` on the PHM recovery service. `RestartProcess` SHALL force-stop and relaunch the named application in a new attempt and answer accepted. An unknown name, an application outside the suite, or any other action SHALL be answered rejected and SHALL change no process.

#### Scenario: Restart request relaunches the application

- **WHEN** the supervisor receives `RestartProcess` for a supervised application
- **THEN** it stops that application if running, relaunches it in a new attempt, and answers accepted

#### Scenario: Unknown application is rejected

- **WHEN** the request names an application the suite does not own
- **THEN** the supervisor answers rejected and no process changes state

#### Scenario: Watchdog action is rejected by the EM

- **WHEN** the request carries `TriggerWatchdogReset`
- **THEN** the supervisor answers rejected because the watchdog is owned by PHM
