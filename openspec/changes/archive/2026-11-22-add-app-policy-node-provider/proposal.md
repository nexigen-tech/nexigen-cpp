# Proposal

## Why

The archived `app/policies` specification says the iceoryx2 metric and trace sinks borrow “one process-wide node”. The implemented policy now supports an optional `NodeProvider` template parameter so the failure path (`NodeProvider::get()` returns `nullptr` → `SystemError`) can be tested without a process-wide singleton. The default behavior is unchanged, but the specification no longer describes the public policy signature accurately.

## What Changes

- Modify the `app/policies` requirement “Iceoryx2 policy publishes all telemetry as fixed frames” to name `Iceoryx2Policy<A,B,C,D,NodeProvider>` and its default `SharedNodeProvider`.
- State that metric and trace sinks borrow `NodeProvider::get()`, that the default provider returns one process-wide node, and that a null node fails initialization with `SystemError` before any sink starts.
- Keep the existing log/metric/trace service names, four-character application tag, and COM gate unchanged.
- No **BREAKING** changes: `Iceoryx2Policy<A,B,C,D>` remains valid through the default template argument.

## Capabilities

### New Capabilities

None.

### Modified Capabilities

- `app/policies`: the iceoryx2 policy requirement now describes the optional node provider, its default shared-node behavior, and the null-node failure contract.

## Impact

- **Spec:** `openspec/specs/app/policies/spec.md`.
- **Code:** `include/nexigen/app/policies/iceoryx2_policy.hpp` (already implements the provider seam); no production behavior change for the default `SharedNodeProvider`.
- **Tests:** `tests/app/policies_com_test.cpp` (already covers a failing provider returning `nullptr`).
- **Docs:** `docs/articles/app.md` already mentions the optional provider.
- **Performance:** provider selection and node creation happen during `Infrastructure::initialize`; no producer or tick hot path is affected.
