# logview/capture Specification

## Purpose

Defines how the viewer attaches to the live log service, validates frames, moves them off the transport without stalling the UI, and makes every lost record visible instead of silently absent.

## Requirements

### Requirement: Attaches to the live log service

The viewer SHALL subscribe to the configured log service (default `Nexigen/Log/nexigen`) and SHALL display frames from the moment of attach. It SHALL NOT claim, require, or wait for a backlog; with no frame received it SHALL show an explicit waiting state.

#### Scenario: Attach before any publisher

- **WHEN** the viewer starts while no application is logging
- **THEN** it opens or joins the service, shows a waiting state, and displays the first frame published afterwards

#### Scenario: Attach to a running suite

- **WHEN** applications are already publishing when the viewer starts
- **THEN** the next received frame appears without restarting the applications

#### Scenario: Idle service costs no CPU

- **WHEN** no frame is published for an extended period
- **THEN** the receive loop sleeps in the kernel and the viewer's CPU share stays at the idle level

### Requirement: Rejects unknown wire versions

The viewer SHALL discard a frame whose wire version is not the supported `LogFrame` version and SHALL count it as a protocol rejection without interpreting its offsets.

#### Scenario: Unsupported version is skipped

- **WHEN** a frame with an older or newer wire version arrives
- **THEN** it is not added to the history, the rejection counter increases, and valid frames continue to render

#### Scenario: Rejection is visible in diagnostics

- **WHEN** the transport status is opened after a rejection
- **THEN** it reports the number of rejected frames

### Requirement: Capture never blocks the UI

The capture path SHALL run independently of the UI thread and SHALL keep draining the transport while the UI is busy. When the fixed hand-off queue is full, capture SHALL continue and count the records it could not retain.

#### Scenario: A UI stall does not stop capture

- **WHEN** the UI thread is busy for 100 ms while frames keep arriving
- **THEN** the transport is still drained, no publisher fails because of the viewer, and the UI catches up afterwards

#### Scenario: Overflow is counted

- **WHEN** more frames arrive during a stall than the hand-off queue can hold
- **THEN** the excess is counted and reported as a gap instead of being silently dropped

### Requirement: Losses become visible gap records

The viewer SHALL represent each detected loss as a distinct gap record carrying the number of missing records. Losses detected from the transport sequence, from the hand-off queue, and from the publisher's drop counter SHALL each be attributed and counted separately.

#### Scenario: Transport sequence gap

- **WHEN** the transport reports that N frames were missed before the received one
- **THEN** one gap record stating N appears at that position in the history

#### Scenario: Hand-off overflow

- **WHEN** the hand-off queue drops records during a UI stall
- **THEN** a gap record stating the dropped count appears when ingestion resumes

#### Scenario: Publisher drop counter

- **WHEN** a received frame carries a non-zero publisher drop counter
- **THEN** the loss is shown next to that frame and counted in the session totals

### Requirement: Transport status is observable

The viewer SHALL expose the active service name, connected publisher count, received and lost counters, hand-off queue occupancy, and the last transport error without stopping the UI when an error occurs.

#### Scenario: Publisher restart is visible

- **WHEN** an application publisher disconnects and reconnects
- **THEN** the status panel updates the publisher count and the sequence baseline continues correctly

#### Scenario: Receive error does not kill the view

- **WHEN** the transport reports an error while receiving
- **THEN** the error is shown in the status and the viewer keeps the stored history interactive
