# Proposal

## Why

The platform can publish log frames and persist them through `nexigen-logd`, but there is no
interactive way to watch a live suite at 50 000 msg/s, filter it, inspect a record, or compare timing
across applications. Engineers fall back to tailing JSONL files or the file-oriented `nexigen-hub` web
viewer, both of which lose the live service contract (per-app color, pause with eviction semantics,
sub-millisecond deltas, bookmarks) that debugging a real-time platform needs. This change adds a
desktop viewer that attaches to the existing `LogFrame` service and never asks applications to change.

## What Changes

- Add a Linux-only GUI tool `nexigen-logview` under `tools/nexigen-logview/`, built only with
  `NEXIGEN_ENABLE_COM=ON`, linking `nexigen_logging_com`, `nexigen_com`, `nexigen_chrono`,
  `nexigen_core`, and the GUI dependencies.
- Capture backend: `com::Node` + `com::Subscriber<LogFrame, TransportHeader>` with the shared
  `log_service_options()` from `fix-log-subscriber-capacity`, an `EventListener` on
  `<service>Event`, and a `WaitSet` drain loop on a `SCHED_BATCH` receive thread.
- Two-stage history: the receive thread pushes into a fixed
  `core::SpscRingBuffer<IngressRecord, 65536>`; the UI thread is the sole owner of a fixed-capacity
  `RecordRing` with absolute record numbers, overwrite-oldest eviction, and a pause floor.
- UI: Dear ImGui docking branch with GLFW + OpenGL3, embedded JetBrains Mono, dark engineering theme,
  `BeginTable` + `ImGuiListClipper`, sources/levels panels, filter + find, message inspector, and a
  severity/bookmark minimap.
- Pro features: timestamp modes (absolute / since boot / relative to T0), three delta modes with
  adaptive formatting and anomaly highlighting, bookmarks (F2), next/previous ERROR/WARN navigation,
  multi-select and async export (JSONL/CSV/TXT), offline import of `nexigen-logd` JSONL.
- Color manager for levels and applications with two display modes and JSON persistence.
- Resource guard: capacity derived from a hard memory budget, chunked filter rebuild (20 000 records
  per frame), CPU-based ECO mode at 15 FPS, thread affinity/priority, and all runtime buffers
  preallocated.
- No **BREAKING** changes. No wire-format change; `LogFrame` v2 is read-only. No history/backlog
  promise on attach. No runtime log-level control (the platform's dynamic level API is not
  implemented). No Windows/macOS GUI and no remote streaming in this change.

## Capabilities

### New Capabilities

- `logview/capture`: attach to the log service, validate frames, drain without blocking the UI, and
  surface transport/viewer losses as explicit gap records.
- `logview/history`: the fixed-capacity record history, absolute indices, overwrite-oldest eviction,
  and pause/floor semantics.
- `logview/filtering`: source/level/text filtering, the incremental visible index, find-versus-filter,
  selection, and bookmarks.
- `logview/time`: timestamp display modes, delta computation modes, and the T0 anchor against the
  platform time base with a fallback.
- `logview/presentation`: level/application color rules, the docking layout, the virtualized table,
  the message inspector, and the minimap.
- `logview/resources`: the memory budget, chunked UI work, ECO throttling, thread affinity, and the
  asynchronous export queue.
- `logview/offline`: saving selected records, opening saved JSONL/plain-text files, and persisting
  view configuration.

### Modified Capabilities

- None. The viewer consumes `logging/iceoryx2-logger`, `logging/log-daemon`, `com/publish-subscribe`,
  and `chrono/time-base` as they are; their behavior does not change.

## Impact

- **Code:** new `tools/nexigen-logview/` (backend, UI, tests), a new CMake target and GUI dependency
  block, a font asset and its OFL license. Existing modules are unchanged; the only platform-side
  effect is one additional subscriber on `Nexigen/Log/nexigen` (budgeted by `max_subscribers=4`).
- **Dependencies:** Dear ImGui (docking branch, pinned commit), GLFW 3.4 (FetchContent), glad (or a
  minimal GL loader), JetBrains Mono (OFL, embedded). `nlohmann_json` 3.12.0 stays the only JSON
  library. System `glfw3` and monospace fonts are absent on the build host, so the dependencies are
  fetched and pinned rather than discovered.
- **Performance and latency:** the viewer runs in its own process and changes nothing on the producer
  hot path. The receive thread performs one `memcpy` of 256 B per frame into the ingress queue
  (12.8 MB/s at 50k msg/s); the UI thread drains the queue and updates the ring in O(new records).
  Per-frame filter work is capped at 20 000 records (about 4.2 MB of payload scan) to stay under
  2–3 ms; export is handed to a `nice 19` writer thread through a bounded queue.
- **Memory:** capacity is derived from a hard budget: default 256 MB yields 524 288 records
  (`256 B` slot + `1 B` kind + `8 B` per-app delta + `8 B` index entry + bitsets ≈ 273 B/record,
  about 140 MiB) plus a 20 MiB ingress queue. Allocation is one `mmap` at startup; no runtime `new`,
  `malloc`, `std::string`, or container growth in the receive/ingest/render paths. ImGui's internal
  frame pools are bounded by visible rows, not by record count.
- **UI budget:** 50 000 msg/s at 60 FPS is 833 records/frame to ingest; at ECO 15 FPS it is 3 333.
  The ingress queue covers about 1.3 s of UI stall and reports any overflow.
- **CI:** a GUI build job (or an Xvfb smoke test) is required; headless unit tests cover the ring,
  filtering, time formatting, colors, import/export, and the resource guard without OpenGL.
- **Compatibility:** export uses the `nexigen-logd` JSONL field names so files are interchangeable.
  Import tolerates the daemon's current unescaped `message` output and any partial trailing line.
