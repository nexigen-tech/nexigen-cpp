# hw/can/viewer Specification

## Purpose

Defines `nexigen-canview`, the read-only real-time CAN/CAN-FD monitor: bounded capture and history, independent views, filtering/find, hardware and estimated-UTC time presentation, bus-load heatmap, resource guards, and the GUI/headless/CLI surface.

## Requirements

### Requirement: The viewer captures read-only CAN channels

The viewer SHALL subscribe to the `rx` and `status` services of every enabled channel, SHALL NOT create `tx` publishers, and SHALL treat the absence of a publisher as a waiting state rather than an error.

#### Scenario: Eight live channels
- **WHEN** eight channel profiles are enabled
- **THEN** the viewer subscribes to all eight `rx` streams and all eight `status` streams

#### Scenario: Ninth live channel is rejected
- **WHEN** nine profiles are enabled for live capture
- **THEN** startup fails with a diagnostic naming the live limit and no service is created

#### Scenario: Publisher appears later
- **WHEN** the viewer starts before any CAN publisher exists and a publisher appears later
- **THEN** frames from that channel become visible without restarting the viewer

#### Scenario: No transmit service
- **WHEN** the viewer is running
- **THEN** no `nexigen/hw/can/<channel>/tx` service is created

### Requirement: Channels have a stable name and numeric identity

A channel SHALL use its validated `name` to derive service names and its unique `channel_id` as the record key; duplicate names or ids SHALL be rejected before capture starts.

#### Scenario: Service name derives from the channel name
- **WHEN** a channel named `powertrain` is configured
- **THEN** the viewer subscribes to `nexigen/hw/can/powertrain/rx` and `.../status`

#### Scenario: Mismatched channel id is rejected
- **WHEN** a frame with `channel_id` 2 arrives on the service bound to `channel_id` 1
- **THEN** the frame is not written to history and the mismatch counter grows

#### Scenario: Duplicate profile is rejected
- **WHEN** two profiles share a name or a `channel_id`
- **THEN** configuration validation fails before any subscription is created

### Requirement: Every loss is visible and classified

The viewer SHALL create a synthetic loss record for every detected hardware-sequence gap, transport loss, ingress overflow, driver drop, and driver-reported sequence gap, with the affected channel, a reason, and a count when known.

#### Scenario: Hardware-sequence gap
- **WHEN** a frame arrives with a hardware sequence that skips five values
- **THEN** a gap record with reason `HardwareSequence` and count 5 appears immediately before that frame

#### Scenario: Ingress overflow
- **WHEN** the capture-to-UI queue is full and cannot accept a frame
- **THEN** the overflow is counted and an `IngressOverflow` record appears in history at the next drain

#### Scenario: Driver drop
- **WHEN** a status sample reports a larger `dropped_count` than the previous sample for that channel
- **THEN** a `DriverDrop` record with the positive delta appears in history

#### Scenario: Sequence status is not double-counted
- **WHEN** frames carry a valid hardware sequence and status also reports `sequence_gap_count`
- **THEN** the status counter is displayed but does not create a second gap record

### Requirement: History is bounded and absolutely numbered

The viewer SHALL allocate one fixed history before capture, SHALL assign a monotonically increasing 64-bit `seq` to every record, and SHALL evict the oldest record when full without renumbering retained records.

#### Scenario: Default history capacity
- **WHEN** the memory budget is 256 MiB with default view settings
- **THEN** the history capacity is 1 048 576 records

#### Scenario: Large history capacity
- **WHEN** the memory budget is 384 MiB with default view settings
- **THEN** the history capacity is 2 097 152 records

#### Scenario: Eviction keeps identity
- **WHEN** the history is full and a new record arrives
- **THEN** the oldest record is evicted, the eviction counter grows, and every retained record keeps its existing `seq`

#### Scenario: No runtime growth
- **WHEN** capture is running
- **THEN** the history mapping and its side arrays are never resized

### Requirement: Records distinguish frames, bus silence, and synthetic losses

The viewer SHALL store exactly five record kinds: `Frame`, `BusIdleHeartbeat`, `TransportGap`, `IngressOverflow`, and `DriverDrop`; only `Frame` SHALL count as a received frame.

#### Scenario: BusIdle is stored but hidden by default
- **WHEN** a `BusIdle` message arrives
- **THEN** it is stored as `BusIdleHeartbeat`, updates the bus-load heatmap, and is hidden by the default table filter

#### Scenario: Synthetic record is not a frame
- **WHEN** a gap record is present in history
- **THEN** it is displayed with its reason and count and does not increase the received-frame count

#### Scenario: Invalid frame is retained
- **WHEN** a frame violates the viewer's CAN/CAN-FD structural rules
- **THEN** it is stored as `Frame` with an `InvalidStruct` marker, counted as rejected, and does not interrupt subsequent frames

#### Scenario: Remote frame with requested length
- **WHEN** a remote frame arrives with `dlc` 6
- **THEN** it is accepted and displayed with length 6

### Requirement: Views have independent scopes and state

The viewer SHALL provide `All`, `Channel`, and `Node` views, each with independent filters, table mode, visible index, find, selection, bookmarks, minimap, pause position, time settings, T0, and export selection.

#### Scenario: Default workspace
- **WHEN** no views are persisted
- **THEN** the viewer opens one `All channels` LIVE view

#### Scenario: Node view is scope-bound
- **WHEN** a node view for `(channel 1, node 2)` is open
- **THEN** it shows only records decoded to that binding and additional filters cannot widen the scope

#### Scenario: Channel scopes are isolated
- **WHEN** the operator changes a filter in one view
- **THEN** every other view keeps its own filter and visible set

#### Scenario: Orphaned node view
- **WHEN** the EDS binding for an open node view is removed
- **THEN** the view becomes orphaned, shows a banner, and recovers when the binding returns

### Requirement: View counts are bounded

The viewer SHALL allow at most eight LIVE views including `All` and at most sixteen PAUSED views; exceeding a limit SHALL be rejected with a visible message and no partially created view.

#### Scenario: Ninth LIVE view
- **WHEN** eight LIVE views are open and a ninth is requested
- **THEN** the request is rejected and the existing eight remain usable

#### Scenario: Seventeenth PAUSED view
- **WHEN** sixteen PAUSED views exist and another clone is requested
- **THEN** the clone is rejected and no snapshot is retained

### Requirement: Clones freeze a bounded snapshot

A `LIVE` clone SHALL follow the shared history; a `PAUSED` clone SHALL copy an immutable snapshot of at most 1 MiB of record data, newest to oldest, independent of later traffic.

#### Scenario: PAUSED clone is isolated
- **WHEN** a PAUSED clone is created on a 5 000 frames/s stream
- **THEN** its snapshot does not change as new frames arrive

#### Scenario: Clone clipping
- **WHEN** a PAUSED clone is created with a selection or T0 outside the snapshot
- **THEN** outside selection/bookmarks are dropped and an outside T0 is cleared with a visible summary

### Requirement: Trace presents chronological records

`Trace` SHALL render every visible record in arrival order with a virtualized table, optional auto-scroll, and columns for bookmark, sequence, hardware time, optional estimated UTC, delta, channel, node/service, identifier, kind, flags, length, and data/decoded text.

#### Scenario: Auto-scroll follows new records
- **WHEN** a LIVE view has auto-scroll enabled and a new visible record arrives
- **THEN** the table follows the newest record

#### Scenario: Manual scroll disables following
- **WHEN** the operator scrolls away from the newest record
- **THEN** auto-scroll turns off without pausing capture

#### Scenario: Two time columns
- **WHEN** the view's time-column mode is `Both`
- **THEN** the table shows separate hardware-time and estimated-UTC columns

### Requirement: Overwrite presents one row per CAN key

`Overwrite` SHALL group frames by `(channel_id, can_id, extended, kind)`, show the latest payload and period statistics, highlight changed bytes with a configurable fade, and exclude synthetic loss and BusIdle records.

#### Scenario: Latest state per key
- **WHEN** two frames with the same key arrive
- **THEN** the row shows the second payload and increments `Count`

#### Scenario: Period statistics
- **WHEN** a key receives frames at a stable period
- **THEN** the row shows the current period, minimum, maximum, average, and peak-to-peak jitter over the configured window

#### Scenario: Gap resets statistics
- **WHEN** a gap affects a key's channel
- **THEN** that key's period window resets and the next gap-sized delta is marked anomalous rather than polluting min/max/jitter

#### Scenario: Changed bytes fade
- **WHEN** a payload byte changes
- **THEN** it is highlighted and the highlight fades out after the configured duration

#### Scenario: Row capacity is bounded
- **WHEN** the overwrite row table is full
- **THEN** the least recently updated row is evicted and the eviction counter grows

### Requirement: Overwrite selection and export are key-based

Overwrite selection SHALL identify keys, not individual records; `Save selected` SHALL export the latest record of each selected key.

#### Scenario: Selection survives a new frame
- **WHEN** a selected key receives a newer frame
- **THEN** the key remains selected and the next export uses the newer record

#### Scenario: Select all visible
- **WHEN** the operator presses `Ctrl+A` in Overwrite
- **THEN** the latest record of every visible row becomes selected

#### Scenario: All frames for selected keys
- **WHEN** the operator chooses the all-frames action for selected keys
- **THEN** every retained frame of those keys is exported in sequence order

### Requirement: Filters compose per view

Each view SHALL support a channel mask, CAN ID with mask, CANopen node mask, kind mask, origin mask, errors-only, show-BusIdle, show-gaps, and a text/hex query, and SHALL rebuild its visible Trace index in bounded chunks.

#### Scenario: Identifier mask
- **WHEN** the identifier filter is `id=0x180, mask=0x780`
- **THEN** only identifiers whose masked value equals `0x180` are visible

#### Scenario: Errors-only
- **WHEN** errors-only is enabled
- **THEN** Error, Overload, invalid frames, and all gap records remain visible while data and BusIdle rows are hidden

#### Scenario: ID filter does not hide gaps
- **WHEN** an identifier filter hides a CAN ID
- **THEN** gap records on visible channels remain visible

#### Scenario: Incremental rebuild
- **WHEN** the filter changes on a full history
- **THEN** the rebuild processes at most 20 000 records per rendered frame and shows progress

### Requirement: CANopen nodes filter by channel and node

The node filter SHALL operate on configured `(channel_id, node_id)` bindings, not on a global node number.

#### Scenario: Same node number on two channels
- **WHEN** node 2 is bound on channel 1 and channel 2
- **THEN** the operator can show or hide each binding independently

#### Scenario: Unbound frames
- **WHEN** a node filter is active and a frame is not decoded to a selected binding
- **THEN** the frame is hidden

### Requirement: Find searches visible order without filtering

Find SHALL highlight matches in visible order, keep the table unchanged, and support next/previous navigation; it SHALL not hide non-matching records.

#### Scenario: Trace find
- **WHEN** the operator searches for a payload byte sequence in a Trace view
- **THEN** matching records are highlighted, non-matching records remain, and `Enter` moves to the first match

#### Scenario: Cyclic navigation
- **WHEN** the last match is focused and next is requested
- **THEN** the first match is focused

#### Scenario: Zero matches
- **WHEN** the query has no matches
- **THEN** the view shows `0 matches` and the table is unchanged

#### Scenario: Overwrite find
- **WHEN** the operator searches in Overwrite mode
- **THEN** matches are evaluated over visible rows' latest records, not over the whole history

### Requirement: Time presentation keeps hardware time authoritative

The viewer SHALL store hardware time per record, SHALL support `Driver`, `Since session start`, and `Relative T0` modes, and SHALL compute all deltas from hardware time in signed nanoseconds.

#### Scenario: Per-ID delta
- **WHEN** a cyclic frame with a 10 ms period has one 60 ms outlier
- **THEN** `Per CAN ID` shows 10.000 ms normally and marks the 60.000 ms outlier as anomalous

#### Scenario: Cross-channel delta is not fabricated
- **WHEN** `assume_shared_hardware_timebase` is false and the previous visible record is from another channel
- **THEN** the cross-channel delta is shown as unavailable with a reason

#### Scenario: T0 is channel-bound
- **WHEN** T0 is set on channel 1 and a channel 2 row is shown in `Relative T0`
- **THEN** the channel 2 delta is unavailable unless the shared-timebase flag is enabled

### Requirement: UTC is an explicitly estimated presentation

The viewer SHALL derive optional UTC from a per-channel anchor built from the publisher's ingest timestamp and the platform time base, SHALL label it `UTC (est.)`, and SHALL not fabricate a UTC value when the anchor is unavailable.

#### Scenario: Time base available
- **WHEN** the time-base service is attached and a valid anchor exists
- **THEN** the UTC column shows an estimated value with a tooltip naming the anchor source, offset, drift, and age

#### Scenario: Time base unavailable
- **WHEN** the time-base service is not attached
- **THEN** the UTC column shows `-` and Status reports the missing time base

#### Scenario: Anchor jump
- **WHEN** the estimated UTC offset jumps beyond the material-change threshold
- **THEN** a new time epoch is recorded, the event is flagged, and affected records keep their epoch's presentation

#### Scenario: No fabricated timestamp
- **WHEN** a record has no usable hardware timestamp
- **THEN** its UTC value is unavailable and no ISO timestamp is emitted

### Requirement: Cross-channel timebase is configurable

The viewer SHALL default to per-channel hardware time and SHALL allow an explicit shared-hardware-timebase mode that enables cross-channel deltas, T0, and a shared heatmap axis.

#### Scenario: Default mode
- **WHEN** the shared-timebase flag is false
- **THEN** cross-channel deltas and T0 are unavailable and the heatmap defaults to per-channel lanes

#### Scenario: Shared mode
- **WHEN** the shared-timebase flag is true
- **THEN** cross-channel deltas, T0, and a shared heatmap axis are available

#### Scenario: Divergent clocks warning
- **WHEN** shared mode is enabled but channel timestamps diverge at session start beyond the configured threshold
- **THEN** the viewer displays a warning without disabling the mode

### Requirement: The heatmap is derived from bounded history

The viewer SHALL build the bus-load heatmap from the shared history over at most 2 048 visible columns, using measured BusIdle load when available and a bit-time estimate otherwise.

#### Scenario: Measured bus load
- **WHEN** BusIdle frames provide idle durations for a channel
- **THEN** the heatmap shows measured load and marks it as measured

#### Scenario: Estimated bus load
- **WHEN** no BusIdle data exists but channel bitrates are configured
- **THEN** the heatmap shows estimated load from frame bit times

#### Scenario: Missing bitrate
- **WHEN** a channel has no configured bitrate and no BusIdle data
- **THEN** its heatmap cells show `n/a` and Status reports the missing bitrate

#### Scenario: Click-to-record
- **WHEN** the operator clicks a heatmap cell
- **THEN** the focused view pauses and Trace scrolls to the first record in that bucket

#### Scenario: Column cap
- **WHEN** the selected step would exceed 2 048 columns over the visible range
- **THEN** the viewer aggregates to a coarser effective step and displays the effective step

### Requirement: Bit activity is bounded

The viewer SHALL show a 64x8 bit-activity matrix for the selected key over the last 4 096 frames or 10 seconds, whichever is shorter, using observed 0-to-1 and 1-to-0 transitions.

#### Scenario: FD key
- **WHEN** the selected key carries CAN FD payloads
- **THEN** the matrix shows 64 bytes by 8 bits

#### Scenario: Classic key
- **WHEN** the selected key carries classic CAN payloads
- **THEN** the matrix shows 8 bytes by 8 bits

#### Scenario: Sparse window
- **WHEN** fewer than two frames exist in the window
- **THEN** the matrix shows unavailable or low-confidence values instead of fabricating a frequency

### Requirement: Memory is preallocated and bounded

The viewer SHALL preallocate the history, indexes, snapshots, overwrite table, heatmap cache, and export queue from the configured budget, and SHALL run a documented degraded mode when the budget is 16 MiB.

#### Scenario: Capacity from budget
- **WHEN** the budget is 256 MiB and the default eight LIVE views are configured
- **THEN** the history capacity is at least 1 048 576 records and allocations stay within the ledger

#### Scenario: Sixteen MiB mode
- **WHEN** the budget is 16 MiB
- **THEN** the viewer runs degraded, reports the mode, and keeps at least 1 024 history records

#### Scenario: View allocation failure
- **WHEN** a new view's index cannot be allocated
- **THEN** the view is rejected with a visible error and existing views remain functional

### Requirement: Resource guard protects the host

The viewer SHALL enter ECO mode after sustained CPU above the configured limit or an incoming rate above the configured storm threshold, SHALL reduce rendering and derived recomputation, and SHALL return to NORMAL with hysteresis.

#### Scenario: CPU pressure
- **WHEN** process CPU stays above 50 percent for three one-second windows
- **THEN** the viewer enters ECO and shows the CPU reason

#### Scenario: Storm
- **WHEN** the incoming rate exceeds 50 000 frames per second
- **THEN** the viewer enters ECO immediately and continues bounded capture

#### Scenario: Recovery
- **WHEN** CPU stays below 70 percent of the limit for two windows
- **THEN** the viewer returns to NORMAL

### Requirement: The UI routes actions to the focused view

The viewer SHALL route keyboard actions and the Inspector to the last focused `Can:` view and SHALL ignore global view hotkeys when no view is focused or a text field has focus.

#### Scenario: Pause focused view
- **WHEN** `Space` is pressed while a view is focused outside a text field
- **THEN** only that view pauses or resumes

#### Scenario: Left-panel shortcut
- **WHEN** a channel or node checkbox is toggled in the left panel
- **THEN** only the focused view's filter changes and capture is unaffected

#### Scenario: No focus
- **WHEN** no `Can:` view has focus
- **THEN** view-scoped hotkeys are ignored

### Requirement: CLI and configuration are explicit and safe

The viewer SHALL load configuration from the XDG path, allow CLI overrides, reject invalid arguments with exit code 2, and write configuration atomically without overwriting a corrupted file.

#### Scenario: Help
- **WHEN** `--help` is passed
- **THEN** usage is printed and the exit code is 0

#### Scenario: Invalid argument
- **WHEN** an unknown option or out-of-range value is passed
- **THEN** a diagnostic and usage are printed to stderr and the exit code is 2

#### Scenario: Corrupted configuration
- **WHEN** the configuration file is malformed
- **THEN** the viewer starts with defaults, emits one warning, and leaves the file unchanged

#### Scenario: Empty configuration
- **WHEN** no channel is configured or enabled
- **THEN** the GUI opens an empty workspace with a configure-channels dialog and capture stays stopped; headless exits with code 2

### Requirement: Capture failures are isolated

A failure to start one channel SHALL NOT prevent other channels from starting; if no channel starts, capture remains stopped and can be retried.

#### Scenario: One channel fails
- **WHEN** one of several channels cannot create its services
- **THEN** the other channels capture normally and Status names the failed channel and reason

#### Scenario: All channels fail
- **WHEN** no channel can start
- **THEN** capture does not start, the GUI shows the failure, and the operator can use `Restart capture`

#### Scenario: Restart
- **WHEN** the operator selects `Restart capture` after correcting a channel
- **THEN** the capture thread is stopped, recreated with the current configuration, and the fixed channel starts

### Requirement: Shutdown is graceful

The viewer SHALL stop capture and export on SIGINT, SIGTERM, SIGHUP, or window close, SHALL preserve the history already stored, and SHALL exit with code 0 unless a runtime failure occurred.

#### Scenario: SIGTERM
- **WHEN** SIGTERM is delivered while capture is running
- **THEN** capture stops, the model is drained as far as possible, and the process exits 0

#### Scenario: Export during shutdown
- **WHEN** the window closes while an export is active
- **THEN** the export is cancelled, the partial file is removed, and shutdown completes

#### Scenario: Missing publisher is not a failure
- **WHEN** a configured channel has no publisher
- **THEN** the viewer reports waiting, keeps other channels running, and does not exit

### Requirement: Viewer vocabulary is diagnosable and serializable

Every public viewer enum and configuration struct SHALL provide a canonical string representation, a `std::formatter`, and non-throwing JSON serialization and deserialization.

#### Scenario: Record and gap enums
- **WHEN** `CanRecordKind` and `GapReason` values are formatted or serialized
- **THEN** they produce their canonical names and round-trip through JSON without throwing

#### Scenario: View and time enums
- **WHEN** `ViewKind`, `TimeColumnMode`, `TimestampMode`, and `DeltaMode` values are formatted or serialized
- **THEN** they produce their canonical names and round-trip through JSON without throwing

#### Scenario: Channel configuration
- **WHEN** a `CanViewChannel` is serialized and parsed back
- **THEN** its name, id, bitrates, and FD flag survive the round-trip

#### Scenario: Overwrite key
- **WHEN** an `OverwriteKey` is serialized and parsed back
- **THEN** its channel, identifier, extended flag, and kind survive the round-trip

### Requirement: Status exposes capture and model health

The viewer SHALL show per-channel connection/state, ingress occupancy, history size/capacity/evictions, loss breakdown, rejected and duplicate/reorder/reset counts, overwrite evictions, EDS usage, import/export progress, ECO reason, and memory usage.

#### Scenario: Overload is visible
- **WHEN** ingress overflow occurs
- **THEN** the overflow count and a synthetic history record are visible

#### Scenario: Memory is visible
- **WHEN** the viewer is running
- **THEN** Status shows core memory and EDS memory separately

#### Scenario: Rebuild is visible
- **WHEN** a filter rebuild or CANopen reindex is running
- **THEN** Status shows its progress

### Requirement: View tabs have editable titles

The viewer SHALL let the operator rename any open view. Renaming SHALL change the tab label, SHALL NOT change the view identity, filters, selection or dock position, and SHALL persist for LIVE views. The clone dialog SHALL accept a title for the new view, default it from the source view's title, and reject an empty title.

#### Scenario: Rename keeps view state and docking

- **WHEN** the operator renames an open view
- **THEN** its tab label changes while its filters, selection and dock position stay unchanged

#### Scenario: Clone with a custom title

- **WHEN** the operator clones a view and enters `Powertrain`
- **THEN** the new tab is named `Powertrain`

#### Scenario: Empty title is rejected

- **WHEN** the operator submits a whitespace-only title
- **THEN** the rename or clone is rejected and the previous title remains

#### Scenario: Same-named clones stay distinct

- **WHEN** two views have the same visible title
- **THEN** both windows remain independently addressable and keep their own state
