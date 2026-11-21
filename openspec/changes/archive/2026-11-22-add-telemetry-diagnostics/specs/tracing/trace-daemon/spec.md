# Spec Delta

## ADDED Requirements

### Requirement: Snapshot UTC resolution

Trace-daemon snapshot names and header timestamps SHALL resolve UTC through the attached published time base. The trigger path SHALL NOT read `CLOCK_REALTIME`. A raw offset sampled at most once during session startup MAY serve as a fallback when the base cannot resolve an instant; the daemon SHALL report that fallback on stderr once per session and SHALL still write the snapshot.

#### Scenario: Attached base names the snapshot

- **WHEN** a recovery trigger fires while the time base resolves the trigger instant
- **THEN** the snapshot name and header carry the published-base UTC value and no raw offset is sampled

#### Scenario: Trigger path never reads the wall clock

- **WHEN** a recovery trigger fires with the time base attached
- **THEN** the trigger path performs no `CLOCK_REALTIME` read (verified with fault injection or a `clock_gettime` interposer)

#### Scenario: Unresolved instant uses the startup fallback once

- **WHEN** the base cannot resolve the trigger instant
- **THEN** the snapshot is still written using the offset sampled once at session startup, and the fallback is reported on stderr once for that session

#### Scenario: Startup fallback is sampled at most once

- **WHEN** a session starts without a resolvable base and later triggers fire
- **THEN** only the startup sample reads `CLOCK_REALTIME`; later triggers reuse it
