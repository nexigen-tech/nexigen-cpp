# Spec Delta

## MODIFIED Requirements

### Requirement: Canonical monotonic scale

`CLOCK_MONOTONIC_RAW` nanoseconds SHALL be the platform hub scale and SHALL be
named `TimeDomain::MonotonicRaw`; no `Canonical` class or user-visible
`canonical` domain name SHALL remain. Every other domain SHALL be related to the
hub by an explicitly published bidirectional affine map and SHALL NOT be assumed
equal to it. The hub epoch SHALL change only with the kernel boot.

#### Scenario: Canonical row is always present

- **WHEN** a time-base block is published
- **THEN** it contains the `MonotonicRaw` row with an identity map and zero
  uncertainty

#### Scenario: Host cycle counter is an ordinary domain

- **WHEN** the platform uses the TSC fast path
- **THEN** the TSC appears as a `CycleCounter` domain with its own measured map,
  uncertainty and status, and no component treats its value as the hub

#### Scenario: No hardware domains is a valid configuration

- **WHEN** no interface module is declared or present
- **THEN** the published block contains only host domains and the base can be
  `Ready`

### Requirement: Domain coefficient row

Each row SHALL carry a stable domain id and name, `TimeDomainClass`, an epoch,
native and hub units, forward and reverse affine coefficients with strictly
positive scales, an uncertainty in nanoseconds, a lock state, and validity
limits in both native and hub units. The reverse coefficient SHALL be stored
beside the forward coefficient so any domain can be converted both ways.

#### Scenario: Map converts a native instant

- **WHEN** a native value inside a row's validity interval is converted to the hub
- **THEN** the result equals the anchored forward map and lies inside the row's
  hub validity interval

#### Scenario: Reverse map converts a hub instant

- **WHEN** a hub value inside a row's validity interval is converted back to the
  native domain
- **THEN** the result equals the anchored reverse map without consulting another
  row

#### Scenario: Validity is expressed in both domains

- **WHEN** a consumer needs the row that applies to a native hardware stamp
- **THEN** it finds the row by native validity without first knowing the hub
  instant

#### Scenario: Non-positive scale is rejected

- **WHEN** a row would carry a forward or reverse scale factor of zero or below
- **THEN** the publisher rejects it and publishes a `Degraded` status instead

### Requirement: Client attach and required domains

An application SHALL attach to the published block and SHALL declare which
domains it requires. Attaching SHALL fail when no block exists or the status is
not usable, and the client SHALL treat a daemon restart that preserves every
required epoch as transparent and SHALL refresh the cached rows on generation
change.

#### Scenario: Attach requires a published base

- **WHEN** an application attaches and no time-base block exists
- **THEN** attach fails with a not-available status and the application does not
  start

#### Scenario: Restart with unchanged epochs is transparent

- **WHEN** the daemon restarts and every required domain keeps its epoch
- **THEN** the client re-reads the new generation and continues without failing

#### Scenario: Epoch change fails fast

- **WHEN** a required domain's epoch changes
- **THEN** the client reports the base as lost instead of applying the old map

### Requirement: Daemon configuration

The daemon SHALL load a configuration naming the declared hardware domains, the
publication interval, the suite identity, the RT thread specification, and the
PTP/TAI policy. Every declared hardware domain SHALL be processed independently;
declared domains that are not discovered SHALL be reported as findings and
degrade the base; an empty declared set SHALL configure a host-only base.

#### Scenario: Declared domain is expected

- **WHEN** the configuration declares a hardware domain that is not discovered
- **THEN** the daemon reports the absence and publishes `Degraded`

#### Scenario: Multiple hardware domains

- **WHEN** the configuration declares several hardware domains
- **THEN** each domain gets its own backend, estimator, epoch, row, and
  per-domain history

#### Scenario: Host-only configuration is valid

- **WHEN** the configuration declares no hardware domains
- **THEN** the daemon starts, publishes host domains, and reports `Ready`

#### Scenario: Invalid configuration is rejected before publication

- **WHEN** the configuration is unreadable or names an invalid domain
- **THEN** the daemon fails initialization and publishes no block

### Requirement: Post-hoc time resolution

Any recorded instant SHALL be resolvable from its native domain, epoch and value
to any other domain through the persisted history and snapshots. An interval
without a history entry or snapshot coverage SHALL be reported as unresolved and
SHALL NOT be resolved by interpolation. UTC SHALL remain derived from the
recorded `Realtime` row, never from the analysing machine's clock.

#### Scenario: Native hardware instant resolves

- **WHEN** an FPGA event carries a domain, epoch and ticks covered by a history
  entry
- **THEN** resolution produces the hub instant and any requested destination
  domain from that entry's validity

#### Scenario: Unknown interval stays unresolved

- **WHEN** an instant falls into a recorded gap
- **THEN** resolution reports it as unresolved rather than choosing a neighbour

#### Scenario: UTC is derived last

- **WHEN** a hub instant is rendered as UTC
- **THEN** the UTC value comes from the recorded `Realtime` row valid at that
  instant, never from the analysing machine's clock

### Requirement: Time-base serialization

`TimeBaseStatus`, `SyncMode`, `ServiceFlags`, `LockState`, `TimeDomainClass`,
and `TimeDomain` SHALL provide `to_string`, `from_string`, non-throwing JSON and
`std::formatter`. `DomainCoefficient`, `TimeBaseBlock`, `TimeSnapshot`, and
`TimeBaseHistoryEntry` SHALL provide `to_string`, formatter and non-throwing JSON
round trips under `JSON_NOEXCEPTION=1`.

#### Scenario: Enums round-trip

- **WHEN** every status, mode, flag, lock-state, class, and domain value passes
  through `to_string`, `from_string`, JSON and `std::format`
- **THEN** every canonical name round-trips and hostile JSON leaves the target
  unchanged

#### Scenario: Value structs round-trip

- **WHEN** a coefficient, block, snapshot, and history entry are serialized and
  parsed back
- **THEN** every field survives and invalid JSON leaves the destination unchanged

#### Scenario: Diagnostics are deterministic

- **WHEN** `to_string()` and `std::format("{}", value)` are evaluated
- **THEN** both render the same text for every time-base value type

## ADDED Requirements

### Requirement: Any-to-any domain conversion

The client SHALL convert a `DomainInstant` (domain, epoch, value) to any
declared destination domain by mapping it to `MonotonicRaw` and then through the
destination reverse map. Conversion SHALL fail when either row is absent, the
epoch does not match, or the shared hub interval is not covered.

#### Scenario: Any pair resolves through the hub

- **WHEN** source and destination rows both cover the same hub interval
- **THEN** conversion returns a `DomainInstant` with the destination epoch and
  value

#### Scenario: Same domain is the identity

- **WHEN** source and destination domains are equal and the epoch matches
- **THEN** conversion returns the input value unchanged

#### Scenario: Invalid interval is unresolved

- **WHEN** one row has no coverage at the converted hub instant
- **THEN** conversion returns no value instead of extrapolating

### Requirement: 1 Hz service snapshot

The daemon SHALL publish a `TimeSnapshot` at 1 Hz carrying generation, boot
token, sync mode, status, service flags, hub and system-scale instants, TAI and
UTC offsets, uncertainty, and every domain's epoch/value/lock state. The
snapshot SHALL NOT duplicate the coefficient table; it SHALL reference the
generation that owns the maps.

#### Scenario: Snapshot carries every domain

- **WHEN** a snapshot is published
- **THEN** every published domain has an epoch, value, and lock state in the
  snapshot

#### Scenario: Snapshot references the map generation

- **WHEN** a snapshot is read together with the history
- **THEN** its generation selects the coefficient rows valid at that instant

#### Scenario: Snapshot serializes losslessly

- **WHEN** a snapshot is serialized to JSON and parsed back under
  `JSON_NOEXCEPTION=1`
- **THEN** every field round-trips and hostile JSON leaves the destination
  unchanged

### Requirement: Real-time daemon execution

`nexigen-chronod` SHALL apply an explicit thread specification, request
`SCHED_FIFO`, attempt `mlockall`, and use absolute sleep deadlines for its
publication cadence. A required attribute failure SHALL be reported through the
startup status; best-effort attributes SHALL be reported as flags.

#### Scenario: Publication cadence does not drift

- **WHEN** the daemon publishes at its configured interval
- **THEN** it sleeps to an absolute deadline rather than a relative duration

#### Scenario: RT setup is observable

- **WHEN** the daemon starts
- **THEN** its thread report and service flags expose the applied scheduling,
  affinity, and memory-locking state

#### Scenario: Required failure stops startup

- **WHEN** a required thread attribute cannot be applied
- **THEN** the daemon fails initialization and publishes no block

### Requirement: Hardware domain access modes

Each declared hardware domain SHALL be read through exactly one access mode:
PHC cross-timestamp, latched register pair, or manual pair. A manual pair SHALL
read the device counter and `CLOCK_MONOTONIC_RAW` in software for modules without
PCIe PTM, record the read latency, and let the estimator treat the pair as
one-sided.

#### Scenario: PHC cross-timestamp

- **WHEN** a domain uses a PTP hardware clock
- **THEN** the daemon reads an atomic cross-timestamp pair from the PHC

#### Scenario: Latched register pair

- **WHEN** firmware exposes the D1-D3 latched register block
- **THEN** the daemon reads the pair under the sequence/CRC protocol

#### Scenario: Manual pair for legacy modules

- **WHEN** a module has no PCIe PTM or latch
- **THEN** the daemon reads the counter and `MONOTONIC_RAW` manually, publishes
  the domain with its measured latency and uncertainty, and never converts the
  counter in the driver

#### Scenario: All declared domains are processed

- **WHEN** several hardware domains are declared
- **THEN** each has a row, estimator, epoch, and snapshot entry
