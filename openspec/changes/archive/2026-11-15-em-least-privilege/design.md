# Design

## Context

See `proposal.md` — Why. The child setup already applies affinity, environment,
rlimits, and the working directory, then reports failures through a setup pipe
(codes 1 setsid, 2 affinity, 3 setenv, 4 exec). `execution_manager_test` launches
real `/bin/sh` children.

## Goals / Non-Goals

**Goals:**

- Drop to a configured user before `exec`, fail-closed.
- Never require root to run the test suite.
- Keep the default behavior for manifests that do not opt in.

**Non-Goals:**

- Ambient capabilities (`CAP_SYS_NICE`) or user namespaces.
- Per-application SELinux/AppArmor policy.
- Changing the SM/PHM daemons' own credentials.

## Decisions

**D1 — Credentials are manifest fields, applied last.** `uid`/`gid` live on
`AppSpec` and travel through `StartOptions`; the child applies them after every
privileged operation and immediately before `exec`. `gid` without `uid` is
meaningless and rejected; `uid` alone defaults `gid` to `uid`.
*Alternative rejected:* a global suite user — mixed-criticality deployments need
per-application accounts.

**D2 — `setgroups` only when privileged.** A non-root child calling
`setgroups(0, nullptr)` gets `EPERM`; running as the same user must stay
supported (tests, developer machines). The call is therefore gated on
`geteuid() == 0`, and the subsequent `setgid`/`setuid` are verified with
`getuid`/`geteuid`/`getgid`.

**D3 — The non-root policy is opt-in.** A hard "never run as root" rule would
break the project's root CI containers and root deployments that intentionally
run everything as root. `trusted_platform.require_non_root` makes the strict
behavior a deployment decision; the example manifest enables it.
*Alternative rejected:* always refusing root — it would make the test matrix and
container deployments unusable without an escape hatch.

**D4 — Fail through the existing setup pipe.** New failure codes (7 setgroups,
8 setgid, 9 setuid, 10 verification) reuse the existing handshake: the parent
sees a reported failure and `start` returns `SystemError`, so no instance is
reported as running.

## Risks / Trade-offs

- **Dropping to a user without access to the artifact directory.** → The parent
  opens the log fds before the fork; the child writes through them. A deployment
  that needs writable directories must grant them to the target user.
- **`uid` configured but the supervisor is non-root.** → `setuid` to another
  user fails with `EPERM`, the child reports setup failure, and the launch fails
  loudly instead of silently keeping the old user.
- **The policy can be forgotten.** → It is documented and enabled in the example
  manifest; the matrix/em-deferred note records the assumption.

## Migration Plan

Additive. Existing manifests keep the current user. To adopt: create the
accounts, add `uid`/`gid` per application, set `require_non_root`, and make sure
the target user can read the executable and settings and write through the
provided fds.
