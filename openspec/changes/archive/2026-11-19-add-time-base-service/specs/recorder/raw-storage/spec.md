# Spec Delta

## MODIFIED Requirements

### Requirement: Session manifest and segment triplet

A session SHALL consist of `session.json` and one or more segments, each with `<session>_<seq>.nxrec`, `<session>_<seq>.nxidx`, and `<session>_<seq>.nxvid`. The manifest SHALL record the format version, session id, time domain, boot token, time-base epoch binding, storage settings, schema/channel tables, and every segment's paths, closed state, record count, gap count, and byte counts. The manifest SHALL be replaced atomically.

#### Scenario: New session creates the triplet and manifest

- **WHEN** a session starts
- **THEN** the manifest exists beside the first `.nxrec`, `.nxidx`, and `.nxvid` files and names each one

#### Scenario: Rotation updates the manifest

- **WHEN** a segment rotates
- **THEN** the closed segment's counts are recorded, the new triplet is appended, and `session.json` is replaced atomically

#### Scenario: Unclosed segment is visible

- **WHEN** the process stops without a footer
- **THEN** the manifest reports the segment as not cleanly closed

#### Scenario: Session is bound to a boot epoch

- **WHEN** a session is opened for recording
- **THEN** its manifest names the boot token and the time-base epoch it was recorded under

### Requirement: Time and sequence mapping

The 48-byte header's commit time SHALL be the recorder's `CLOCK_MONOTONIC_RAW` write time. Event time SHALL stay in the payload or in the record type's documented field, in the native domain the channel declares. The session SHALL declare its time domain; UTC conversion SHALL use the recorded time-base series, never the local wall clock. Headers SHALL carry the 64-bit source sequence and publisher id; a new publisher id or a backwards sequence SHALL start a new publisher epoch.

#### Scenario: Capture time survives storage

- **WHEN** a video frame with `capture_mono_ns` is recorded
- **THEN** the `.nxrec` record and the video reference preserve that capture instant

#### Scenario: UTC is derived, not invented

- **WHEN** a reader converts a stored timestamp to UTC
- **THEN** it uses the recorded time-base series and reports the canonical monotonic value when no UTC calibration covers that instant

#### Scenario: Publisher restart is an epoch

- **WHEN** a publisher id changes or its sequence moves backwards
- **THEN** the reader starts a new epoch instead of reporting a multi-billion-record gap

#### Scenario: Channel declares its native domain

- **WHEN** a channel is declared
- **THEN** the declaration names the native time domain of its event fields, and a hardware channel names the device domain and epoch

## ADDED Requirements

### Requirement: Time-base history persistence

The session SHALL record every published time-base change as an append-only series carrying generation, domain, epoch, map, uncertainty, status, and validity intervals in native and canonical units. Intervals without a published entry SHALL be recorded as explicit gaps.

#### Scenario: Every change is recorded

- **WHEN** the time base publishes a changed domain row during recording
- **THEN** the session gains one history entry for that change and earlier entries remain unchanged

#### Scenario: Daemon downtime is a gap

- **WHEN** the time base is unavailable during recording
- **THEN** the session records a gap for that interval

#### Scenario: History travels with the session

- **WHEN** the session is copied to another machine
- **THEN** the exporter resolves native instants through the embedded history without reading the local clock

#### Scenario: Unresolvable instant is reported

- **WHEN** an instant falls into a recorded gap
- **THEN** extraction reports it as unresolved and does not substitute a neighbouring map
