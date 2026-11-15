# Proposal

## Why

`REQ_EM_009` (EM least privilege) is the last unimplemented item in the EM audit:
the suite supervisor launches every application under its own user, with no way
to drop to a dedicated account. Running safety applications as the same user (or
as root) defeats the isolation that process-per-application is supposed to
provide, and it blocks any future checkpoint-identity work.

## What Changes

- A manifest application entry gains optional `uid`/`gid`. `uid` alone implies
  `gid = uid`; `gid` without `uid` and any explicit `0` are rejected at
  validation time.
- The child applies the credentials after all privileged setup (affinity,
  rlimits, working directory, environment) and immediately before `exec`:
  supplementary groups are cleared when the process is privileged, then
  `setgid`, then `setuid`; the drop is verified with `getuid`/`geteuid`/`getgid`.
  Any failure reports a setup code and the application does not start.
- `trusted_platform.require_non_root` (default `false`) makes a suite refuse to
  launch an application when the supervisor has effective UID 0 and the entry
  declares no `uid`. Deployments that must never run as root enable it; the
  default keeps root CI containers working.
- `docs/em-deferred.md` records `REQ_EM_009` as implemented for setuid/setgid;
  granting `CAP_SYS_NICE` selectively remains open.

## Capabilities

### New Capabilities

None.

### Modified Capabilities

- `exec/process-supervision`: the child launch contract gains credential
  dropping and the non-root policy.

## Impact

- **Code**: `AppSpec`, `StartOptions`, `TrustedPlatformConfig`, the suite launch
  plan, and the child setup in `execution_manager.cpp`.
- **Performance**: zero hot-path impact; three syscalls once per child start.
- **Memory**: none.
- **Compatibility**: additive. Manifests without `uid`/`gid` and suites without
  `require_non_root` behave exactly as before.
