# Spec Delta

## REMOVED Requirements

### Requirement: Base clock-domain identifiers

**Reason**: The single-level `Clock_id` enum is replaced by the two-level
`TimeDomainClass` and `TimeDomain` vocabulary in `chrono/time-domain`.

**Migration**: Use `TimeDomain` for the clock scale and `TimeDomainClass` for
its role; `monotonic_raw` maps to `TimeDomain::MonotonicRaw`, and `realtime`
maps to `TimeDomain::Realtime`.

### Requirement: Compile-time domain declaration

**Reason**: The compile-time declaration now uses `TimeDomain`, which also
covers `monotonic`, `tai`, and the TSC counter.

**Migration**: Replace `constexpr Clock_id` declarations with
`constexpr TimeDomain` and update the default from `monotonic_raw` to
`TimeDomain::MonotonicRaw`.
