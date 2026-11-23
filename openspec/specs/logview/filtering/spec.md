# logview/filtering Specification

## Purpose

Defines how the viewer narrows the visible record set without losing data, how find differs from filter, and how selection and bookmarks stay attached to records rather than screen positions.

## Requirements

### Requirement: Filters compose without deleting data

The viewer SHALL filter by any combination of application visibility, log-level visibility, and a text substring. Filtering SHALL hide records from the table only; clearing a criterion SHALL make the previously hidden records visible again.

#### Scenario: Hiding and unhiding an application

- **WHEN** the user unchecks an application in the sources panel
- **THEN** its rows disappear from the table and reappear when it is checked again

#### Scenario: Level filter

- **WHEN** only Error and Fatal are checked
- **THEN** only rows at those levels are visible

#### Scenario: Text filter

- **WHEN** the user types a substring in the filter field
- **THEN** only records whose message contains the substring remain visible

### Requirement: The visible index updates incrementally

The viewer SHALL maintain the ordered set of visible record numbers. New matching records SHALL be appended without rescanning the history, and a changed criterion SHALL be applied in bounded chunks of at most 20 000 records per frame with progress reported.

#### Scenario: New record appears while filtered

- **WHEN** a new record matching the active filter is ingested
- **THEN** it appears at the end of the visible set without a full rebuild

#### Scenario: Filter change is incremental

- **WHEN** the user changes a filter over a full history
- **THEN** the table updates in chunks, shows progress, and stays interactive during the rebuild

#### Scenario: Eviction during a rebuild

- **WHEN** a record is evicted before the rebuild reaches it
- **THEN** it is skipped instead of displaying stale content

### Requirement: Find does not filter

Find (Ctrl+F) SHALL keep every visible row in place, highlight the matching text, and provide next/previous navigation with Enter and Shift+Enter. The find text SHALL be independent of the filter text.

#### Scenario: Matches are highlighted in place

- **WHEN** the user searches for a substring
- **THEN** all visible rows remain and the matches are highlighted

#### Scenario: Navigation wraps

- **WHEN** the user presses Enter past the last match
- **THEN** navigation wraps to the first match

#### Scenario: No matches

- **WHEN** the search text matches nothing
- **THEN** the viewer reports zero matches and keeps the table unchanged

### Requirement: Selection follows record identity

The viewer SHALL support single click, Shift+click over the visible order, Ctrl+click toggle, and Ctrl+A for every filtered record. Selection SHALL refer to record numbers and SHALL survive filter changes and scrolling.

#### Scenario: Range selection

- **WHEN** the user clicks a record and Shift+clicks another
- **THEN** every record between them in the visible order is selected

#### Scenario: Select all filtered

- **WHEN** the user presses Ctrl+A
- **THEN** every record currently matching the filter is selected

#### Scenario: Selection survives a filter change

- **WHEN** the filter hides a selected record
- **THEN** the record stays selected and is still included when the selection is saved

### Requirement: Bookmarks are record-level

F2 SHALL toggle a bookmark on the selected record. The viewer SHALL navigate to the next and previous bookmark and SHALL show bookmarks on the minimap. Bookmarks SHALL survive filtering and scrolling.

#### Scenario: Toggle a bookmark

- **WHEN** the user presses F2 on a record
- **THEN** the bookmark state toggles and the row shows it

#### Scenario: Navigate bookmarks

- **WHEN** the user requests the next bookmark
- **THEN** the table selects and reveals the next bookmarked record

#### Scenario: Bookmarks survive filtering

- **WHEN** a bookmark is on a record hidden by the current filter
- **THEN** the bookmark stays attached and appears again when the filter is cleared
