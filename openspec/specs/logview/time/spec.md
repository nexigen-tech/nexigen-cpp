# logview/time Specification

## Purpose

Defines how the viewer turns monotonic frame timestamps into readable time, how deltas are computed across filter and application boundaries, and how the T0 anchor makes a single record the reference for analysis.

## Requirements

### Requirement: Timestamp display modes

The viewer SHALL offer Absolute (UTC), Since Boot, and Relative to T0. Absolute time SHALL come from the platform time base when it is attached; otherwise the viewer SHALL use a sampled UTC offset and SHALL show an explicit no-time-base state instead of presenting a guess as calibrated UTC.

#### Scenario: Time base is available

- **WHEN** the platform time base is attached
- **THEN** Absolute renders calibrated UTC with millisecond and microsecond fields

#### Scenario: Time base is missing

- **WHEN** no time base is attached
- **THEN** Absolute uses the sampled offset, the toolbar shows a no-time-base badge, and Since Boot remains exact

#### Scenario: Since boot

- **WHEN** the user selects Since Boot
- **THEN** each row renders the frame's monotonic timestamp as an offset from boot

#### Scenario: Relative to anchor

- **WHEN** the user selects Relative to T0
- **THEN** each row renders a signed offset from the anchor record

### Requirement: Delta computation modes

The viewer SHALL compute Delta as the difference from the previous visible record, from the previous record of the same application, or from the anchor. Deltas SHALL use adaptive units and SHALL show a negative value as negative.

#### Scenario: Previous visible

- **WHEN** the filter changes the visible order
- **THEN** Delta is recomputed against the previous record in the visible order

#### Scenario: Per application

- **WHEN** several applications interleave
- **THEN** Delta compares each record only with the previous record from the same application

#### Scenario: From anchor

- **WHEN** an anchor is set
- **THEN** records before it show a negative offset and records after it a positive one

#### Scenario: Negative delta is honest

- **WHEN** two application workers produce timestamps out of order
- **THEN** the delta is shown as negative and is not clamped to zero

### Requirement: Anomalous deltas are highlighted

The viewer SHALL highlight a delta above a configurable threshold and SHALL persist the threshold across restarts.

#### Scenario: Threshold crossed

- **WHEN** a delta exceeds the configured threshold
- **THEN** the cell is highlighted

#### Scenario: Threshold persists

- **WHEN** the user changes the threshold and restarts
- **THEN** the new value is restored

### Requirement: One anchor record

The viewer SHALL set the anchor from the row context menu or the T key, SHALL highlight the anchor row, and SHALL keep at most one anchor. Setting a new anchor SHALL replace the previous one.

#### Scenario: Set and replace

- **WHEN** the user sets an anchor on row A and later on row B
- **THEN** only B is marked and all relative offsets use B

#### Scenario: Clear the anchor

- **WHEN** the user clears the anchor
- **THEN** Relative-to-T0 and From-Anchor become unavailable until a new anchor is set

### Requirement: Arrival order is preserved

The viewer SHALL NOT sort records by timestamp. Time display modes SHALL change formatting only, never the order of the history.

#### Scenario: Out-of-order arrival

- **WHEN** a later-arriving record carries an earlier timestamp
- **THEN** it stays at its arrival position and its delta may be negative

#### Scenario: Switching modes

- **WHEN** the user switches timestamp or delta mode
- **THEN** the row order is unchanged
