# Spec Delta

## REMOVED Requirements

### Requirement: Nanosecond timestamp value type

**Reason**: Relocated — the `Timestamp` value type now belongs to the shared `nexigen::chrono` module (`chrono/timestamp` capability), which owns all time primitives.
**Migration**: Include `<nexigen/chrono/timestamp.hpp>` and use `nexigen::chrono::Timestamp`. The type's behavior is unchanged; no backward-compatible alias in `nexigen::core` is retained.

### Requirement: Monotonic clock sourcing

**Reason**: Relocated to `nexigen::chrono` (`chrono/timestamp` capability) together with the value type it reads.
**Migration**: Call `nexigen::chrono::Timestamp::now()` from `<nexigen/chrono/timestamp.hpp>`; the error-model contract (`Result`, no termination on clock failure) is identical.

### Requirement: Timestamp arithmetic

**Reason**: Relocated to `nexigen::chrono` (`chrono/timestamp` capability).
**Migration**: Use the operators on `nexigen::chrono::Timestamp` (ordering, offset arithmetic, saturating deltas) — semantics are unchanged from the removed core requirement.

### Requirement: Hot-path safety of timestamps

**Reason**: Relocated to `nexigen::chrono` (`chrono/timestamp` capability); the guarantee travels with the type.
**Migration**: `nexigen::chrono::Timestamp` remains an 8-byte trivially-copyable, allocation-free, lock-free value usable under `-fno-exceptions`; link the `nexigen::chrono` target.
