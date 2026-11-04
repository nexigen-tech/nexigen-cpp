# Spec Delta

## MODIFIED Requirements

### Requirement: Single implementation behind module shims

`nexigen::core::affinity` SHALL remain the compatibility implementation of the uint64-mask contract and SHALL delegate CPU-set conversion/application to the shared `core/thread-management` machinery. The chrono, logging, metrics, and tracing `detail/affinity` declarations SHALL remain forward-only shims, and no module unit SHALL keep a private `cpu_set_t`-converting copy. Runner CPU lists and thread specs SHALL use the same public CPU vocabulary rather than a third representation.

#### Scenario: Audit finds one implementation and four forwarders

- **WHEN** the affinity-related units are audited
- **THEN** legacy pin/query behavior is preserved, each module header forwards to core, and only the shared core machinery converts CPU values for operating-system calls

#### Scenario: Existing call sites and suites unchanged

- **WHEN** legacy `detail::pin_self`/`detail::self_affinity` calls are exercised
- **THEN** zero-mask rejection, calling-thread pinning, low-64-bit readback, and error mapping behave as before, including for workers not yet migrated by a caller
