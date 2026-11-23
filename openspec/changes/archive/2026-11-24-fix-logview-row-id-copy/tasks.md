# Tasks

## 1. Row identity fix

- [x] 1.1 Build a per-row `##row<seq>` label and wrap the row in `ImGui::PushID`/`PopID`.
- [x] 1.2 Keep selection, Shift/Ctrl selection, bookmarks, and virtualized clipping unchanged.

## 2. Copy interactions

- [x] 2.1 Add the hover tooltip with complete message/gap text and the copy hint.
- [x] 2.2 Add the right-click `Copy message` and `Copy full record` actions.
- [x] 2.3 Add `Ctrl+C` copy with hovered-row priority and selected-row fallback.

## 3. Documentation

- [x] 3.1 Document `Ctrl+C` and the right-click copy actions in `docs/articles/log-viewer.md`.

## 4. Verification

- [x] 4.1 Extend the Xvfb/scripted GUI test to assert conflict-free hover across multiple rows and both copy-menu actions.
- [x] 4.2 Run the GUI Xvfb suite and static gates (clang-format, naming, result-value, JSON vocabulary, complexity).

## 5. Release hygiene

- [x] 5.1 Run `graphify update .` after the UI/documentation changes.
