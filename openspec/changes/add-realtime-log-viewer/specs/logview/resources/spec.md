# Spec Delta

## Purpose

Defines how the viewer stays inside a hard memory budget, keeps every frame fractionally short, throttles itself when the host is loaded, and exports without competing with live capture.

## ADDED Requirements

### Requirement: A hard memory budget

The viewer SHALL enforce a startup memory budget, derive the history and index capacities from it, and SHALL NOT allocate heap memory per received record, per visible row, or per rendered frame in the receive, ingest, filter, or render paths.

#### Scenario: Budget is respected

- **WHEN** the viewer starts with the default budget
- **THEN** its large allocations are derived from that budget and the reported capacity matches the formula

#### Scenario: Allocation failure

- **WHEN** a large buffer cannot be allocated
- **THEN** the viewer exits with a visible error before capture starts

#### Scenario: No per-record allocation

- **WHEN** an instrumented ingest run processes a full history
- **THEN** no allocation occurs after startup except bounded, pre-sized ImGui frame pools

### Requirement: UI work is time-sliced

Filter rebuilds, find scans, and queue drains SHALL be capped per frame so no single frame spends more than 2–3 ms on model work, and long operations SHALL show progress.

#### Scenario: Full filter rebuild

- **WHEN** the user changes a filter over the full history
- **THEN** the rebuild spans several frames, shows progress, and the UI remains interactive

#### Scenario: Find over the full history

- **WHEN** the user searches a 500 000-record history
- **THEN** the scan is chunked and the UI does not freeze

### Requirement: ECO mode protects the host

The viewer SHALL monitor its own process CPU time and SHALL enter ECO mode when a configurable CPU limit is exceeded or the incoming rate exceeds 100 000 messages per second. In ECO it SHALL render at 10–15 FPS by waiting on events, recompute filters and statistics at most every 250 ms, and show the trigger. It SHALL return to NORMAL with hysteresis.

#### Scenario: CPU limit exceeded

- **WHEN** process CPU stays above the configured limit for the entry window
- **THEN** the viewer enters ECO and shows the CPU trigger

#### Scenario: Log storm

- **WHEN** the incoming rate exceeds 100 000 messages per second
- **THEN** the viewer enters ECO even if CPU is below the limit

#### Scenario: Recovery

- **WHEN** load stays below the exit threshold for the recovery window
- **THEN** the viewer returns to NORMAL

#### Scenario: Input still wakes the UI

- **WHEN** the user moves the mouse or presses a key in ECO
- **THEN** the UI reacts immediately instead of waiting for the next ECO frame

### Requirement: Threads are low priority and placeable

The viewer SHALL run capture on a non-real-time low-priority thread and export on a lower-priority thread, and SHALL allow optional CPU affinity for both. An invalid thread specification SHALL be rejected at startup.

#### Scenario: Affinity is applied

- **WHEN** the configuration names a CPU set for capture
- **THEN** the receive thread runs only on those CPUs

#### Scenario: Export yields to capture

- **WHEN** an export runs while capture is active
- **THEN** the export thread runs at lower priority and capture keeps draining the transport

#### Scenario: Invalid thread spec

- **WHEN** an affinity or priority value is rejected by the OS
- **THEN** startup reports the rejected attribute instead of silently running unpinned

### Requirement: Export is asynchronous

Export SHALL run on a writer thread fed by a bounded queue owned by the UI thread. The UI SHALL stay responsive, report progress, and report records that were evicted before they could be queued.

#### Scenario: Export while live

- **WHEN** the user exports 100 000 selected records while frames keep arriving
- **THEN** the UI stays responsive and the file completes with a progress indicator

#### Scenario: Eviction during export

- **WHEN** a selected record is evicted before it is queued for writing
- **THEN** the export reports it as a gap instead of writing stale content

#### Scenario: Cancel

- **WHEN** the user cancels an export
- **THEN** the writer stops, the partial file is closed, and the UI returns to normal
