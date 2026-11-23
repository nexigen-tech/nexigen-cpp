# Design

## Context

See `proposal.md` — Why. The virtualized table draws only visible rows, but all rows currently use the same `Selectable("##row")` label inside one table scope. ImGui therefore sees several visible items with the same ID and emits a programmer-error tooltip; the table also receives the error colour. The row already carries an absolute record number (`RecordView::seq`), which is a stable per-session identity.

## Goals / Non-Goals

**Goals:**

- Remove the conflicting ImGui ID without changing row order, clipping, or selection semantics.
- Make the complete row text available on hover and copyable by keyboard or context menu.
- Keep copy work off the ingest/render hot path until the operator asks for it.

**Non-Goals:**

- Rich text selection inside the table cell.
- Copying arbitrary multi-row ranges as one document.
- Changing the inspector Copy button or export formats.

## Decisions

### D1. Scope each row with its absolute record number

`draw_record_row` builds a fixed stack label `##row<seq>` and wraps the entire row in `ImGui::PushID(label)` / `PopID()`. The `Selectable`, tooltip, and context-menu IDs therefore become unique per resident record. The record number is already monotonic and unique for the session, so no new counter or allocation is needed.

Alternatives considered: `PushID(int(seq))` (rejected: truncates 64-bit identity), pointer-based IDs (rejected: the local view address is reused by the stack), and manually suffixing every label (rejected: easy to miss future row items).

### D2. Hover tooltip and row context menu

Hovering a row shows the complete message (or gap text) plus `Ctrl+C copies this record; right-click for more`. The tooltip uses the existing row item and no extra window state. A right-click selects the row and opens a unique context menu with:

- `Copy message` — payload/gap text only.
- `Copy full record` — the same metadata + payload helper used by the inspector.

### D3. Ctrl+C priority

`Ctrl+C` copies the hovered row when `UiState::hovered_seq` is set; otherwise it copies the selected row. The hovered number is reset at the start of each table draw, so a stale hover cannot capture the shortcut after the mouse leaves the table.

### D4. Performance and allocation

The only per-visible-row addition is one `snprintf` into a 32-byte stack buffer and ImGui ID bookkeeping. No heap allocation is added to ingest, filtering, or the per-frame render path. Tooltip/context-menu strings and clipboard copies happen only on hover or explicit user action.

### D5. Verification

Extend the Xvfb/scripted GUI checks to cover hover over multiple rows and the copy actions, and keep the existing table/selection tests. Because this is GUI-only, the headless model/transport suites remain unchanged.

## Risks / Trade-offs

- **Very long tooltip text** → ImGui wraps/clips the tooltip; the complete text remains copyable through `Copy full record`.
- **Clipboard support on a minimal X server** → the existing inspector Copy button already relies on the same ImGui clipboard path; Xvfb tests cover the action, not OS clipboard persistence.
- **Hover + right-click ordering** → right-click selects the row before opening the menu so both copy actions refer to the intended record.
