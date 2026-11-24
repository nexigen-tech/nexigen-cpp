# Proposal

## Why

`nexigen-logview` currently has one global table, one filter index, one selection, one pause state, and one time anchor. Operators investigating a suite need to watch several applications side by side, compare live and frozen windows, and apply different severity filters per source without duplicating the process or losing the shared capture history. A single global view forces constant filter switching and makes one investigation overwrite another's selection and pause position.

## What Changes

- Introduce independent log **views** rendered as separate dockable windows (`Log: All logs`, `Log: GENS`, `Log: RECV`), which ImGui can group into tabs or show side by side.
- Give each view its own source filter, level/text filter, visible index, find state, selection, bookmarks, minimap, pause/scroll position, timestamp/delta mode, anomaly threshold, T0 anchor, and export selection.
- Keep capture, the live history ring, application registry, colors, time base, ECO guard, UI scale, and dock layout shared.
- Add `All logs` as the default, closable view. Its local `Sources...` popup controls source visibility; the left panel becomes a global source catalogue and view launcher.
- Add source views created from the left catalogue; each source view is fixed to one application.
- Add a clone dialog that can create a `LIVE` or `PAUSED` copy of the active view, with independent checkboxes for levels, text filter, timestamp mode, delta mode, anomaly threshold, selection, bookmarks, T0, and find query. All boxes default to checked.
- A `LIVE` clone follows the shared live tail. A `PAUSED` clone owns a frozen snapshot: newest records of its source (or all sources for `All`) plus gap records in the same sequence range, bounded to 1 MiB of record data.
- Limit the viewer to 8 LIVE views including `All`, and 16 PAUSED views. Closing a view releases its slot and resources.
- Persist `All` and LIVE source views and restore them on restart; PAUSED views and per-view selection/bookmarks/T0/find/scroll are session-only.
- Keep a single global export writer; each view's local `Save selected` snapshots that view's selection and is rejected while another export is running.
- No **BREAKING** changes: the default `All logs` view preserves the existing single-view workflow.

## Capabilities

### New Capabilities

- `logview/views`: independent log-view lifecycle, focus/active-view semantics, clone modes, bounded paused snapshots, view limits, persistence, and per-view export selection.

### Modified Capabilities

- `logview/presentation`: change the layout contract from one log table to one or more dockable Log windows with local controls, and bind the inspector and keyboard actions to the focused view.
- `logview/resources`: add explicit view-count, view-index, find-match, and paused-snapshot budgets.

## Impact

- **Code:** `tools/nexigen-logview/model/` (view state, history abstraction, per-view indexes/snapshots), `ui/logview_window.cpp` (dock windows, local toolbars, clone dialog, source catalogue), configuration/persistence, tests and docs.
- **Performance:** no capture-path change. Ingest appends matching records to each LIVE view index, so cost is O(live views) per record (bounded by 8). Filter rebuilds are chunked per view and share the existing 20 000-record/frame budget. Find matches are allocated lazily; a full-history match array is never reserved per view.
- **Memory:** each LIVE view adds `capacity * 8 B` for `visible[]` (4 MiB at 524 288 records, 8 MiB at 1 048 576), plus ~128 KiB of bitsets and ~12 KiB minimap. Eight LIVE views add ~33 MiB or ~65 MiB. Each PAUSED view adds at most 1 MiB of snapshot records plus a small index; 16 PAUSED views add at most ~16 MiB. No allocation is added to the receive or ingest hot path.
- **Compatibility:** the default `All` view keeps the current workflow; PAUSED behavior is session-only; no wire-format, service, logging, or export-format change.
- **Tests:** model tests for view state/snapshot limits, GUI Xvfb tests for docking, local controls, clone dialog, limits, focus routing, and persistence across two runs; full Debug/Release/ASan/TSan and GUI matrices.
