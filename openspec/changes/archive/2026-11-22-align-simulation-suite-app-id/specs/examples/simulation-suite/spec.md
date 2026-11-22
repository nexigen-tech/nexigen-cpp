# Spec Delta

## ADDED Requirements

### Requirement: Simulation suite declares manifest application identifiers

The generated ExecutionManager suite manifest SHALL declare an explicit, valid, suite-unique
`app_id` for each application: `SIM_GEN` for `vector_generator`, `SIM_RECV` for `vector_receiver`,
and `SIM_FLKY` for `flaky_receiver`. Logs, metrics, and spans produced in suite mode SHALL carry
that identifier; the compiled four-character tags (`GEN0`, `RECV`, `FLKY`) SHALL remain only the
standalone fallback used when no `--app-id` is supplied.

#### Scenario: Generated manifest carries explicit identifiers

- **WHEN** `run_simulation_suite.sh` writes the generated `suite.json`
- **THEN** the three application entries declare `SIM_GEN`, `SIM_RECV`, and `SIM_FLKY`, all within
  the 1..16 byte identifier contract and unique within the suite

#### Scenario: Suite telemetry uses the manifest identifier

- **WHEN** the ExecutionManager launches an application from the generated manifest
- **THEN** its `LogFrame`, `MetricFrame`, and `SpanFrame` carry the manifest `app_id`, not the
  compiled tag and not the long application name

#### Scenario: Standalone run keeps the compiled fallback

- **WHEN** an example starts without `--app-id` and without the ExecutionManager
- **THEN** its telemetry falls back to the compiled tag (`GEN0`, `RECV`, or `FLKY`) and still starts
  successfully
