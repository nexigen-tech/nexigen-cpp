# diagnostics/scoped-stopwatch Specification

## Purpose

Defines the zero-allocation RAII scope guard for measuring a scope's execution time: elapsed reporting to a caller-provided reference or a non-throwing sink at scope exit, optional calibrated source injection, and budget-overrun checks for real-time loop guards.

## Requirements

### Requirement: RAII scope measurement

`ScopedStopwatch` SHALL be a non-copyable, non-movable RAII guard that samples the start timestamp at construction and, on destruction, computes the scope's elapsed nanoseconds and delivers it exactly once to a caller-provided destination: a `std::uint64_t&`, or a non-throwing callable sink. A supplied `chrono::Stamp_source*` stamps through that source; null or source-less construction selects the default clock. The whole surface is `noexcept`, zero-allocation, lock-free, atomic-free.

#### Scenario: Out-reference destination receives the elapsed value

- **WHEN** a `ScopedStopwatch` bound to a `std::uint64_t&` covers a scope containing measurable work and returns by scope exit
- **THEN** the reference holds a strictly positive nanosecond value at least as large as the performed work (a fresh guard + work + re-measure stays consistent across repeated scopes)

#### Scenario: Sink destination is invoked exactly once

- **WHEN** a `ScopedStopwatch` bound to a non-throwing sink covers a scope and returns by scope exit
- **THEN** the sink is invoked exactly once with the scope's elapsed nanoseconds (called on destruction, once, and never at construction), and the reporting call itself remains `noexcept`

#### Scenario: Guard is not transferable

- **WHEN** a translation unit attempts to copy or move a `ScopedStopwatch`
- **THEN** compilation fails — the guard owns exactly one destination and one measurement, and there is no legal handoff

### Requirement: Budget-overrun check

`ScopedStopwatch` SHALL expose `has_exceeded_budget(std::uint64_t budget_ns) const noexcept -> bool` reporting true when elapsed-so-far is strictly greater than the threshold, plus a no-argument overload against a default budget optionally configured at construction (`0` meaning none; unconfigured always reports false). Checks are available mid-scope and SHALL NOT mutate the guard or report anything to the destination early.

#### Scenario: Exceeded budget reports true during the scope

- **WHEN** `has_exceeded_budget` is queried mid-scope after work that clearly exceeds the given threshold
- **THEN** it returns true, and the destination has received nothing yet (destruction still holds the final report)

#### Scenario: Within budget reports false

- **WHEN** the check is queried mid-scope with a threshold the elapsed time cannot have reached (bounded query immediately after construction against a much larger threshold)
- **THEN** it returns false

#### Scenario: Default budget participates when configured

- **WHEN** a guard is constructed with a default budget and the no-argument `has_exceeded_budget()` is called after work exceeding it, and separately on a guard constructed without one
- **THEN** the configured guard reports true while the unconfigured guard reports false with the same body of work

### Requirement: Construction overloads and default-clock fallback

`ScopedStopwatch` SHALL be constructible for both destination kinds — a `std::uint64_t&` or a non-throwing sink — each with an optional leading `chrono::Stamp_source*` source and an optional trailing default budget (`0` meaning none). Constructor clients SHALL be able to combine source + destination + budget; the integer/budget parameters SHALL be distinguishable from the sink type (an integral literal SHALL bind to the integer destination, never to the sink template).

#### Scenario: Source and budget combine with each destination

- **WHEN** guards are constructed with a `Stamp_source*` plus each destination kind, with and without a default budget
- **THEN** construction compiles cleanly and the guard's measurement uses the injected source, reporting elapsed normally at destruction

#### Scenario: Destination construction is unambiguous

- **WHEN** a `ScopedStopwatch` is constructed with only a `std::uint64_t&` (no further arguments) and with its sink factory only (a non-throwing callable)
- **THEN** each resolves to its clear overload and both behave as the RAII lifecycle specifies

### Requirement: Hot-path and layout guarantees

Guard construction, destruction, and budget queries SHALL complete with at most one clock sample per boundary (one at construction, one at destruction) plus fixed store/register work; no heap allocation, no locks, no atomics at any point. In Release builds, a warm median harness SHALL gate construct + destruct ≤ 20 ns on the calibrated `Stamp_source` path. The guard SHALL stay a small stack object; destinations are borrowed references/callables, not owned state.

#### Scenario: Guard lifecycle allocates nothing

- **WHEN** allocation counting is armed and repeated scope measurements run with each destination kind
- **THEN** zero heap allocations occur across construction scope/destruction for both destinations

#### Scenario: Release latency gate holds for guard lifecycle

- **WHEN** the Release build's median harness measures construct + destruct around a minimal scope on the calibrated source
- **THEN** the median stays within the 20 ns budget and is above zero (not optimized to nothing)

### Requirement: ScopedStopwatch diagnostic rendering

`ScopedStopwatch` SHALL provide the cold-path member `std::string to_string() const` and ADL free `to_string(const ScopedStopwatch&)`, rendered through a `std::formatter` specialization delegating to the member; all format/diagnostic surface stays cold-path (the templated sink type does not participate in rendering), and the guard itself is intentionally not deserializable.

#### Scenario: Guard renders its live measurement

- **WHEN** `to_string()` and `std::format("{}", guard)` are evaluated mid-scope on an active guard
- **THEN** both render the guard's active scope status and the elapsed-so-far/pending budget readout, and the free helper returns the same text as the member

#### Scenario: Guard format stays cold-path

- **WHEN** the guard measurement lifecycle runs with allocation counting armed
- **THEN** no rendering call occurs on the guard hot path — formatting only happens when explicitly invoked for diagnostics
