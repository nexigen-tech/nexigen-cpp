# Spec Delta

## Purpose

Defines the platform-wide vocabulary for time domains: the class of a domain,
the well-known identifiers of system scales, and the naming and serialization
rules every module uses when it names a clock.

## ADDED Requirements

### Requirement: Two-level time-domain vocabulary

`nexigen::chrono` SHALL expose `TimeDomainClass` with `SystemClock`,
`CycleCounter`, and `DeviceClock`, and `TimeDomain` with `MonotonicRaw`, `Monotonic`, `Tai`,
`Realtime`, and `HostCounter`. `CLOCK_MONOTONIC_RAW` SHALL be represented by
`TimeDomain::MonotonicRaw`; no `Canonical` class or domain name SHALL exist.

#### Scenario: Classes and well-known domains exist

- **WHEN** the domain vocabulary is inspected at compile time
- **THEN** `TimeDomainClass` has exactly `SystemClock`, `CycleCounter`, and
  `DeviceClock`, and `TimeDomain` names `MonotonicRaw`, `Monotonic`, `Tai`,
  `Realtime`, and `HostCounter`

#### Scenario: MonotonicRaw is the hub, not a class

- **WHEN** a component names the hub scale
- **THEN** it uses `TimeDomain::MonotonicRaw` and no `Canonical` enumerator
  exists

#### Scenario: Every value round-trips

- **WHEN** every `TimeDomainClass` and `TimeDomain` value passes through
  `to_string`, `from_string`, `std::format`, and non-throwing JSON
- **THEN** the canonical spelling round-trips and hostile JSON leaves the
  destination unchanged

### Requirement: Well-known and device domain identifiers

System domains SHALL use stable identifiers derived from `TimeDomain`
(`MonotonicRaw` 0, `Monotonic` 1, `Tai` 2, `Realtime` 3, `HostCounter` 4);
hardware domains SHALL use identifiers in `16..47`. A device identifier SHALL be
paired with `TimeDomainClass::DeviceClock` and a stable domain name.

#### Scenario: System identifiers are stable

- **WHEN** a system domain is published
- **THEN** its identifier and canonical name are the well-known pair for that
  scale

#### Scenario: Device identifiers do not collide

- **WHEN** hardware domains are declared
- **THEN** every device uses a unique identifier in `16..47` and class `DeviceClock`

#### Scenario: Device names are stable

- **WHEN** a device domain is declared with a name
- **THEN** the name survives serialization and diagnostics without being
  replaced by an ordinal

### Requirement: Compile-time domain declaration

A component SHALL be able to declare the time domain it stamps in as a
`constexpr TimeDomain`, and a component that does not declare one SHALL default
to `TimeDomain::MonotonicRaw`.

#### Scenario: Declaration is a constant expression

- **WHEN** the declared domain of a component is read
- **THEN** it is a `constexpr TimeDomain` requiring no live object or runtime
  state

#### Scenario: Reading the declaration is free

- **WHEN** the declared domain is read under an armed allocation arena
- **THEN** no allocation, lock, or clock read occurs

#### Scenario: Undeclared defaults to MonotonicRaw

- **WHEN** a component provides no declaration
- **THEN** its domain is interpreted as `TimeDomain::MonotonicRaw`
