# Spec Delta

## ADDED Requirements

### Requirement: Host counter certification

The publisher SHALL certify the host cycle counter before it publishes the
`CycleCounter` row. Certification SHALL treat the counter as one coherent
domain only when the canonical instants implied on every online CPU agree
within 50 microseconds. An uncertified counter SHALL remain unpublished and the
publication SHALL be `Degraded`.

#### Scenario: Coherent counter is published

- **WHEN** every online CPU implies the same canonical instant within the
  tolerance
- **THEN** the publication is `Ready` and carries the `CycleCounter` row with
  its measured map and uncertainty

#### Scenario: Incoherent counters are rejected

- **WHEN** two CPUs imply canonical instants more than 50 microseconds apart
- **THEN** the probe reports the counter as unsynchronized and the publication
  is `Degraded` with no `CycleCounter` row

### Requirement: Jitter-tolerant probe sampling

The certification probe SHALL read the canonical clock on both sides of each
counter read and use the midpoint of the pair, and SHALL keep the tightest of a
bounded number of attempts, so a scheduling delay inside a sample cannot be
reported as counter skew. The probe SHALL restore the calling thread's affinity
mask on every exit path.

#### Scenario: Scheduling jitter is not counter skew

- **WHEN** a sample of the certification probe is preempted between its
  bracketed reads
- **THEN** the probe retries the sample and certifies the counter from the
  tightest attempt, and the publication status stays `Ready`

#### Scenario: Certification preserves the caller's affinity

- **WHEN** the probe finishes, whether it certifies or rejects the counter
- **THEN** the calling thread keeps the affinity mask it had before the probe
