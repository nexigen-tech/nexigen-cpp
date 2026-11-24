# Proposal

## Why

Operators can scroll away from the tail of a Log view and currently have no direct
way back to the first or newest record, while auto-follow is implicit and cannot
be deliberately switched off. The viewer also opens straight into the workspace,
which feels abrupt for a branded desktop tool and gives no signal that capture is
starting.

## What Changes

- Add three per-view toolbar controls with Wireshark-like icons and tooltips:
  - **First message** - jump to the oldest displayed record.
  - **Last message** - jump to the newest displayed record.
  - **Auto-scroll to latest** - toggle following newly displayed records.
- Enable auto-scroll on every new LIVE Log view by default.
- Keep the auto-scroll toggle independent from pause: pausing suspends following
  without changing the toggle, and resuming restores the user's choice.
- Make **First message** disable auto-scroll so the operator can inspect the
  oldest record; **Last message** only moves the viewport and leaves the
  auto-scroll toggle unchanged.
- Preserve selection, filters, bookmarks, and the time anchor when navigating.
- Add a one-second startup splash rendered from the supplied
  `images/nexigen_1360x768.png` artwork before the main workspace appears.
- Start capture and model draining before the splash so records received during
  the splash are not lost.
- Skip the splash for headless and scripted/smoke UI modes so CI remains
  deterministic; provide a dedicated Xvfb splash test mode.
- Add a build-time image asset pipeline and runtime PNG decoding for the bundled
  splash image.
- **BREAKING:** none. No persisted configuration, log format, service, or export
  format changes.

## Capabilities

### New Capabilities

None.

### Modified Capabilities

- `logview/presentation`: add first/last navigation, explicit default-on
  auto-scroll, Wireshark-like navigation icons, and the startup splash contract.

## Impact

- **Code:** `tools/nexigen-logview/ui/` (Log-window controls, table scroll state,
  vector icon drawing, splash renderer), `tools/nexigen-logview/CMakeLists.txt`
  (embedded splash asset, PNG decoder), tests, and the user guide. A new splash
  asset is copied from `images/nexigen_1360x768.png` under
  `tools/nexigen-logview/assets/`.
- **Dependencies:** libpng (`PNG::PNG`, 1.6+) for decoding the bundled PNG at
  startup. GLFW/ImGui/OpenGL remain unchanged. Navigation icons are drawn at
  runtime, so they add no image assets or decoder dependency.
- **Performance:** first/last navigation and the auto-scroll toggle are O(1) state
  changes with no allocation or ingest-path work. The splash decode happens once
  on the cold startup path (~3.1 MiB RGB decode buffer plus a ~3.1 MiB texture for
  1360x768), then both are released before the workspace loop. Capture and ingest
  continue unchanged while the splash is visible; no new per-frame or per-record
  allocation is introduced.
- **Compatibility:** interactive GUI startup now takes at least one additional
  second; headless, offline headless, smoke, and scripted test modes keep their
  current timing. Rollback is additive and requires no data migration.
