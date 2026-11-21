# Spec Delta

## REMOVED Requirements

### Requirement: Snapshot UTC resolution

**Reason**: The startup raw-offset fallback let the daemon name snapshots from `CLOCK_REALTIME` even when a published base existed, contradicting ADR 0001's "runtime never reads the wall clock" rule.

**Migration**: Publish `/nexigen-timebase` (or attach the base before starting the daemon). Trigger instants outside the published horizon now fail the snapshot write and are counted instead of using a fallback offset.

## ADDED Requirements

### Requirement: Snapshot UTC resolution requires the time base

Trace-daemon snapshot names and header timestamps SHALL resolve UTC through the attached published time base. The daemon SHALL require a usable base at session start and SHALL fail startup when none is attached or attachable. The trigger path SHALL NOT read `CLOCK_REALTIME` and SHALL NOT use a stored raw offset; when the base cannot resolve a trigger instant, the snapshot write SHALL fail with `NotInitialized` and SHALL be counted as a dump failure without stopping the daemon.

#### Scenario: Attached base names the snapshot

- **WHEN** a recovery trigger fires while the time base resolves the trigger instant
- **THEN** the snapshot name and header carry the published-base UTC value and no raw offset is sampled

#### Scenario: Trigger path never reads the wall clock

- **WHEN** a recovery trigger fires
- **THEN** the trigger path performs no `CLOCK_REALTIME` read (verified with a `clock_gettime` interposer)

#### Scenario: Startup requires a published base

- **WHEN** a session starts with no attached base and none can be attached under the configured name
- **THEN** `start()` fails with the attach error before opening ports, and no snapshot fallback state is created

#### Scenario: Unresolved trigger fails the snapshot

- **WHEN** the attached base cannot resolve the trigger instant (outside its validity window)
- **THEN** no snapshot file is written, the failure is counted in the daemon's dump failures, and the drain loop continues

#### Scenario: No wall-clock sample is taken at startup

- **WHEN** a session starts with a usable base
- **THEN** startup performs no `CLOCK_REALTIME` read and stores no raw offset
