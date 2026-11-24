# Tasks

## 1. Navigation and auto-scroll

- [x] 1.1 Make auto-scroll default-on for every new view, remove pause/resume mutations of the toggle, and implement First/Last/auto-scroll actions using `display_at`, `scroll_to`, and `auto_scroll`; verify with headless tests for filtered, paused, PAUSED-snapshot, empty, default-on views, and manual-scroll disable.
- [x] 1.2 Draw three Wireshark-like vector icon controls with tooltips for First, Last, and auto-scroll, including an active/inactive visual state for the toggle; verify the three hit areas and toggle rendering in an Xvfb test.
- [x] 1.3 Route `Ctrl+Home` and `Ctrl+End` to First/Last for the focused Log view without changing selection, filters, bookmarks, T0, or pause state; verify focus routing and no-op behavior with no focused view.
- [x] 1.4 Update the user guide for First, Last, default-on auto-scroll, the toggle semantics, and the new shortcuts; verify the stale "Return to live" reference is replaced.

## 2. Startup splash

- [x] 2.1 Add `assets/nexigen_splash.png` from `images/nexigen_1360x768.png`, embed it at configure time, and add the libpng dependency; verify a clean GUI configure/build and the generated asset size/dimensions.
- [x] 2.2 Implement the splash image loader (PNG decode, RGB upload, failure fallback) and delete the decode buffer/texture after use; verify with a dedicated `--ui-splash-test` that reports 1360x768 and a valid texture.
- [x] 2.3 Run capture and `process_model_frame()` before the splash loop, render a non-interactive fitted one-second splash, and skip it in headless/smoke/scripted modes; verify records arriving during the splash are present when the workspace opens.
- [x] 2.4 Add an Xvfb `nexigen_logview_ui_splash` test for the one-second duration, aspect-fit rendering, and clean fallback when the image cannot be uploaded; keep existing GUI test timings unchanged.
- [x] 2.5 Update the user guide and build prerequisites for the startup splash and libpng; verify the documented build instructions mention the new dependency.

## 3. Documentation and release gates

- [x] 3.1 Update Doxygen comments in the touched logview UI headers and helpers; verify the generated documentation has no missing `@brief`/`@param`/`@return` warnings.
- [x] 3.2 Update `docs/architecture.md` with the startup splash path and the controllable auto-scroll/first/last navigation behavior; verify the Mermaid diagrams still render.
- [x] 3.3 Run the Debug, Release (`-O3 -march=native -flto` with latency gates), ASan, and TSan model/transport/tool suites, the full GUI Xvfb suite, static gates, and `graphify update .`; verify 100% pass and a current graph.

## Workflow follow-up

- Verify and archive this change after implementation and review.
