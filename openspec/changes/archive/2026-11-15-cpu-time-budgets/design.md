# Design

## Context

See `proposal.md` — Why. Resource groups already carry an `RLIMIT_AS` limit,
CPU affinity, and a working directory. The suite start path validates the
manifest, preflights thread attributes, then forks the detached supervisor; the
child applies affinity, environment, rlimits, and credentials before `exec`.

## Goals / Non-Goals

**Goals:**

- Real CPU and memory caps for groups, applied by the kernel.
- Fail-closed when a configured budget cannot be enforced.
- Test the path without a writable cgroup hierarchy in CI.

**Non-Goals:**

- CPU *accounting* or admission control (a later `RS_SAF_10008` step).
- `SCHED_DEADLINE` or real-time throttling.
- Cleaning up empty cgroups after a suite stops (follow-up).
- Per-application budgets separate from the group.

## Decisions

**D1 — cgroup v2 files, no library.** `cpu.max` and `memory.max` are plain
files; the manager creates the directory tree and writes two files. This keeps
the dependency surface at zero and works with the kernel's delegation model.
*Alternative rejected:* libcgroup — an extra dependency for two writes.

**D2 — Prepare in the parent, join in the child.** `start_suite` creates the
group directories before the supervisor fork, so a missing hierarchy fails
`start` cleanly with `SystemError`. Each child writes `0` to `cgroup.procs`
after its other privileged setup, which moves the calling process into the
group. The parent cannot know the child pid reliably before `exec`, and the
child's self-move is the documented cgroup v2 mechanism.
*Alternative rejected:* the parent writes the pid after the fork — it races
with `exec` and fails for short-lived children.

**D3 — Budgets are opt-in and fail closed.** A group with no `cpu_quota_us` /
`memory_max_mb` never touches the filesystem, so existing deployments and CI
are unaffected. When a budget *is* configured, a missing writable hierarchy is
an error rather than a silent downgrade: a safety budget that is not enforced
is worse than no budget.
*Alternative rejected:* warn-and-continue — the deployment would believe it has
a cap it does not have.

**D4 — `cgroup_root` is configurable for testability and delegation.** The
default `/sys/fs/cgroup` works for root/delegated deployments; tests point it at
a temporary directory and assert the file contents and the child's
`cgroup.procs` write. A real-hierarchy integration test stays opt-in.

**D5 — `RLIMIT_AS` remains.** `memory.max` caps the group's charged memory;
`RLIMIT_AS` bounds a single process's address space. They answer different
questions and both stay.

## Risks / Trade-offs

- **A partially created hierarchy on failure.** → `start` fails; the empty
  directories are harmless and reusable on retry.
- **`memory.max` without swap control.** → The group can still swap; swap
  limiting is a follow-up (`memory.swap.max`).
- **Nested/delegated roots differ per distro.** → `cgroup_root` makes the
  deployment explicit; the default is the standard mount point.

## Migration Plan

Additive. To adopt: ensure a writable cgroup v2 subtree, set `cgroup_root` if it
is not `/sys/fs/cgroup`, and add `cpu_quota_us`/`memory_max_mb` to groups.
Removing the fields restores the previous behavior.
