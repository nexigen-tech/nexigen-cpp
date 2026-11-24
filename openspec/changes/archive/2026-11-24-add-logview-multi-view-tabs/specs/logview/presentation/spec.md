# Spec Delta

## MODIFIED Requirements

### Requirement: Dockable three-zone layout

The viewer SHALL present a global toolbar, a left Sources catalogue, one or more dockable Log windows (one per view), and a collapsible bottom message inspector. Each Log window SHALL carry its own local controls for levels, text filter, pause, find, clone, and save selected. The dock layout and the order of Log windows SHALL be restored on restart.

#### Scenario: Panes dock and undock

- **WHEN** the user drags a pane or a Log window to another dock area
- **THEN** the layout changes and every Log view keeps its data and selection

#### Scenario: Several Log windows share a dock node

- **WHEN** the user docks two Log windows into the same node
- **THEN** they appear as tabs and switching between them preserves each view's filters and scroll position

#### Scenario: Several Log windows are visible together

- **WHEN** the user undocks one Log window and keeps another docked
- **THEN** both remain usable and each keeps independent state

#### Scenario: Layout is restored

- **WHEN** the viewer restarts with persisted LIVE view definitions
- **THEN** the previous dock layout and Log-window order are restored

### Requirement: Toolbar reports state

The global toolbar SHALL show Open, Clear history, Colors, the resource mode (NORMAL or ECO with the reason), and global capture counters. Each Log window SHALL show its local pause/resume state, the count of new records while paused, its visible/total counts, its current rate, and its local Save selected action.

#### Scenario: Pause badge

- **WHEN** the user pauses one Log view
- **THEN** that window's badge reads PAUSED with its own count of new records

#### Scenario: ECO badge

- **WHEN** the viewer enters ECO mode
- **THEN** the global toolbar badge reads ECO and its tooltip names the trigger

#### Scenario: Counters

- **WHEN** records are ingested and filtered
- **THEN** each Log window reports its own visible count and rate while the global toolbar reports capture totals

### Requirement: Inspector shows one record

The inspector SHALL show the selected record of the focused Log view, its complete payload, its metadata (index, timestamp, delta, application, context, level, thread, pid, drop counter, wire version), and a copy action. It SHALL indicate when the payload may have been truncated at the producer.

#### Scenario: Select a record

- **WHEN** the user selects a row in a Log view and that view has focus
- **THEN** the inspector shows that record's complete payload and metadata

#### Scenario: Focus changes the inspector

- **WHEN** the user focuses a different Log view with a selected row
- **THEN** the inspector shows the newly focused view's selected record

#### Scenario: Copy

- **WHEN** the user presses Copy in the inspector
- **THEN** the focused view's selected payload and metadata are placed on the clipboard in a readable form

#### Scenario: Possible truncation

- **WHEN** the payload length equals the frame capacity
- **THEN** the inspector warns that the producer may have truncated it
