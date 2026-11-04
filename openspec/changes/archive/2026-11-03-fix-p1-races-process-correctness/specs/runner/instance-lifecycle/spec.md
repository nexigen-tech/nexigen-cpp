# runner/instance-lifecycle delta spec

## MODIFIED Requirements

### Requirement: resource.json captures start-time metadata

On start the runner SHALL write `resource.json` inside the instance directory
with the following fields: `run_id`, `app_name`, `instance_id`, `instance_dir`,
`pid`, `ppid`, `start_time_rfc3339`, `start_time_mono_ns`, `proc_start_ticks`
(the Linux `/proc/<pid>/stat` starttime value of the launched child, recorded
while the runner still owns the child — the identity anchor consumed by the
stop/status signal gate), and `version` (optional nexigen-version tag).

#### Scenario: resource.json is present after start

- **WHEN** the runner starts a child process
- **THEN** a `resource.json` file exists in the instance directory and contains
  at minimum the non-null identity fields, the child PID, and a positive
  `proc_start_ticks` anchor

#### Scenario: resource.json uses the same names as the telemetry-layout contract

- **WHEN** a human or tool reads `resource.json`
- **THEN** the `run_id`, `app_name`, `instance_id`, and `instance_dir` keys
  match the semantics of the `Run_identity` fields from `app/telemetry-layout`

#### Scenario: proc_start_ticks is serializable round-trip

- **WHEN** the metadata struct is serialized and parsed back through the
  project-wide to_json/from_json pair
- **THEN** `proc_start_ticks` round-trips unchanged and stays embedded-NUL-free
