# Tasks

## 1. Manifest and validation

- [x] 1.1 Add `cpu_quota_us`, `cpu_period_us`, and `memory_max_mb` to `ResourceGroupConfig`, plus an optional absolute `cgroup_root` on `SuiteManifest`, with JSON round-trip; verify the round-trip.
- [x] 1.2 Reject a relative `cgroup_root`, a period outside 1000–1000000 when a quota is set, and a zero-period quota; verify one test per rejection.

## 2. Preparation and child join

- [x] 2.1 Add `prepare_resource_groups(manifest, error)`: create `<root>/nexigen/<suite>/<group>` and write `cpu.max`/`memory.max` for budgeted groups, touching nothing otherwise; verify the file contents in a temporary root and the no-budget no-op.
- [x] 2.2 Call the preparation from `start_suite` before the fork (failure → `SystemError`, no process) and pass the group path through the launch plan to `StartOptions.cgroup_path`.
- [x] 2.3 In the child, write `0` to `<cgroup_path>/cgroup.procs` before `exec` (setup code 11) and verify the write against a temporary directory.

## 3. Documentation, matrix, and verification

- [x] 3.1 Document the budgets in `docs/architecture.md`, promote `RS_EM_00005`/`RS_OSI_00201`/`RS_OSI_00202` in the matrix, and verify the traceability gate exits 0.
- [x] 3.2 Run `nexigen_app_suite_tests`, `nexigen_exec_tests`, the complexity gate, and `openspec validate --changes --strict`; verify all pass.

## Workflow follow-up

- Archive the change after review.
