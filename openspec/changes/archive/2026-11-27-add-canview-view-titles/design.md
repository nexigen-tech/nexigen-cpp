# Design

## Context

Each `CanView` is an ImGui dock window created as `"Can: " + view.title()`. The window name is also the ImGui ID, so renaming would create a new window and lose its dock node. Clone creation passes constant titles (`LIVE clone` / `PAUSED clone`), so a second clone of the same kind resolves to the same window ID and overwrites the first window. The model already owns a per-view `title` and persists it in `canview.json`; `clone_view()` already accepts an arbitrary title.

## Goals / Non-Goals

**Goals:**

- Let the operator rename an existing view or name a clone from the UI.
- Keep the view's ImGui identity, dock position, filters, selection, and persisted state across a rename.
- Keep headless, capture, transport, and session formats unchanged.

**Non-Goals:**

- Renaming channels, CANopen nodes, applications, or exported files.
- Automatic re-docking or tab reordering.
- Changing the `canview.json` schema beyond the already persisted `views[].title`.

## Decisions

### Stable window ID: `Can: <title>###canview-view-<id>`

ImGui hashes the part after `###` for the window ID while rendering only the part before it. Appending the immutable view id makes the ID independent of the displayed title, so `ImGui::Begin` updates `window->Name` in place and keeps the window, dock node, and tab order. This mirrors the existing `Log: <title>###logview-<id>` scheme in `nexigen-logview`. A per-frame `std::string` is built only on the UI thread.

### One-time layout migration

The old INI keys windows by their scope-derived title and hashes the full name. After the stable-ID change those settings cannot apply. `layout_has_docking()` now also requires the marker `canview-view-` in the INI; when it is absent the existing `build_dock_layout()` runs once and re-docks the live views under their stable IDs. Subsequent starts restore the operator's layout unchanged.

### Model-owned title validation

`CanViewModel::rename_view(id, title)` looks up the view, trims leading/trailing spaces, tabs, CR/LF, rejects an empty result with `InvalidArgument`, rejects an unknown id with `NotFound`, and calls `CanView::set_title()`. Keeping validation in the model makes it testable without ImGui and gives the clone dialog the same rules. Visible titles may collide; identity is the view id, not the label.

### Dialog flow

- Toolbar `Rename` and the per-view context menu prefill a 96-byte `rename_title` buffer and set `rename_modal`.
- The rename modal applies on `Apply` or Enter and keeps the dialog open on validation failure.
- `Clone LIVE` / `Clone PAUSED` prefill `clone_title` with `<source> (live|paused)` and pass the edited string to `clone_view()`; an empty input falls back to the old constant name.

## Risks / Trade-offs

- **Layout rebuild**: users lose a customised dock layout once because the window ID changes. This is bounded to the first start after the upgrade and is preferable to silently floating view windows.
- **Duplicate labels**: two tabs may display the same text by operator choice. They remain independent because IDs use the view id; the trade-off is visual ambiguity chosen by the user.
- **Hot path**: none. Renaming and label formatting stay on the GUI thread; capture callbacks, the ingress queues, and the history ring are unchanged.
- **Memory**: two fixed 96-byte dialog buffers and the existing per-view title string; no allocation in the ingest path.
