# Proposal

## Why

The platform already publishes every CAN/CAN-FD frame and channel status through `nexigen/hw/can/<channel>/rx` and `.../status`, but the only consumer is the line-oriented `nexigen-canmon` CLI. Engineers analysing a live bus need a bounded, real-time GUI with independent views, per-ID period/jitter analysis, CANopen decoding, bus-load history, and offline replay. The proven `nexigen-logview` architecture supplies the real-time viewer pattern; this change applies it to the CAN record model while leaving the existing CLI and transport contracts untouched.

## What Changes

- Add a new Linux-only GUI tool **`nexigen-canview`** built from `tools/nexigen-canview/`, with a headless mode sharing the same model.
- Capture up to **8 simultaneous live CAN channels** through one capture thread and one `com::EventConsumerSet<16>`; retain up to **32 channel profiles** for configuration and offline sessions. The capture path is strictly read-only and creates no `tx` services.
- Add a fixed-capacity history ring of **126 B/slot** (`CanMessage` + per-ID delta + CANopen node slot + record meta + time epoch + CANopen mapping epoch), with absolute `seq`, eviction, and five record kinds: `Frame`, `BusIdleHeartbeat`, `TransportGap`, `IngressOverflow`, `DriverDrop`.
- Add independent dock views with `All`, `Channel`, and `Node` scopes, LIVE/session-only PAUSED clones, `Trace` and `Overwrite` table modes, per-view filters, find, selection, bookmarks, minimap, T0, and two optional time columns (`HW`, `UTC (est.)`).
- Add CANopen analysis for CiA 306 `*.eds`/`*.dcf`: shared immutable dictionaries, 128 `(channel_id, node_id)` bindings, per-channel COB-ID routing, NMT/EMCY/SDO/PDO decoding, node colours/NMT state, and passive dynamic PDO remapping with per-record mapping epochs.
- Add a bus-load heatmap derived from the ring (BusIdle measured load, frame bit-time fallback), channel/ID-bin axes, switchable UTC/HW/shared time axis, click-to-record, plus a 64x8 bit-activity matrix.
- Add the canonical `nexigen.canview.session` JSONL v1 format (session header, frame/gap/status records) and import of legacy `nexigen-canmon --json` lines; JSONL is canonical, CSV/TXT are export-only.
- Add the memory ledger (worst-case 164 B/slot including indexes), separate EDS budget, degraded 16 MiB mode, ECO guard, async export writer, and scripted Xvfb UI test modes.
- Hoist the pinned ImGui/GLFW build targets to a shared top-level third-party target and add a pinned ImPlot target; existing `nexigen-logview` links the hoisted targets without behaviour change.
- **No breaking changes** to `nexigen-canmon`, `nexigen/hw/can` payloads, service names, or transport semantics.

## Capabilities

### New Capabilities

- `hw/can/viewer`: real-time CAN/CAN-FD GUI monitor behaviour — capture, history, record kinds and losses, views, filters/find, time and UTC estimation, heatmap, resources/ECO, CLI and UI.
- `hw/can/canopen-viewer`: passive CANopen analysis behaviour — EDS/DCF loading and binding, COB-ID routing, NMT/EMCY/SDO/PDO decoding, dynamic PDO remapping, node presentation and limits.
- `hw/can/viewer-session`: canonical CAN session JSONL v1, legacy `nexigen-canmon --json` import, CSV/TXT export, round-trip and offline semantics.

### Modified Capabilities

None. The existing `hw/can/monitor-app`, `hw/can/client`, `hw/can/transport`, and `logview/*` requirements are unchanged. The third-party CMake hoist is a build detail with no spec-level behaviour change.

## Impact

- **Code**: new `tools/nexigen-canview/` (backend, model, canopen, ui, tests); new top-level third-party GUI targets; `tools/nexigen-logview/CMakeLists.txt` switches to the hoisted ImGui/GLFW targets; no `include/nexigen/` public headers change.
- **APIs**: no change to `nexigen::hw::can` payloads or service names. The viewer uses existing `nexigen::com`, `nexigen::chrono`, and `nexigen::hw::can` contracts; it does not add a `tx` publisher.
- **Dependencies**: adds a pinned ImPlot revision compatible with the existing pinned ImGui docking commit; ImGui/GLFW are reused rather than duplicated.
- **Performance / latency**: capture callbacks only validate IDs, update sequence/loss/anchor state, and copy into fixed SPSC queues — no allocation, no formatting, no blocking. The UI drain budget is 2–3 ms per frame at 5 000 frames/s and remains bounded during a 50 000 frames/s storm; ECO drops the render rate to 15 FPS and recomputes derived aggregates at most every 250 ms. Export runs on a nice-19 thread fed by a 4 096-slot SPSC queue. No hot-path `std::string`, `std::format`-to-string, `new`, or container growth is introduced.
- **Memory**: history is one anonymous `mmap` of 126 B/slot; ingress is 65 536 x 128 B = 8 MiB; the worst-case ledger including eight full LIVE indexes is 164 B/slot, preserving the 1 048 576 / 2 097 152 capacities at 256 / 384 MiB. PAUSED snapshots are capped at 1 MiB each; the Overwrite table is bounded (default 4 096 rows); EDS dictionaries have a separate 32 MiB default budget; 16 MiB configuration runs a documented degraded mode.
- **Systems**: read-only CAN monitoring only; no driver, sender, or transport changes. Offline replay reads JSONL and performs no COM operations.
- **Testing**: new unit, transport, offline, soak, and scripted Xvfb UI suites; Debug, Release, TSan, and ASan builds must pass.
