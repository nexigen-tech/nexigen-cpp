# Spec Delta

## Purpose

Defines independent log views, their dock-window lifecycle, clone modes, bounded frozen snapshots, view limits, focus routing, persistence, and per-view export selection.

## ADDED Requirements

### Requirement: Views are independent dock windows

The viewer SHALL render each log view as a separate dockable window whose title identifies the view. ImGui SHALL be able to group those windows into tabs or show them side by side. Closing a view SHALL release that view's state and resources without changing any other view.

#### Scenario: Two source views are tabbed

- **WHEN** the operator opens views for two different sources and docks them into the same node
- **THEN** both windows appear as tabs and switching tabs changes only the visible table, not the shared capture

#### Scenario: Undocking keeps the view independent

- **WHEN** the operator undocks one view
- **THEN** it keeps its history, filters, selection, pause state, and scroll position

#### Scenario: Closing one view preserves the others

- **WHEN** the operator closes one of several log views
- **THEN** the remaining views keep their data and state and the closed view's resources are released

### Requirement: Per-view state is isolated

Each view SHALL own its source filter, level and text filters, visible index, find state, selection, bookmarks, minimap, pause/scroll position, timestamp and delta modes, anomaly threshold, T0 anchor, and export selection. Capture, live history, application registry, colors, time base, ECO guard, UI scale, and dock layout SHALL remain shared.

#### Scenario: Level changes stay in one view

- **WHEN** the operator changes the visible levels in one view
- **THEN** other views keep their own level sets and visible records

#### Scenario: Selection and bookmarks are independent

- **WHEN** the operator selects or bookmarks a record in one view
- **THEN** another view's selection and bookmarks are unchanged

#### Scenario: Time anchor is independent

- **WHEN** the operator sets T0 in one view
- **THEN** another view keeps its own T0 or no anchor

### Requirement: All and source views have distinct scopes

The viewer SHALL provide an `All logs` view by default. It SHALL be closable and reopenable, and SHALL count as one LIVE view. A source view SHALL be fixed to one application. The left Sources panel SHALL be a global catalogue and launcher; the `All` view SHALL expose source visibility through a local `Sources...` popup.

#### Scenario: All is the default view

- **WHEN** the viewer starts without persisted views
- **THEN** it opens an `All logs` view with all sources visible

#### Scenario: Opening a source view

- **WHEN** the operator chooses Open for an application in the Sources catalogue
- **THEN** a new source view is created with that application fixed as its source

#### Scenario: All source filter is local

- **WHEN** the operator changes source visibility from the `All` view's `Sources...` popup
- **THEN** only the `All` view's source filter changes; source views keep their fixed source

#### Scenario: Reopening All

- **WHEN** the operator closes `All` and later opens it from the Sources catalogue
- **THEN** it returns as a new LIVE view with the default all-source filter

### Requirement: Clone dialog selects copied state

Cloning the focused view SHALL open a dialog that chooses `LIVE` or `PAUSED` and offers independent checkboxes for level filters, text filter, timestamp mode, delta mode, anomaly threshold, selection, bookmarks, T0 anchor, and find query. Every checkbox SHALL default to checked. The source SHALL always be inherited; scroll position SHALL not be copied.

#### Scenario: Defaults copy all state

- **WHEN** the clone dialog opens
- **THEN** every copy checkbox is checked

#### Scenario: Unchecked state starts at defaults

- **WHEN** the operator clears a checkbox and creates the clone
- **THEN** that part of the clone starts with the viewer default instead of the original value

#### Scenario: Source is inherited

- **WHEN** the operator clones a source view
- **THEN** the clone is fixed to the same source and does not offer a different source

### Requirement: LIVE clone follows the shared tail

A `LIVE` clone SHALL attach to the shared live history and follow newly ingested records. It SHALL receive independent view state as selected in the clone dialog.

#### Scenario: Live clone receives new records

- **WHEN** new records arrive after a LIVE clone is created
- **THEN** the clone appends those matching its filters and its original continues independently

#### Scenario: Live clone starts at the tail

- **WHEN** a LIVE clone is created
- **THEN** it starts following the newest records rather than reopening the original scroll position

### Requirement: PAUSED clone uses a bounded frozen snapshot

A `PAUSED` clone SHALL copy an immutable snapshot bounded to 1 MiB of record data. A source view SHALL copy that application; `All` SHALL copy all applications. Records SHALL be selected newest-first and materialised in sequence order. Gap records in the copied range SHALL be included and SHALL count toward the budget. All levels and text SHALL be copied regardless of current filters, and absolute record numbers SHALL be preserved.

#### Scenario: Source snapshot contains only its source

- **WHEN** a source view is cloned as PAUSED
- **THEN** the snapshot contains that source's records plus gap records in the same sequence range, but no frames from other sources

#### Scenario: All snapshot contains every source

- **WHEN** the `All` view is cloned as PAUSED
- **THEN** the newest records across all sources, up to the 1 MiB record budget, are copied together with gaps in range

#### Scenario: Snapshot is bounded to newest data

- **WHEN** the scope contains more than 1 MiB of record data
- **THEN** the oldest records are omitted and the clone keeps the newest records that fit

#### Scenario: Hidden levels remain available

- **WHEN** the original view hides Warning and Error and is cloned as PAUSED with all checkboxes checked
- **THEN** the clone's snapshot still contains those records and the copied level filter initially hides them

#### Scenario: Snapshot state is clipped

- **WHEN** selection, bookmarks, or T0 refer to records outside the copied range
- **THEN** selection/bookmarks outside the snapshot are dropped and an out-of-range T0 is cleared; an in-range find query is re-run on the clone

### Requirement: View creation is bounded

The viewer SHALL allow at most 8 LIVE views including `All` and at most 16 PAUSED views. Attempting to exceed a limit SHALL be rejected with a visible message and SHALL NOT create or partially initialise a view. Closing a view SHALL release its slot. Paused clones of paused views SHALL count as PAUSED views.

#### Scenario: Live limit

- **WHEN** eight LIVE views exist and the operator opens another
- **THEN** the viewer reports the limit and keeps the existing views unchanged

#### Scenario: Paused limit

- **WHEN** sixteen PAUSED views exist and the operator clones into PAUSED
- **THEN** the viewer reports the limit and no snapshot is created

#### Scenario: Closing frees a slot

- **WHEN** a LIVE view is closed
- **THEN** a new LIVE view can be opened without exceeding the limit

### Requirement: View definitions persist, session state does not

The viewer SHALL persist `All` and LIVE source view definitions and restore them on restart. PAUSED views SHALL be session-only. Per-view selection, bookmarks, T0, find query, and scroll position SHALL be session-only. If no LIVE views are persisted, the viewer SHALL show an empty workspace from which `All` or a source view can be opened.

#### Scenario: Live tabs restore

- **WHEN** the viewer restarts with two LIVE source views
- **THEN** both return with their sources, levels, text filters, modes, and thresholds

#### Scenario: Paused tabs do not restore

- **WHEN** the viewer restarts after PAUSED views existed
- **THEN** those views are absent

#### Scenario: Empty workspace

- **WHEN** the persisted configuration contains no LIVE views
- **THEN** the viewer opens no Log window and offers `All` and sources from the catalogue

### Requirement: View definitions round-trip

Each persisted view definition SHALL round-trip through the viewer configuration JSON and SHALL expose a stable diagnostic `to_string()` and `std::formatter` representation.

#### Scenario: Configuration round-trip

- **WHEN** a view definition is written and read back
- **THEN** its source, levels, text filter, modes, threshold, and title are preserved

#### Scenario: Diagnostic formatting

- **WHEN** a view definition is formatted with `std::format("{}", value)`
- **THEN** the output equals its `to_string()`

### Requirement: Export selection is per view with one writer

Each view SHALL own its export selection. Its local `Save selected` action SHALL snapshot that selection and submit it to one global export writer. A second export request SHALL be rejected while an export is running. Closing a view SHALL NOT cancel an export already copied from its selection.

#### Scenario: Export from two views

- **WHEN** the operator exports a selection from one view and then requests an export from another while the first is still running
- **THEN** the second request reports that an export is already running

#### Scenario: Closing the source view during export

- **WHEN** an export has copied its records and the originating view is closed
- **THEN** the export completes from its copied selection

### Requirement: Focus routes keyboard and inspector actions

Keyboard copy, pause, bookmark, attention navigation, and find SHALL act on the last focused Log view. The inspector SHALL show the selected record of the focused view. If no Log view has focus, global keyboard actions SHALL be ignored rather than applied to an arbitrary view.

#### Scenario: Shortcut follows focus

- **WHEN** the operator focuses one Log view and presses `Ctrl+C`
- **THEN** the focused view's hovered or selected record is copied

#### Scenario: No focused view

- **WHEN** no Log view has focus and the operator presses `F2`
- **THEN** no view changes its bookmarks
