# Spec Delta

## Purpose

Defines the structural C++23 concepts every metrics instrument satisfies and the compile-time access policy that exposes or hides read/snapshot operations without virtual dispatch or runtime checks.

## ADDED Requirements

### Requirement: Metric concept core surface

`nexigen::metrics::concepts::Metric` SHALL be satisfied only by types exposing, without virtual functions: a `static constexpr MetricKind kind`; `static constexpr ConcurrencyPolicy concurrency`; `static constexpr AccessPolicy access`; `static constexpr ResetPolicy reset`; and a cold-path `std::string to_string() const`.

#### Scenario: Conforming instrument satisfies the concept

- **WHEN** `concepts::Metric<T>` is evaluated for every published instrument type with its declared policies
- **THEN** it is `true` in a constant expression

#### Scenario: Missing policy metadata rejects the concept

- **WHEN** `concepts::Metric<T>` is evaluated for a type missing `kind`, `concurrency`, `access`, `reset`, or `to_string`
- **THEN** it is `false` and the rejection is detected at compile time

#### Scenario: Wrong policy type rejects the concept

- **WHEN** `concepts::Metric<T>` is evaluated for a type whose `kind` is an integer rather than `MetricKind`
- **THEN** it is `false`

### Requirement: Readable metric concept

`nexigen::metrics::concepts::ReadableMetric` SHALL refine `Metric` with public `read()` and `snapshot()` operations callable on a const-qualified instrument: `read()` returns the instrument's current value summary and `snapshot()` returns its full `MetricRecord`, each usable without allocation or blocking.

#### Scenario: Readable instruments satisfy the refined concept

- **WHEN** `concepts::ReadableMetric<T>` is evaluated for an instrument instantiated with `AccessPolicy::Readable`
- **THEN** it is `true` and both `read()` and `snapshot()` are callable on a `const T`

#### Scenario: Write-only instruments fail the refined concept

- **WHEN** `concepts::ReadableMetric<T>` is evaluated for an otherwise identical instrument instantiated with `AccessPolicy::WriteOnly`
- **THEN** it is `false`

#### Scenario: Read does not mutate observable totals

- **WHEN** a readable instrument is read repeatedly in `Cumulative` mode with no intervening recording
- **THEN** every read returns the same totals and leaves the instrument usable for further recording

### Requirement: Compile-time access gating

For `AccessPolicy::Readable`, the instrument SHALL expose public `.read()` and `.snapshot()`. For `AccessPolicy::WriteOnly`, both operations SHALL be disabled at compile time by a constraint of the form `requires (Access == AccessPolicy::Readable)`, while `MetricProvider`'s harvester retains internal friend access to produce snapshots.

#### Scenario: Write-only read is rejected at compile time

- **WHEN** a translation unit evaluates `requires` expressions for `metric.read()` and `metric.snapshot()` on a `WriteOnly` instrument
- **THEN** both are unsatisfied and overload resolution finds no viable candidate outside the provider

#### Scenario: Write-only recording still succeeds

- **WHEN** a `WriteOnly` instrument records values through its hot-path operations
- **THEN** those calls compile and execute, and the provider's harvester observes the accumulated values through internal access

#### Scenario: Readable read is accepted

- **WHEN** the same calls are compiled against a `Readable` instrument
- **THEN** `.read()` and `.snapshot()` compile and return the expected values

### Requirement: Instrument conformance is pinned

The metrics test translation unit SHALL contain `static_assert` checks proving that every published instrument satisfies `concepts::Metric`, that every `Readable` instantiation satisfies `concepts::ReadableMetric`, and that no `WriteOnly` instantiation does.

#### Scenario: Conformance assertions compile

- **WHEN** the metrics test translation unit is compiled
- **THEN** all concept assertions pass for `Counter`, `Gauge`, `DescriptiveStatistics`, `ExplicitBucketHistogram`, and `ExponentialHistogram` in both concurrency policies

#### Scenario: Removing a required member breaks the build

- **WHEN** a required member is removed from a published instrument in a scratch build
- **THEN** the corresponding concept assertion fails at compile time
