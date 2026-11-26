# Proposal

## Why

`nexigen-canview` derives every view window's title from its scope and hard-codes clone names (`LIVE clone` / `PAUSED clone`). Operators running several views of the same channel cannot tell them apart, and clones of the same kind share both a label and an ImGui window ID, so a second clone can reuse the first clone's window. Tabs need stable, operator-chosen names.

## What Changes

- Add `CanView::set_title()` and `CanViewModel::rename_view()`; surrounding whitespace is trimmed and an empty title is rejected with `InvalidArgument`.
- Add a toolbar `Rename` action and a per-view context menu (`Rename...`, `Clone LIVE`, `Clone PAUSED`) that open a rename dialog prefilled with the current title.
- Add a `Tab title` field to the clone dialog, prefilled from the source view (`<source> (live|paused)`), passed to `clone_view()`; an empty input falls back to the previous default name.
- Give every view window a stable ID `Can: <title>###canview-view-<id>` (the pattern already used by `nexigen-logview`) so renaming keeps the dock position and same-named clones stay distinct windows.
- Rebuild the dock layout once when the layout INI predates the stable-ID scheme (marker `canview-view-`).
- Persist renamed LIVE titles through the existing `canview.json` `views[].title` field.

No breaking changes to the CLI, COM services, record model, headless output, or session format.

## Capabilities

### New Capabilities

None.

### Modified Capabilities

- `hw/can/viewer`: add editable view-tab titles and a titled clone dialog while preserving independent view state and docking.

## Impact

- **Code**: `tools/nexigen-canview/model/can_view.{hpp,cpp}`, `can_view_model.{hpp,cpp}`, `ui/canview_window.cpp`, and `tests/views_test.cpp`; spec `hw/can/viewer`.
- **APIs**: internal `nexigen::canview` model API only (`rename_view`, `set_title`); no public `include/nexigen/` contract changes.
- **Performance / latency**: renames and clone naming run only on explicit operator actions on the UI thread; the capture thread and per-frame ingest path are untouched. Window labels are formatted once per view per rendered frame (5 FPS default) and allocate only on the cold UI path. No hot-path `std::string`, `std::format`-to-string, `new`, or container growth is added.
- **Memory**: two fixed 96-byte dialog buffers in `UiState`, plus the existing one-`std::string` title per view. No per-record or per-frame allocation is added; the capture/ingest memory ledger is unchanged.
- **Systems**: GUI-only behaviour; headless, CLI, capture, transport, and persistence formats remain compatible. An existing dock layout is rebuilt once after the upgrade.
- **Testing**: model unit tests for rename validation and persistence; the canview CTest suite; Debug, Release, ASan, and TSan builds.
