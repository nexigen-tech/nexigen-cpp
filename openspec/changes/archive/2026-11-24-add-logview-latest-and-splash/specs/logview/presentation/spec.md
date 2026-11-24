# Spec Delta

## ADDED Requirements

### Requirement: First and last message navigation

Each Log view SHALL provide separate actions for the first and last displayed
record. First SHALL move to the oldest visible record and turn auto-scroll off.
Last SHALL move to the newest visible record without changing the auto-scroll
toggle. Neither action SHALL change selection, filters, bookmarks, the time
anchor, or pause state. An empty view SHALL remain unchanged.

#### Scenario: First message

- **WHEN** the operator activates First on a non-empty view
- **THEN** the oldest visible record is shown and auto-scroll is off

#### Scenario: Last message with auto-scroll off

- **WHEN** auto-scroll is off and the operator activates Last
- **THEN** the newest visible record is shown and auto-scroll stays off

#### Scenario: Last message with auto-scroll on

- **WHEN** auto-scroll is on and the operator activates Last
- **THEN** the newest visible record is shown and subsequent records remain visible

#### Scenario: Navigation respects filters and empty views

- **WHEN** the active filter hides records, or hides every record
- **THEN** First and Last target only visible records, and an empty view does nothing

#### Scenario: Paused view navigation

- **WHEN** a paused LIVE view or PAUSED snapshot uses First or Last
- **THEN** navigation stays inside its frozen displayed range and the pause state is unchanged

### Requirement: Auto-scroll follows latest by default

Every new LIVE Log view SHALL start with auto-scroll enabled. While it is enabled
and the view is not paused, newly displayed records SHALL keep the newest row
visible. The operator SHALL be able to toggle auto-scroll off and navigate
manually. Pausing SHALL suspend following without changing the toggle; resuming
SHALL preserve the operator's choice.

#### Scenario: New view follows the tail

- **WHEN** a LIVE view is created
- **THEN** auto-scroll is enabled and incoming records keep the newest row visible

#### Scenario: Operator disables auto-scroll

- **WHEN** the operator toggles auto-scroll off
- **THEN** the current scroll position remains and subsequent records do not move the table

#### Scenario: Pause preserves the toggle

- **WHEN** the operator pauses a view whose auto-scroll is off and later resumes it
- **THEN** auto-scroll remains off and the table does not jump to the tail

#### Scenario: Toggle back on

- **WHEN** a live view is not paused and the operator enables auto-scroll
- **THEN** the table moves to the newest visible row and continues following

#### Scenario: Manual scrolling disables auto-scroll

- **WHEN** the operator wheels or drags the table away from the newest row while auto-scroll is on
- **THEN** auto-scroll turns off and the table keeps the manually chosen position

### Requirement: Wireshark-like navigation controls

Each Log view SHALL show three distinct toolbar controls for First, Last, and
auto-scroll. Their icon shapes SHALL resemble the corresponding Wireshark
navigation controls. Each control SHALL have a tooltip that names its action and
current state.

#### Scenario: Navigation controls are visible

- **WHEN** a Log view is shown
- **THEN** it displays separate first, last, and auto-scroll icon controls with tooltips

#### Scenario: Auto-scroll state is visible

- **WHEN** auto-scroll is enabled or disabled
- **THEN** its toolbar control shows the corresponding active or inactive state

### Requirement: Startup splash

On normal interactive launch, the viewer SHALL display a non-interactive splash
of its bundled artwork for one second before the workspace. Capture and ingestion
SHALL continue while it is visible. The image SHALL scale uniformly and letterbox
inside the window. If it cannot be displayed, the viewer SHALL report the failure
and continue to the workspace.

#### Scenario: Splash precedes the workspace

- **WHEN** the viewer starts in normal interactive mode
- **THEN** the splash artwork is visible for one second and the workspace appears afterwards

#### Scenario: Capture is not delayed by the splash

- **WHEN** records arrive during the splash interval
- **THEN** those records are present in the workspace when it first appears

#### Scenario: Splash is non-interactive

- **WHEN** the operator clicks or types during the splash
- **THEN** no workspace action is executed and the splash still ends after its fixed duration

#### Scenario: Test modes skip the splash

- **WHEN** the viewer runs headless or in a scripted/smoke UI mode
- **THEN** it proceeds without waiting for the splash

#### Scenario: Splash image failure

- **WHEN** the bundled image cannot be decoded or uploaded
- **THEN** the viewer reports the failure and opens the workspace without waiting for the splash

#### Scenario: High-DPI aspect ratio

- **WHEN** the viewer runs with a high-DPI scale
- **THEN** the splash image is scaled uniformly and remains fully visible
