# Design

## Context

See `proposal.md` — Why. The viewer consumes the `LogFrame` v2 contract
(`logging/iceoryx2-logger`): 256-byte, pointer-free, `app_id[16]`, `context_id` reserved at 0,
`thread_id`, `pid`, `dropped_counter`, `level`, `payload[211]`, `timestamp_ns` in the
`CLOCK_MONOTONIC_RAW` domain. `fix-log-subscriber-capacity` gives the service a 16 384-sample
subscriber depth, 64 publishers, 64 nodes, and safe overflow, so a slow viewer loses the oldest
frames instead of failing publishers.

`nexigen-logd` already demonstrates the transport recipe: `com::Node`, a
`Subscriber<LogFrame, TransportHeader>`, an `EventListener` on `<service>Event`, a `WaitSet` with an
interval fallback, and `chrono::TimeBaseClock` with a sampled-offset fallback. Its JSONL schema
(`timestamp`, `mono_ns`, `boot`, `app`, `level`, `thread_id`, `pid`, `message`) is the interchange
format. The platform has no GUI stack, no CPU/ECO guard, and no indexable history structure: the
existing `SpscRingBuffer` is a queue, not a random-access ring.

## Goals / Non-Goals

**Goals:**

- A separate, crash-isolated process that never changes producer behavior.
- Formally race-free threading: TSAN-clean without suppressions.
- Bounded memory and bounded per-frame work at the full configured history.
- A table that stays interactive at 50 000 msg/s and 60 FPS, degrading to 15 FPS under load.

**Non-Goals:**

- No `LogFrame` v3, no message re-rendering, no source location or context recovery.
- No history on attach beyond the four frames the transport may hand over.
- No GUI on non-Linux platforms in this change; no remote/streaming viewer.
- No native file dialog dependency; the file browser is in-app.

## Decisions

### D1. A new tool, not an extension of the hub

`nexigen-hub` tails JSONL files and renders a run tree; adding a live iceoryx2 stream there would mix
a file-discovery model with a live transport and still lack per-row color, docking, and the pause
semantics. The viewer is `tools/nexigen-logview`, a separate binary. Alternatives considered:
extend `nexigen-hub` (rejected: different data source and UX), embed in `nexigen-logd` (rejected:
a daemon must not link GL/GLFW and must keep running headless).

### D2. Two-stage pipeline with a single owner per ring

```
receive thread (com, WaitSet)          UI thread (owner of everything else)
        |                                       |
        | try_push(IngressRecord)               | try_pop -> ingest
        v                                       v
  SpscRingBuffer<IngressRecord, 65536>     RecordRing (slots + side arrays)
        |                                       |
        +---------------------------------------+--> filter index, selection, minimap
                                                |
                                                +--> export queue -> writer thread
```

The receive thread is the only producer and the UI thread the only consumer of the ingress queue. The
history ring is owned exclusively by the UI thread: it is written only during ingest, which runs on the
UI thread. No structure is read and written by two threads, so there is no seqlock, no torn read, and
nothing for TSAN to flag. A shared ring read by the UI while the receive thread overwrites it was
rejected: it requires a per-slot version protocol and a benign data race under the C++ memory model.

`IngressRecord` is `alignas(64)` and holds `LogFrame frame` (256 B), `std::uint64_t lost_before`, and a
`kind` byte; its size rounds to 320 B, so the queue costs 20 MiB. The queue covers about 1.3 s of UI
stall at 50k msg/s. `SpscRingBuffer` keeps its existing `alignas(64)` producer/consumer cells and
release/acquire index stores; this change adds no atomics or memory orders.

### D3. RecordRing layout

```
RecordRing (capacity = power of two, derived from the budget)
  slots[cap]            256 B each   LogFrame, alignas(64)
  kinds[cap]              1 B        frame | gap | overflow
  prev_same_app_ns[cap]   8 B        delta to the previous record of the same app
  visible[cap]            8 B        absolute record numbers that pass the filter
  selected_bits[cap/64]   8 B/64     slot-indexed selection
  bookmarked_bits[cap/64] 8 B/64     slot-indexed bookmarks
  head, tail            plain u64    absolute range [head, tail); UI-thread only
  app table             256 slots    hash, name, color, counter, last stamp
  minimap               1024 buckets error/fatal/bookmark counts
```

- `seq & (cap - 1)` maps an absolute number to a slot; residency is `head <= seq < tail`.
- On ingest the reused slot's selection and bookmark bits are cleared and the previous minimap bucket
  contribution is decremented, so a stale bit can never attach to a new record.
- `prev_same_app_ns` is computed at ingest from the app table, making the "Per App" delta O(1) at any
  scroll position. The "Previous visible" delta is O(1) from `visible[i-1]`; "From anchor" is one
  subtraction.
- Eviction advances `head`; stale entries at the front of `visible` are skipped at render and compacted
  when more than a quarter are stale.
- Imported files reuse the same ring and assign fresh numbers; a session-wide `utc_offset_ns` derived
  from the first record keeps absolute display cheap without an extra per-record array.

Memory at the default 256 MB budget (capacity 524 288, ingress 65 536 × 320 B):

| Buffer | Size |
|---|---|
| `slots` | 128.0 MiB |
| `kinds` | 0.5 MiB |
| `prev_same_app_ns` | 4.0 MiB |
| `visible` | 4.0 MiB |
| bitsets + minimap | ~0.2 MiB |
| ingress queue | 20.0 MiB |
| **total model** | **~157 MiB** |

The remainder of the budget covers ImGui, the font atlas, GL driver buffers, and the window; the
capacity formula subtracts the ingress and a fixed reserve before dividing by 273 B. One `mmap` at
startup (`MAP_NORESERVE`, zero-filled) keeps RSS proportional to use and guarantees no runtime growth.

### D4. Pause is a reader position, not a capture switch

Pause freezes `view_floor` and `view_end`; ingest continues and `tail` grows. When `head` passes
`view_floor`, the UI clamps the floor to `head` and shows `N overwritten while paused`. No snapshot is
copied: it would cost up to 256 MB and still break when the user scrolls past it. The honest eviction
banner is the contract.

### D5. Filtering is an index, not a copy

`visible[]` stores absolute record numbers. New matching records append in O(1); a criterion change
rebuilds over `[head, tail)` in chunks of 20 000 records per frame (about 4.2 MB of payload scan) with a
progress bar. Rows evicted during a rebuild are skipped by the residency check. Find does not touch
`visible`; it scans it in chunks and stores match positions in a pre-sized array. Selection and
bookmarks are slot-indexed bitsets with the clearing rule from D3.

### D6. Time formatting

`TimeBaseClock::attach("/nexigen-timebase")` is attempted once at startup, like `nexigen-logd`. When it
fails, the viewer samples a UTC offset and shows a no-time-base badge. Absolute time is
`utc_from_canonical(timestamp_ns)` or `utc_from_mono_ns`; Since Boot is the raw monotonic value.
Formatting uses `chrono::civil_from_utc_ns` and stack buffers, never `std::string` per row. Records are
never sorted: per-app worker order can produce negative deltas, which are displayed as negative.

### D7. Rendering

Dear ImGui docking branch with GLFW and the OpenGL3 backend; ImGui's bundled loader is used, so no
glad dependency is required. The table uses `BeginTable` + `ImGuiListClipper`, which draws only the
visible ~60 rows regardless of the visible set size. Per-row work is formatting into stack buffers and
resolving `seq -> slot`; there is no per-row allocation on our side. ImGui's own frame pools are
bounded by draw data and are explicitly out of the "no allocation" rule.

The layout is a dockspace with the toolbar, a left sources/levels panel, the table, and a bottom
inspector. The layout is stored by ImGui in a dedicated `.ini` next to the configuration.

### D8. Colors

A fixed level table (text + background) and a fixed 256-slot application registry with an ordered
palette. New applications take the next unused color. Combined and criticality modes are a single
render-time branch. Colors are persisted as JSON; Reset restores the built-in table. No color logic
allocates.

### D9. Resources and ECO

The process CPU share comes from `clock_gettime(CLOCK_PROCESS_CPUTIME_ID)` deltas over a one-second
window, not `/proc/self/stat`: no parsing, no allocation, and it includes every viewer thread. Entry
requires three consecutive windows above the configured limit; exit requires two seconds below 70% of
it. A rate EWMA above 100 000 msg/s also enters ECO. ECO uses `glfwWaitEventsTimeout(1/15)` (input
wakes immediately) and recomputes filters/statistics at most every 250 ms. The receive thread runs
`SCHED_OTHER` with a low `nice`; the export thread is lower still. Affinity comes from
`core::ThreadSpec`/`CpuSet`.

### D10. Export and import

The UI pushes `ExportRecord` (an `alignas(64)` struct with the frame, absolute number, both deltas, and
kind) into a 4096-slot SPSC queue; the writer thread formats JSONL/CSV/TXT and writes. The UI never
blocks and never shares the ring with the writer. JSONL uses the daemon's field names and correct
escaping (the daemon currently interpolates `message` with `%.*s`, so a quote or newline can break its
JSON; import tolerates that, export does not reproduce it). Import parses in bounded chunks on the UI
thread, skips malformed lines with a count, and refuses to present deltas across different boot tokens
without a warning. `--offline <file>` skips COM entirely, which also makes UI tests runnable without a
transport.

### D11. Build and dependencies

A `NEXIGEN_BUILD_LOGVIEW` option (Linux-only, requires `NEXIGEN_ENABLE_COM=ON`) adds the target. ImGui
(docking branch, pinned commit), GLFW 3.4, and the JetBrains Mono TTF (OFL, license checked in) are
fetched or embedded with pinned hashes, matching the existing `nlohmann_json` pin policy. Headless unit
tests link only the model layer, so ring, filter, time, color, import/export, and resource logic are
tested without GL; a GUI smoke test runs under Xvfb in CI.

### D12. Configuration

One JSON document at `$XDG_CONFIG_HOME/nexigen/logview.json` (with `--config` override) holds colors,
filters, time/delta modes, thresholds, memory budget, CPU limit, service name, and thread options.
Parsing is non-throwing under `JSON_NOEXCEPTION=1`, wrong types leave defaults untouched, and a corrupt
file produces one warning and defaults. The same document is the round-trip target for the
`to_string`/`std::formatter` mandate on the config value types.

## Risks / Trade-offs

- **UI stall fills the 64k ingress queue** → 1.3 s of slack at 50k msg/s; overflow is counted and shown
  as a gap, and the queue is the first knob to raise.
- **Pause expectation vs eviction** → the banner states exactly how many displayed records were
  overwritten; no silent substitution.
- **Filter rebuild on a full 1M history at 15 FPS** → 1M / 20k = 50 frames ≈ 3.3 s with progress; the
  UI stays interactive and new records continue to append.
- **~160 MB model + ~30 MB GUI** → capacity is derived from the budget, not hard-coded; lowering the
  budget lowers the history.
- **ImGui/GLFW in CI** → headless tests cover the model; the GUI job uses Xvfb; sanitizer runs exclude
  the GUI target except for the headless smoke.
- **Exported records can be evicted mid-export** → the queue carries copies, so already-queued rows are
  safe; rows evicted before queueing are reported as gaps.
- **Daemon JSONL can be invalid** → import is line-tolerant; export escapes correctly.
- **`context_id` is always 0** → the Context column shows "—"; no invention.
- **Viewer is one more subscriber** → the shared capacity budget reserves one slot (4 total: daemon,
  recorder, viewer, spare).

## Migration Plan

1. Land `fix-log-subscriber-capacity` first; the viewer uses `log_service_options()`.
2. Add the target behind `NEXIGEN_BUILD_LOGVIEW` (off by default on non-Linux, on for the GUI CI job).
3. Ship the binary with the platform packages; it is read-only and can be started or stopped at any
   time without affecting producers.
4. Rollback: stop the viewer; no service or on-disk state is modified.

## Open Questions

- The exact ImGui docking commit and GLFW patch release are pinned at implementation time; either pin
  can change without changing the spec or the architecture.
- Whether the receive thread uses `SCHED_BATCH` or `SCHED_OTHER` plus `setpriority` is a portability
  detail; the observable requirement is "low priority, no RT".
