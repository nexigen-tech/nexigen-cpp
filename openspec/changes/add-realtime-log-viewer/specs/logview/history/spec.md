# Spec Delta

## Purpose

Defines the viewer's bounded record history: how its capacity is derived, how records keep a stable identity across eviction, and what pause means when the stream keeps arriving.

## ADDED Requirements

### Requirement: Capacity comes from the memory budget

The viewer SHALL derive the history capacity from a configured hard memory budget (`max_memory_mb`, default 256), rounded down to a power of two, and SHALL allocate it once at startup. The history SHALL NOT grow or reallocate at runtime.

#### Scenario: Default budget selects the default capacity

- **WHEN** the viewer starts with a 256 MB budget
- **THEN** the history holds 524 288 records and the reported footprint stays within the budget

#### Scenario: A larger budget raises the capacity

- **WHEN** the budget is raised to 384 MB
- **THEN** the history holds 1 048 576 records

#### Scenario: Allocation failure stops startup

- **WHEN** the history cannot be allocated
- **THEN** the viewer exits with a visible error before starting capture or rendering

### Requirement: Records keep an absolute identity

Every record retained in the history SHALL carry a monotonically increasing 64-bit number assigned at ingest. Eviction SHALL NOT renumber surviving records or change their identity.

#### Scenario: Numbers increase across the session

- **WHEN** records are ingested one by one
- **THEN** each receives the next number and the numbers are never reused

#### Scenario: Eviction does not renumber

- **WHEN** the history wraps and the oldest record is evicted
- **THEN** every surviving record keeps its original number and the oldest retained number increases

#### Scenario: Imported records get a fresh sequence

- **WHEN** a saved file is opened
- **THEN** its records are numbered from the first imported record

### Requirement: Oldest records are overwritten

When the history is full, ingesting a record SHALL evict the oldest retained record. The viewer SHALL count evictions and SHALL report the count rather than silently replacing data.

#### Scenario: One record over capacity

- **WHEN** the history holds its full capacity and one more record is ingested
- **THEN** the oldest record is gone, the eviction counter increases by one, and the newest record is present

#### Scenario: Continuous stream

- **WHEN** a stream keeps arriving at a steady rate
- **THEN** the eviction counter grows with the number of overwritten records

### Requirement: Pause freezes the view, not capture

Pause SHALL freeze the displayed range while capture and ingestion continue. When eviction reaches the frozen floor, the viewer SHALL move the floor to the oldest retained record and show a banner stating how many displayed records were overwritten. Resume SHALL follow the newest record.

#### Scenario: Pause and long overflow

- **WHEN** the user pauses, then more than the configured capacity of records arrives
- **THEN** the view advances to the oldest retained record and a banner reports the overwritten count

#### Scenario: Resume returns to live

- **WHEN** the user resumes after a pause
- **THEN** the table follows the newest record and the live badge returns

#### Scenario: Manual scroll stops auto-scroll

- **WHEN** the user scrolls the table up while live
- **THEN** auto-scroll stops and a "return to live" control appears

### Requirement: Received and synthetic records are distinct

The history SHALL distinguish received frames from gap and overflow records. Synthetic records SHALL NOT be counted as received frames and SHALL render with a distinct treatment.

#### Scenario: Gap record is distinguishable

- **WHEN** a gap record is displayed
- **THEN** it is visually distinct from a frame, states the missing count, and is excluded from the received counter

#### Scenario: Counters stay split

- **WHEN** the session includes frames and synthetic records
- **THEN** the toolbar reports received, synthetic, and evicted counts separately
