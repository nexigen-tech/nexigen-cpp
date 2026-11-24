# logview/presentation Specification

## Purpose

Defines the viewer's visual contract: colors that separate severity from source, a dockable three-zone layout, a virtualized table that stays fast at full history, an inspector for one record, and a minimap for the whole buffer.

## Requirements

### Requirement: Level and application colors

The viewer SHALL let the user set a text color and an optional background color per log level, and SHALL assign each newly discovered application a distinct color from a palette. Both SHALL be editable at runtime and resettable to defaults.

#### Scenario: Default level colors

- **WHEN** the viewer starts with default settings
- **THEN** Fatal uses its distinct background, Error, Warning, Info, and Debug/Verbose use their default text colors

#### Scenario: New application gets a color

- **WHEN** an application identifier appears for the first time
- **THEN** it receives the next unused palette color and shows a color square in the sources panel

#### Scenario: Reset colors

- **WHEN** the user presses Reset Colors
- **THEN** every level and application color returns to its default

### Requirement: Two color modes

The viewer SHALL offer a combined mode, where the application column uses the application color and Level/Message use the level color, and a criticality mode, where Error and Fatal color the whole row regardless of application color.

#### Scenario: Combined mode

- **WHEN** combined mode is active
- **THEN** the application column reflects the source and the message reflects the level

#### Scenario: Criticality mode

- **WHEN** criticality mode is active and a row is Error or Fatal
- **THEN** the whole row uses the level color even if the application has its own color

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

### Requirement: The table is virtualized

The table SHALL render only visible rows and SHALL stay responsive with the full configured history. It SHALL show bookmark, index, timestamp, delta, application, context, level, and message columns.

#### Scenario: Full history scroll

- **WHEN** the visible set contains 500 000 or more records
- **THEN** scrolling stays responsive and only the visible rows are drawn

#### Scenario: Column set

- **WHEN** the table is shown
- **THEN** every required column is present and the context column is shown as unavailable when the platform provides no context

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

### Requirement: Minimap marks severity and bookmarks

The viewer SHALL draw a minimap next to the table scrollbar that marks Error and Fatal density and bookmarks across the entire history.

#### Scenario: Errors are visible

- **WHEN** the history contains Error or Fatal records
- **THEN** their positions are marked on the minimap

#### Scenario: Bookmarks are visible

- **WHEN** records are bookmarked
- **THEN** their positions are marked on the minimap

#### Scenario: Minimap follows the buffer

- **WHEN** records are ingested or evicted
- **THEN** the minimap updates with the retained history

### Requirement: UI scales for high-DPI displays

The viewer SHALL support a process-local UI scale for its font, ImGui style metrics, and initial window size. The scale SHALL be provided explicitly with `--ui-scale <factor>` in the range `0.5`–`4.0`; when absent, the viewer SHALL derive it from the primary monitor content scale and clamp it to `1.0`–`4.0`. The scale SHALL NOT change captured data, the history budget, the transport service, or the export/import format.

#### Scenario: Automatic scale on a high-DPI display

- **WHEN** the viewer starts without `--ui-scale` on a display whose reported content scale is `2.0`
- **THEN** the font, control metrics, and initial window use a `2.0` scale

#### Scenario: Explicit 4K scale

- **WHEN** the operator starts the viewer with `--ui-scale 2`
- **THEN** the embedded font is loaded at twice its base size, ImGui control sizes are doubled, and the initial window is created at twice its base dimensions

#### Scenario: Invalid scale is rejected

- **WHEN** the operator supplies a scale below `0.5`, above `4.0`, or a non-numeric value
- **THEN** the viewer exits with the invalid-argument path and prints usage rather than starting with a guessed scale

#### Scenario: Scale is visible in diagnostics

- **WHEN** the parsed command-line options are rendered with `to_string()`
- **THEN** the numeric UI scale is present in the diagnostic text

#### Scenario: Headless mode is unaffected

- **WHEN** the viewer runs with `--headless` and an explicit `--ui-scale`
- **THEN** the headless model/dump behaviour is unchanged; the scale only applies to windowed rendering

### Requirement: Table rows have unique interaction identity

Every visible log row SHALL have an interaction identity derived from its absolute record number. Hovering, clicking, or opening a row context menu SHALL NOT produce an ImGui conflicting-ID programmer error, and the error SHALL NOT recolour the table.

#### Scenario: Hovering visible rows is conflict-free

- **WHEN** the table displays several rows from different records
- **THEN** each row has a distinct interaction identity and hovering any row produces no conflicting-ID error

#### Scenario: Selection survives unique row identities

- **WHEN** the operator clicks one row and then another
- **THEN** selection changes exactly as before, with Shift/Ctrl behaviour preserved

### Requirement: Row text is copyable

The viewer SHALL expose the complete record text when a row is hovered and SHALL allow copying it. `Ctrl+C` SHALL copy the hovered row, falling back to the selected row when nothing is hovered. A right-click row menu SHALL offer `Copy message` and `Copy full record`.

#### Scenario: Ctrl+C copies the hovered row

- **WHEN** the operator hovers a row and presses `Ctrl+C`
- **THEN** the full record text and metadata are placed on the clipboard

#### Scenario: Copy message from the context menu

- **WHEN** the operator right-clicks a row and chooses `Copy message`
- **THEN** only the message/gap text is placed on the clipboard

#### Scenario: Copy full record from the context menu

- **WHEN** the operator right-clicks a row and chooses `Copy full record`
- **THEN** the record metadata and complete message are placed on the clipboard

#### Scenario: Fallback to the selected row

- **WHEN** no row is hovered and a row is selected
- **THEN** `Ctrl+C` copies the selected row
