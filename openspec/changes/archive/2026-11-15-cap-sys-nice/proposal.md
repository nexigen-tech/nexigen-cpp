# Proposal

## Why

The least-privilege change drops applications to a configured user but leaves
the kernel capability set untouched: a root-supervised application still inherits
`CAP_SYS_NICE`, which is exactly the capability that lets it raise its own
real-time priority and escape the affinity/CPU-budget model. `REQ_EM_009b` asks
for that capability to be granted selectively.

## What Changes

- An application entry gains `allow_rt_priority` (default `false`).
- In the child, before the credential drop and `exec`, the manager drops
  `CAP_SYS_NICE` from the capability bounding set unless the entry opts in.
  Dropping it from the bounding set means no later `exec` can regain it, even
  when the process runs as root.
- A failed drop on a privileged child reports setup failure code 12; a
  non-root child (which cannot use the capability anyway) skips the call.
- `docs/em-deferred.md` records `REQ_EM_009b` as implemented for the bounding
  set; ambient capabilities and a full `capset` policy stay deferred.

## Capabilities

### New Capabilities

None.

### Modified Capabilities

- `exec/process-supervision`: the child capability policy is now part of the
  launch contract.

## Impact

- **Code**: `AppSpec`, `StartOptions`, the launch plan, and the child setup.
- **Performance**: one `prctl` per child start; no hot-path impact.
- **Memory**: none.
- **Compatibility**: opt-in manifest field; the default removes a capability that
  a well-behaved application should not use, and a deployment that relies on it
  can set `allow_rt_priority: true`.
