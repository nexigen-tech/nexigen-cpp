# Design

## Context

See `proposal.md` - Why. The current UI keeps per-view scroll state in
`ViewUiState` (`auto_scroll`, `scroll_to`) and applies it after drawing the table
(`tools/nexigen-logview/ui/logview_window.cpp:828-852`). A LIVE unpaused view
follows the tail while `auto_scroll` is true; manual wheel/drag clears it. There
is no explicit first/last action and no visible auto-scroll toggle, and the
documented "Return to live" control is not present. Pause/resume currently
overwrites `auto_scroll`, which conflicts with an operator-controlled toggle.

`run_ui()` creates the GLFW/ImGui window, starts capture, then enters the main
loop. The splash will be inserted between capture start and the main workspace
loop so capture and model draining begin before the one-second delay.

The supplied image is `images/nexigen_1360x768.png` (1360x768, 8-bit RGB,
non-interlaced, 1,244,698 bytes).

## Goals / Non-Goals

**Goals:**

- Three view-local controls: First, Last, and auto-scroll toggle.
- First/last navigation never changes selection, filters, bookmarks, or pause state.
- Auto-scroll is enabled for every new view and remains independent from pause.
- One-second branded splash without losing records received while it is visible.
- Self-contained binary: splash artwork travels with the executable.
- No new hot-path allocations and no new concurrent state.

**Non-Goals:**

- Persisting splash on/off or a custom splash path.
- Changing pause semantics or persisting the auto-scroll choice.
- Changing the log wire format, capture, export, or persisted view schema.
- Adding an animated splash or user-dismissible splash.

## Decisions

### D1. Three navigation controls use the existing row-position state

`ViewUiState::auto_scroll` already defaults to true; remove the pause/resume
assignments that overwrite it so pause only suspends following. The per-view
local controls add three icon buttons:

- **First**: set `scroll_to = display_at(0)` and `auto_scroll = false`.
- **Last**: set `scroll_to = display_at(display_count() - 1)` and leave
  `auto_scroll` unchanged.
- **Auto-scroll**: flip `auto_scroll`; when enabling it, also position on the
  newest visible row.

`apply_table_scroll()` continues to consume `scroll_to`; when auto-scroll is on
and the view is live and unpaused, it follows the bottom on subsequent frames.
An empty view is a no-op. Selection, filters, bookmarks, and T0 are untouched.
Manual wheel/drag keeps the existing behavior of turning auto-scroll off, which
makes the toggle discoverable after manual navigation.

Keyboard shortcuts: `Ctrl+Home` and `Ctrl+End` on the focused Log view for First
and Last. The auto-scroll toggle is button-only and always shows its current
state.

Alternative considered: reuse a "return to live" action that always unpauses.
Rejected because it would destroy the operator's frozen inspection position and
would conflict with the separate auto-scroll control.

### D1a. Wireshark-like icon drawing

The three controls are drawn as custom vector icons with `ImDrawList` inside
`InvisibleButton` hit areas, not copied bitmap artwork:

- First: a vertical bar plus a left-pointing arrow.
- Last: a right-pointing arrow plus a vertical bar.
- Auto-scroll: a downward arrow over a horizontal baseline, tinted when active.

Each control has a tooltip (`First message (Ctrl+Home)`, `Last message
(Ctrl+End)`, and `Auto-scroll: on/off`). The shapes are intentionally similar to
Wireshark's navigation controls while remaining original project artwork.

### D2. Splash is an overlay in the main window, not a second GL context

After `init_gui()` and capture start, run a bounded `run_splash()` loop until a
steady-clock deadline of one second. Each splash frame:

1. drains the capture queue through `process_model_frame()`;
2. starts ImGui/GLFW frames normally;
3. draws one full-viewport, non-interactive `NoDecoration | NoMove | NoResize |
   NoSavedSettings | NoInputs` window with the splash image;
4. presents the frame.

Then the loop ends, the texture is deleted, and the normal workspace loop starts.
This keeps one GLFW/ImGui context and avoids a second window during startup.
Closing the window during the splash exits the process without entering the
workspace loop.

Alternative considered: a separate borderless GLFW window with its own context.
Rejected for startup complexity and extra GL state.

### D3. Bundled image decoding and lifetime

The source artwork is copied to
`tools/nexigen-logview/assets/nexigen_splash.png`. CMake embeds it as a generated
byte array, following the existing JetBrains Mono font pattern in
`tools/nexigen-logview/CMakeLists.txt`. The GUI target requires libpng
(`find_package(PNG 1.6 REQUIRED)`; link `PNG::PNG`) and decodes the embedded PNG
after the OpenGL context exists using the simplified `png_image` API with
`PNG_FORMAT_RGB`.

The decoded RGB buffer is exactly `1360 * 768 * 3 = 3,133,440` bytes. It is
uploaded to one `GL_RGB` texture (same 3,133,440 bytes) and freed immediately;
the texture is deleted after the splash loop. The embedded compressed asset is
about 1.2 MiB and remains in the executable. Peak startup overhead is therefore
about 7.5 MiB (asset + decode + texture), dropping to the embedded asset after
the splash.

No new mutex, atomic, shard, or cache-line layout is introduced. All splash and
scroll state is owned by the UI thread; the existing capture hand-off remains the
only concurrent boundary. There is consequently no new `std::memory_order` or
`alignas(64)` requirement.

Image load failure logs a diagnostic and skips the splash; it never blocks the
workspace. The splash uses a uniform fit scale and centers the image, with the
theme background letterboxing the remainder.

### D4. Splash duration and test modes

The splash uses a one-second `std::chrono::steady_clock` deadline measured from
the first splash frame. Headless, `--ui-smoke`, offline smoke, action/input,
view, and focus test modes skip the splash so existing CI timing stays stable.
A dedicated `--ui-splash-test` mode runs the splash for its normal one second,
then enters a short workspace session before exiting. It is the Xvfb coverage
for the startup path: it prints the decoded dimensions/texture state, the number
of records ingested during the splash, whether injected input was ignored, and
whether the workspace appeared with a stable view set. The same script runs a
second `--ui-scale 2` pass for the high-DPI aspect-ratio scenario.

### D5. Navigation test seams

Extract the first/last visible sequence selection and the three navigation
actions (First, Last, auto-scroll toggle) into a small `ui_actions` helper that
shares a plain `ViewScrollState` with the window. Headless tests cover filtered,
paused, PAUSED-snapshot, and empty views, plus state preservation for selection,
bookmarks, the time anchor, and pause. The Xvfb view test checks default-on
auto-scroll, pause/resume preserving the toggle, the three icon hit areas, then
drives real mouse clicks to toggle auto-scroll off and on and a wheel event to
confirm manual scrolling turns it off. `Ctrl+Home`/`Ctrl+End` focus routing is
covered by the separate focus test.

## Risks / Trade-offs

- **libpng becomes a GUI build/runtime dependency** → Add it to the documented
  build prerequisites and CMake; if a target lacks libpng, fail at configure time
  rather than silently omitting the splash.
- **One-second startup delay** → Capture and draining start before the splash, so
  the delay affects only first visibility, not received history.
- **Splash texture memory** → Free the decode buffer immediately and delete the
  texture after the splash; only the compressed asset stays resident.
- **`Ctrl+Home`/`Ctrl+End` may interact with future table navigation** → Handle
  them only when a Log window owns focus and no text input is active; keep the
  buttons as the discoverable path.
- **Missing/broken artwork** → Report and continue to the workspace.

## Migration Plan

1. Add the splash asset and PNG decoder; existing installations need no config
   migration.
2. Existing GUI test modes skip the splash, so CI behavior is unchanged except
   for the new dedicated splash test.
3. Rollback is additive: removing the splash asset/decoder and the three
   navigation controls restores the previous startup and scrolling behavior.

## Open Questions

None. The one-second duration, bundled asset, default-on auto-scroll, and
pause-preserving navigation behavior are fixed by the request and the
requirements above.
