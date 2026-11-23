# Proposal

## Why

On 4K and other high-DPI displays the viewer's fixed 16 px font and ImGui metrics are too small to read. The desktop content scale does not automatically make Dear ImGui text larger, so operators must either lower the display resolution or zoom the whole desktop. The viewer needs a first-class UI scale that keeps the log table readable without changing captured data or the transport contract.

## What Changes

- Add `--ui-scale <factor>` to `nexigen-logview` with an accepted range of `0.5`–`4.0`.
- When the flag is absent, derive the scale from the primary monitor content scale through GLFW (`1.0`–`4.0` clamp).
- Scale the embedded JetBrains Mono font atlas, ImGui style metrics, and the initial window size by the same factor.
- Keep the layout proportional and let the existing docking layout persist through the scaled `.ini` file.
- Add CLI help, the user article, and an Xvfb smoke test for `--ui-scale 2`.
- No **BREAKING** changes. No wire-format change, no service change, no change to the viewer's data model or history budget.

## Capabilities

### New Capabilities

None.

### Modified Capabilities

- `logview/presentation`: add a requirement for high-DPI UI scaling.

## Impact

- **Code:** `tools/nexigen-logview/cli.{hpp,cpp}`, `main.cpp`, `ui/logview_window.{hpp,cpp}`, `tools/nexigen-logview/CMakeLists.txt`, `docs/articles/log-viewer.md`.
- **Performance:** startup-only work. One font atlas is built at `16 * scale` px; no producer, receive, ingest, filter, or render hot path gains per-record work. Font-atlas memory grows with the square of the scale; style and window scaling are constant-size. The per-frame UI path is unchanged.
- **Compatibility:** the flag is optional and defaults to monitor auto-detection; existing invocations keep working. The scale is process-local and does not alter the JSONL export/import format.
- **Tests:** CLI help/invalid-argument coverage and an Xvfb smoke test at `--ui-scale 2`; existing logview Debug/Release/ASan/TSan and GUI suites stay green.
