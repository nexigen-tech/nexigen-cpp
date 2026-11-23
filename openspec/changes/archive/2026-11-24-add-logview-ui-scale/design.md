# Design

## Context

See `proposal.md` — Why. `nexigen-logview` already owns a GLFW/OpenGL3 window, an embedded JetBrains Mono atlas, and a docking layout. The current implementation hard-codes the font at 16 px and the window at 1600×900, so a 4K display shows physically tiny text. The viewer has no runtime dependency on a desktop toolkit beyond GLFW, so scaling must be applied at startup inside the existing UI initialisation.

## Goals / Non-Goals

**Goals:**

- One process-local scale that applies consistently to font, ImGui style, and initial window size.
- Explicit operator override for displays where GLFW reports no useful content scale.
- Startup-only cost; no new per-frame allocation or transport work.

**Non-Goals:**

- Runtime zoom shortcuts or live font reloading.
- Per-monitor scale changes while the window is open.
- Persisting the scale in `logview.json`; the flag remains an invocation choice.
- Changing the history capacity, capture policy, or exported files.

## Decisions

### D1. Auto-detect, then explicit override

`init_gui` queries `glfwGetMonitorContentScale` on the primary monitor when `--ui-scale` is absent, takes the larger axis, and clamps it to `1.0`–`4.0`. An explicit `--ui-scale` bypasses detection and is validated by the CLI at `0.5`–`4.0`. This keeps normal desktops at `1.0`, gives HiDPI desktops a sensible default, and leaves an operator escape hatch when X11 reports `1.0` despite physical HiDPI.

Alternatives considered: relying on `GDK_SCALE`/desktop scaling (rejected: GLFW/ImGui do not inherit it); `io.FontGlobalScale` (rejected: scales the bitmap and blurs text); environment-only configuration (rejected: less discoverable and harder to test).

### D2. Scale the atlas and style before the first frame

The font is loaded at `16.0F * scale`, then `ImGui::GetStyle().ScaleAllSizes(scale)` runs after the dark theme is applied. The window is created at `round(1600 * scale) × round(900 * scale)`. The docking layout remains ratio-based, so the persisted `.ini` restores relative pane sizes. Scaling before the first frame avoids a resize flash and a second font atlas build.

### D3. Startup-only memory and no hot-path impact

Only the font atlas, style constants, and window dimensions change. No capture, ingest, filter, find, export, or render loop gains work proportional to the scale. Font-atlas memory grows approximately with the square of the scale (the 273 KiB TTF becomes a larger atlas at 2×/3×), while style and window state are constant-size. Ingestion remains bounded by the existing fixed history and SPSC queue.

### D4. CLI and documentation contract

`--ui-scale` belongs to the existing `CliOptions`, is included in `CliOptions::to_string()`, and is documented in `--help` and `docs/articles/log-viewer.md`. The value is parsed with a strict full-token float check; malformed, sub-`0.5`, and above-`4.0` values use the existing invalid-argument path.

### D5. Verification

Add a CLI help/invalid-argument case and an Xvfb smoke test `nexigen-logview --ui-smoke --ui-scale 2`; keep the existing `--ui-smoke`, `--ui-input-test`, and headless tests. The full four-configuration matrix (Debug, Release, ASan, TSan) must stay green for the model, transport, and tool tests.

## Risks / Trade-offs

- **Scale larger than the display** → the initial 1600×900 window may exceed the usable area at 3×/4×; the user can resize it, and the accepted range is capped at 4.0.
- **GLFW reports `1.0` on X11 despite 4K** → explicit `--ui-scale 2` is the documented escape hatch.
- **Blurry text if scaled through the wrong mechanism** → the atlas is rebuilt at the target size rather than bitmap-scaled.
- **Font-atlas memory grows with scale²** → acceptable for a desktop viewer; it is startup-only and bounded by the 4.0 cap.
- **Layout `.ini` from an unscaled run** → ImGui restores proportional dock sizes; the user can reset the layout by deleting the generated `.ini` if a pane is too small.
