# Spec Delta

## ADDED Requirements

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
