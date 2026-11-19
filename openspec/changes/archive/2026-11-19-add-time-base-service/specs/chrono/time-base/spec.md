# Spec Delta

## Purpose

Defines the platform time base as a service: one canonical monotonic scale, a table of
host and hardware clock domains mapped into it, the durable history of those maps, and
the readiness contract applications rely on to trust time.

## ADDED Requirements

### Requirement: Canonical monotonic scale

`CLOCK_MONOTONIC_RAW` nanoseconds SHALL be the canonical monotonic scale for the
platform. Every other domain — host TSC, each hardware timer — SHALL be related to the
canonical scale by an explicitly published affine map and SHALL NOT be assumed equal to
it. The canonical domain's epoch SHALL change only with the kernel boot.

#### Scenario: Canonical row is always present

- **WHEN** a time-base block is published
- **THEN** it contains the canonical domain row with an identity map and zero uncertainty

#### Scenario: Host cycle counter is an ordinary domain

- **WHEN** the platform uses the TSC fast path
- **THEN** the TSC appears as a domain with its own measured map, uncertainty and status,
  and no component treats its value as canonical

#### Scenario: No hardware domains is a valid configuration

- **WHEN** no interface module is declared or present
- **THEN** the published block contains only host domains and the base can be `Ready`

### Requirement: Domain coefficient row

Each published row SHALL carry a domain identifier, a domain kind, an epoch identifier,
a native unit, an affine map into the canonical scale with a strictly positive scale
factor, an uncertainty in nanoseconds, a lock state, and validity limits expressed in
both native and canonical units.

#### Scenario: Map converts a native instant

- **WHEN** a native value inside a row's validity interval is converted
- **THEN** the result equals the row's offset plus the native value multiplied by its
  scale factor, and lies inside the row's canonical validity interval

#### Scenario: Validity is expressed in both domains

- **WHEN** a consumer needs the row that applies to a native hardware stamp
- **THEN** it finds the row by native validity without first knowing the canonical instant

#### Scenario: Non-positive scale is rejected

- **WHEN** a row would carry a scale factor of zero or below
- **THEN** the publisher rejects it and publishes a `Degraded` status instead

### Requirement: Shared-memory publication contract

The daemon SHALL publish the domain table as one fixed-size block in shared memory that
exactly one writer owns and any process may read. A read SHALL observe either the
previous or the new complete block, never a mixture, and a reader that attaches late
SHALL read the latest block immediately.

#### Scenario: Readers never observe a torn block

- **WHEN** a reader copies the block while the writer updates it
- **THEN** the copy belongs entirely to one generation and the reader retries on a
  generation change

#### Scenario: Late reader gets the current block

- **WHEN** a process attaches after the daemon has published several blocks
- **THEN** its first read returns the latest block without waiting for an update

#### Scenario: Single writer is enforced

- **WHEN** a second process attempts to publish to an existing block
- **THEN** the attempt is rejected and the existing writer keeps ownership

### Requirement: Time-base status

The block SHALL carry a status of `NotReady`, `Ready`, `Degraded` or `Lost`, a
monotonically increasing generation, a heartbeat instant, and the boot token. Status
SHALL distinguish an absent optional domain from a lost required one, and a stale
heartbeat SHALL be detectable by readers without the daemon's cooperation.

#### Scenario: Ready with no hardware

- **WHEN** only host domains are declared and their maps are valid
- **THEN** the status is `Ready`

#### Scenario: Declared but absent domain degrades

- **WHEN** a configured hardware domain is not discovered
- **THEN** the status is `Degraded` and the domain is reported as absent

#### Scenario: Stale heartbeat is observable

- **WHEN** the daemon stops updating the block
- **THEN** a reader detects the stale heartbeat from the block alone and treats the base
  as lost after the holdover window

### Requirement: Coefficient history

The daemon SHALL append every published change of a domain row to an append-only history
that records the generation, domain, epoch, map, uncertainty, status and validity
intervals in both domains. Intervals without a published row — including daemon downtime
— SHALL be recorded as explicit gaps rather than covered by a neighbouring row.

#### Scenario: Every change is appended

- **WHEN** a domain row changes its map, epoch, lock state or uncertainty
- **THEN** a new history entry is appended and no earlier entry is modified or removed

#### Scenario: Downtime is a gap

- **WHEN** the daemon is unavailable for an interval
- **THEN** that interval appears as a gap in the history and is not resolved from an
  adjacent entry

#### Scenario: History survives a daemon restart

- **WHEN** the daemon restarts and the domain epochs are unchanged
- **THEN** the history continues with a new generation and the earlier entries remain
  valid for their intervals

### Requirement: Client attach and required domains

An application SHALL attach to the published block and SHALL declare which domains it
requires. Attaching SHALL fail when no block exists or the status is not usable, and the
client SHALL treat a daemon restart that preserves every required epoch as transparent.

#### Scenario: Attach requires a published base

- **WHEN** an application attaches and no time-base block exists
- **THEN** attach fails with a not-available status and the application does not start

#### Scenario: Restart with unchanged epochs is transparent

- **WHEN** the daemon restarts and every required domain keeps its epoch
- **THEN** the client re-reads the new generation and continues without failing

#### Scenario: Epoch change fails fast

- **WHEN** a required domain's epoch changes
- **THEN** the client reports the base as lost instead of applying the old map

### Requirement: Holdover and fail-fast

When a required domain is lost or the heartbeat becomes stale, the client SHALL enter
holdover for at most two seconds, measured on the canonical scale, during which records
may still be produced and the cause is recorded once. At expiry the client SHALL stop
with the `time_base_lost` reason and SHALL NOT retry on its own.

#### Scenario: Holdover is bounded

- **WHEN** the required domain is lost and not restored within two seconds of canonical
  time
- **THEN** the client stops with `time_base_lost` and performs no self-retry

#### Scenario: Recovery inside the window is silent

- **WHEN** the daemon returns with the same epochs inside the holdover window
- **THEN** the client leaves holdover without stopping and records no failure

#### Scenario: Non-required degradation does not stop the client

- **WHEN** a domain the client did not declare as required degrades or disappears
- **THEN** the client continues and the domain is reported as unavailable

### Requirement: Daemon configuration

The daemon SHALL load a configuration naming the declared hardware domains, the
publication interval and the suite identity. Declared domains that are not discovered
SHALL be reported as findings, and an empty declared set SHALL configure a host-only
base.

#### Scenario: Declared domain is expected

- **WHEN** the configuration declares a hardware domain that is not discovered
- **THEN** the daemon reports the absence and publishes `Degraded`

#### Scenario: Host-only configuration is valid

- **WHEN** the configuration declares no hardware domains
- **THEN** the daemon starts, publishes host domains, and reports `Ready`

#### Scenario: Invalid configuration is rejected before publication

- **WHEN** the configuration is unreadable or names an invalid domain
- **THEN** the daemon fails initialization and publishes no block

### Requirement: Post-hoc time resolution

Any recorded instant SHALL be resolvable from its native domain, epoch and value to the
canonical scale through the persisted history, and from canonical to UTC through the
recorded UTC series. An interval without a history entry SHALL be reported as
unresolved and SHALL NOT be resolved by interpolation.

#### Scenario: Native hardware instant resolves

- **WHEN** an FPGA event carries a domain, epoch and ticks covered by a history entry
- **THEN** resolution produces the canonical instant of that entry's validity

#### Scenario: Unknown interval stays unresolved

- **WHEN** an instant falls into a recorded gap
- **THEN** resolution reports it as unresolved rather than choosing a neighbour

#### Scenario: UTC is derived last

- **WHEN** a canonical instant is rendered as UTC
- **THEN** the UTC value comes from the recorded series valid at that instant, never from
  the analysing machine's clock

### Requirement: Time-base serialization

`TimeBaseStatus`, `DomainKind` and `LockState` SHALL provide `to_string`, `from_string`,
non-throwing JSON and `std::formatter`. `DomainCoefficient`, `TimeBaseBlock` and
`TimeBaseHistoryEntry` SHALL provide `to_string`, formatter and non-throwing JSON round
trips under `JSON_NOEXCEPTION=1`.

#### Scenario: Enums round-trip

- **WHEN** every status, domain-kind and lock-state value passes through `to_string`,
  `from_string`, JSON and `std::format`
- **THEN** every canonical name round-trips and hostile JSON leaves the target unchanged

#### Scenario: Value structs round-trip

- **WHEN** a coefficient, block and history entry are serialized and parsed back
- **THEN** every field survives and invalid JSON leaves the destination unchanged

#### Scenario: Diagnostics are deterministic

- **WHEN** `to_string()` and `std::format("{}", value)` are evaluated
- **THEN** both render the same text for every time-base value type
