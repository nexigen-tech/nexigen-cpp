# Proposal

## Why

`RS_EM_00005` and `RS_OSI_00202` require configurable CPU budgets for processes
and groups, and `RS_OSI_00201` a memory budget. Today a resource group carries
only an `RLIMIT_AS` address-space limit and a CPU affinity set: there is no way
to cap CPU time, so one busy application can starve the rest of the suite. The
traceability matrix records that gap on `RS_EM_00005`, `RS_OSI_00201`,
`RS_OSI_00202`, and `RS_SAF_10008`.

## What Changes

- A resource group gains `cpu_quota_us`, `cpu_period_us` (default 100000), and
  `memory_max_mb`.
- The suite manifest gains an optional `cgroup_root` (default
  `/sys/fs/cgroup`), and validation rejects a relative root, an invalid period,
  or a negative budget.
- Before forking the supervisor, `start_suite` prepares one cgroup v2 directory
  per budgeted group (`<root>/nexigen/<suite>/<group>`) and writes `cpu.max`
  and `memory.max`. A configured budget that cannot be applied fails the start
  (`SystemError`); groups without budgets never touch the filesystem.
- The child moves itself into the prepared cgroup (write `0` to
  `cgroup.procs`) after the other privileged setup and before `exec`, reporting
  setup code 11 on failure.
- `RLIMIT_AS` stays as a per-process backstop.
- The matrix promotes `RS_EM_00005`, `RS_OSI_00201`, and `RS_OSI_00202` to
  `supported`; `RS_SAF_10008`/`RS_SAF_10031` stay `partial` (no accounting or
  per-thread execution-time detection).

## Capabilities

### New Capabilities

None.

### Modified Capabilities

- `exec/resource-groups`: groups gain CPU and cgroup-memory budgets applied via
  cgroup v2.

## Impact

- **Code**: `ResourceGroupConfig`, manifest parsing/validation, a cgroup
  preparation helper, the launch plan, and the child setup.
- **Performance**: cold path only — a few filesystem writes per suite start and
  one `cgroup.procs` write per child.
- **Memory**: none.
- **Compatibility**: additive. Groups without the new fields keep `RLIMIT_AS`
  and affinity exactly as before. Deployments need a writable cgroup v2
  hierarchy to use budgets.
