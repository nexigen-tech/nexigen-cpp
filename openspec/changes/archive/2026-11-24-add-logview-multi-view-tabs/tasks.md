# Tasks

## 1. View model and shared history

- [x] 1.1 Introduce a `HistorySource` abstraction and move the visible index out of `RecordRing` into a per-view `ViewIndex`; keep chunked rebuilds and lazy find storage.
- [x] 1.2 Split the current single model state into shared `LogSession` plus per-view `LogView` state (source, filters, selection/bookmarks, minimap, pause/scroll, time/T0, export selection).
- [x] 1.3 Append each new matching live record to every LIVE view index, clear evicted slot state in every live view, and leave PAUSED snapshots untouched.
- [x] 1.4 Implement `All`/source view creation, closing, reopening, 8-LIVE/16-PAUSED limits, and visible limit/allocation errors.

## 2. Clone and frozen snapshot

- [x] 2.1 Implement `FrozenHistory` with compact frame/kind/delta storage, absolute sequence preservation, and a 1 MiB record-data cap.
- [x] 2.2 Copy newest-first source records plus in-range gap records for source views, and all sources for `All`; share immutable snapshots between paused clones.
- [x] 2.3 Implement clone-state copying and clipping for selection, bookmarks, T0, and find; keep scroll uncopied and source inherited.
- [x] 2.4 Add the clone dialog state model for LIVE/PAUSED mode and the nine copy checkboxes, all checked by default.

## 3. Dock-window UI

- [x] 3.1 Render every view as a unique dockable `Log: <name>###logview-<id>` window, dock new views beside the focused view, and keep ImGui layout restoration.
- [x] 3.2 Move levels, text filter, pause, find, clone, save-selected, and per-view counters into each Log window; convert the left pane to a Sources catalogue with Open actions.
- [x] 3.3 Add the local `Sources...` popup for the `All` view and hide source selection for fixed source views.
- [x] 3.4 Wire the clone dialog, view close/reopen, limit messages, and empty-workspace state.
- [x] 3.5 Route keyboard shortcuts and the inspector to the last focused Log view; ignore global keyboard actions when no Log view is focused.

## 4. Persistence and export

- [x] 4.1 Add the persisted `views` configuration array and a `ViewConfig` value type with `to_string()`, `std::formatter`, and non-throwing `to_json`/`from_json`.
- [x] 4.2 Restore `All` and LIVE views on startup, keep PAUSED and per-view interaction state session-only, and handle missing/empty view lists.
- [x] 4.3 Keep per-view export selection but a single global `ExportWriter`; reject concurrent exports and preserve an already-copied export when its view closes.

## 5. Verification

- [x] 5.1 Add model tests for per-view filters/selection/bookmarks/T0, live ingestion, snapshot bounds, gap inclusion, clone clipping, view limits, and config round-trips.
- [x] 5.2 Add Xvfb tests for docked/undocked Log windows, local level changes, All source popup, clone LIVE/PAUSED, limits, focus routing, and persistence across two runs.
- [x] 5.3 Add memory-budget tests for 8 LIVE indexes, 16 one-MiB snapshots, lazy find storage, and rejected allocations.

## 6. Documentation and release gates

- [x] 6.1 Update Doxygen comments for every touched logview public header and the user guide with dock-window, clone, snapshot, limit, and focus behavior.
- [x] 6.2 Update `docs/architecture.md` with the shared-session/per-view data path and the paused-snapshot layout Mermaid diagrams.
- [x] 6.3 Run Debug, Release, ASan, and TSan model/transport/tool suites, the GUI Xvfb suite, static gates, and `graphify update .`.

## Workflow follow-up

- Verify and archive this change after implementation and review.
