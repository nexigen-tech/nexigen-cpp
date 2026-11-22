# Design

## Context

See `proposal.md` — Why. The archived `app/policies` change added `Iceoryx2Policy<A,B,C,D>` with a single process-wide `com::Node`. The verification pass identified that the null-node failure path (`SystemError`) could not be exercised because `com` has no fault-injection hook. The implementation now exposes an optional `NodeProvider` template parameter; the default `SharedNodeProvider` preserves the process-wide singleton.

## Goals / Non-Goals

**Goals:**

- Describe the public policy signature and the default node behavior accurately.
- Make the null-node failure path testable without changing production behavior.
- Keep `Iceoryx2Policy<A,B,C,D>` source-compatible.

**Non-Goals:**

- No change to the logger, metric or trace frame formats.
- No change to the service names or the COM gate.
- No runtime policy switching or multi-node production topology.

## Decisions

### D1 — Default provider preserves the singleton

`SharedNodeProvider::get()` returns `iceoryx2_shared_node()`, the same lazily created process-wide `com::Node` used before. `Iceoryx2Policy<A,B,C,D>` relies on the default template argument, so existing applications and examples compile unchanged.

Alternative considered: keep the singleton private and add a separate test-only policy. Rejected because it would duplicate the telemetry wiring and leave the production policy untested on its failure path.

### D2 — Optional provider parameter

`Iceoryx2Policy<A,B,C,D,NodeProvider = SharedNodeProvider>` requires `NodeProvider::get() noexcept -> com::Node*`. The policy calls it only in `emplace_metrics_sink` and `emplace_trace_sink`; the logger continues to create its own node.

### D3 — Null provider is a status

A null node returns `SystemError` before `out.emplace`, so no sink is started and the infrastructure rollback path is exercised. The test uses `FailingNodeProvider` returning `nullptr`.

### D4 — Diagnostics generalize over the provider

`to_string`, the free `to_string`, `std::formatter`, `to_json` and `from_json` accept any `NodeProvider`, so a test policy with a non-default provider still has the mandatory diagnostic surfaces.

## Risks / Trade-offs

- **[Provider lifetime]** A custom provider must return a node that outlives the sinks. → The default shared node is process-lifetime; custom providers are documented as borrowed and are intended for tests or explicit integrations.
- **[Spec/API drift]** The main spec previously described only the default. → This change updates the requirement to name the provider and its default.
- **[Test-only seam in public header]** The template parameter is visible to users. → It is a small, source-compatible extension; the default keeps the previous behavior.

## Migration Plan

No migration. Existing `Iceoryx2Policy<A,B,C,D>` uses remain valid. Rollback is removing the template parameter and the failure test; no data or ABI changes.

## Open Questions

None.
