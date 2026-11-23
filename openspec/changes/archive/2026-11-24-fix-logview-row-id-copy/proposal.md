# Proposal

## Why

Hovering a log row exposes an ImGui programmer error: every visible row uses the same `##row` selector, so ImGui reports conflicting IDs and paints the table red. The same hover interaction has no way to copy the full message or record, even when the table cell truncates long text.

## What Changes

- Give every visible log row a unique interaction identity derived from its absolute record number.
- Add a row tooltip that shows the complete message/gap text and a short copy hint.
- Copy the full record with `Ctrl+C` when a row is hovered (falling back to the selected row).
- Add a right-click row menu with `Copy message` and `Copy full record`.
- Document the new keyboard/mouse copy paths.

No **BREAKING** changes. No wire-format, history, filtering, or transport change.

## Capabilities

### New Capabilities

None.

### Modified Capabilities

- `logview/presentation`: add unique row interaction identity and row-level copy behavior.

## Impact

- **Code:** `tools/nexigen-logview/ui/logview_window.cpp`, `docs/articles/log-viewer.md`, and the GUI regression test.
- **Performance:** UI-only hover/context-menu work on the already virtualized visible rows. No producer, receive, ingest, filter, or export hot path changes. One small stack label is formatted per visible row; clipboard work runs only on an explicit user action.
- **Memory:** no new long-lived allocation. The row label is a fixed stack buffer; tooltip/context-menu text is transient ImGui data.
- **Compatibility:** existing selection, bookmarks, and the inspector Copy button remain unchanged.
