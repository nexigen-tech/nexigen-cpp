# Tasks

## 1. Model and persistence

- [x] 1.1 Add `CanView::set_title()` and `CanViewModel::rename_view()` with whitespace trimming, `InvalidArgument` for an empty title, and `NotFound` for an unknown view id.
- [x] 1.2 Reuse the persisted `views[].title` field so renamed LIVE views survive restart; PAUSED clones remain session-only.
- [x] 1.3 Add model tests for rename, trimming, empty-title rejection, unknown id, JSON persistence, and restore.

## 2. View-tab UI

- [x] 2.1 Render every view as `Can: <title>###canview-view-<id>` and use the same label in `build_dock_layout()` so renaming preserves the dock node and same-named clones stay distinct windows.
- [x] 2.2 Rebuild the dock layout when the INI predates the stable-ID scheme by requiring the `canview-view-` marker in `layout_has_docking()`.
- [x] 2.3 Add the toolbar `Rename` action and the per-view context menu (`Rename...`, `Clone LIVE`, `Clone PAUSED`) with a rename modal prefilled from the focused view.
- [x] 2.4 Add the `Tab title` field to the clone dialog, prefill it from the source title, pass it to `clone_view()`, and fall back to the old constant name when empty.

## 3. Documentation and specification

- [x] 3.1 Record the behaviour as the `hw/can/viewer` delta in this change; main specs are updated by `openspec archive`.
- [x] 3.2 Add `/// @brief`, `@param`, and `@return` Doxygen comments to the new `set_title()` and `rename_view()` declarations in `tools/nexigen-canview/model/` (no touched headers under `include/`).
- [x] 3.3 Mermaid diagrams in `docs/architecture.md` are not applicable: the change affects only UI window naming and does not alter the capture, history, or persistence architecture.

## 4. Verification

- [x] 4.1 Debug build and the full `nexigen_canview_tests` CTest suite pass.
- [x] 4.2 Release build (`build-logview-release`) and the canview CTest suite pass.
- [x] 4.3 AddressSanitizer build (`build-asan-com`) and the canview CTest suite pass.
- [x] 4.4 ThreadSanitizer build (`build-tsan-com`) and the canview CTest suite pass.
- [x] 4.5 Run `graphify update .` after the final code edit.

## 5. Workflow follow-up

- [x] 5.1 Run `openspec validate add-canview-view-titles` (valid).
- [ ] 5.2 Archive this change after review.
