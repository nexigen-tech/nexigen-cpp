# Design

## Context

See `proposal.md` — Why. The child already applies cgroup membership, credentials,
rlimits, and affinity before `exec`; a failed step reports through the setup
pipe (codes 1–11).

## Goals / Non-Goals

**Goals:**

- Deny self-elevation to real-time priority by default.
- Keep the opt-in explicit per application.
- No new library dependency.

**Non-Goals:**

- A full capability policy (per-capability permitted/effective sets, ambient
  capabilities). That needs `libcap` or raw `capget`/`capset` and is deferred.
- Changing which scheduling policy the application uses (the EM still does not
  select one).

## Decisions

**D1 — Drop from the bounding set.** `PR_CAPBSET_DROP` is a single `prctl`, needs
only `CAP_SETPCAP`, and is irreversible across `exec`: a root application cannot
regain `CAP_SYS_NICE` later. That is precisely the "opt-in" semantics
`REQ_EM_009b` asks for.
*Alternative rejected:* `capset` with an empty set — it needs raw syscalls or
`libcap` and would also remove capabilities unrelated to RT scheduling.

**D2 — Non-root children skip the call.** A process without `CAP_SETPCAP`
cannot drop from the bounding set; a non-root application also cannot use
`CAP_SYS_NICE` for priority escalation. The call is attempted only when the
child is privileged, avoiding a spurious setup failure on developer machines.

**D3 — Fail closed when the drop fails.** If a privileged child cannot drop the
capability, the launch fails (setup code 12) instead of silently leaving the
capability in place.

## Risks / Trade-offs

- **An application that legitimately needs RT priority must opt in.** → The
  manifest field is explicit and documented; the default protects the shared
  CPU budget.
- **The bounding set is process-wide and inherited.** → The drop happens in the
  child before `exec`, so the supervisor and sibling applications are unaffected.
- **Containers may already lack the capability.** → The drop still succeeds (the
  bit is already clear); the test reads the bit from `/proc/self/status`.

## Migration Plan

Additive. Deployments that need RT priority add `allow_rt_priority: true`; all
others get the safer default on the next restart.
