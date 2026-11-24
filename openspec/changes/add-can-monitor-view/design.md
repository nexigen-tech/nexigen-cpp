# Design

## Context

See `proposal.md` for motivation. The design builds on the existing, working pieces:

- `nexigen::hw::can::CanMessage` (112 B, `alignas(8)`) and `CanChannelStatus` (72 B), carried by `nexigen/hw/can/<name>/rx` and `.../status`.
- `nexigen::com::EventConsumerSet<16>`, `EventConsumer<T>`, `EventHeader`, and `LossInfo`, which already provide notification multiplexing, transport sequence tracking, loss classification, and `ingest_timestamp_ns` (host `CLOCK_MONOTONIC_RAW`).
- `nexigen::chrono::TimeBaseClock`, which maps canonical `CLOCK_MONOTONIC_RAW` to UTC when the time-base service is attached.
- The proven `nexigen-logview` viewer pattern: one anonymous `mmap` history, absolute sequence numbers, independent dock views, a bounded ingress queue, a 4 096-slot async export queue, an ECO guard, and scripted Xvfb UI tests.
- The existing `nexigen-canmon` CLI, which is unchanged and remains the legacy JSONL producer.

Constraints from the repository: C++23, `-fno-exceptions`, `-fno-rtti`, no hot-path heap allocation, no blocking primitives on hot paths, explicit `std::memory_order`, `alignas(64)` for shared atomics, file-per-entity layout, and universal `to_string`/`std::formatter`/JSON for public enums and structs.

## Goals / Non-Goals

**Goals:**

- A read-only GUI/headless CAN monitor with deterministic memory and bounded UI work.
- A record model that preserves driver arrival order, exposes every loss class, and keeps enough per-record metadata for time and CANopen without a second history.
- Independent `All`/`Channel`/`Node` views with `Trace`/`Overwrite`, per-view filters/find/selection/bookmarks, LIVE/PAUSED clones, and an offline-capable session format.
- CANopen analysis from EDS/DCF with passive dynamic PDO remapping.
- A derived bus-load heatmap and bit-activity matrix bounded by the visible window.
- Build and test seams that let a future `common/monview` extraction happen without changing public behaviour.

**Non-Goals:**

- CAN transmit, NMT/SDO master commands, node emulation, or DBC signal decoding.
- Replacing or modifying `nexigen-canmon`, `nexigen/hw/can`, `CanClient`, or the transport.
- Recording a live CAN session to disk. Export writes selected records; there is no continuous recorder.
- Multi-host time calibration. UTC is an estimate anchored on the local time base and the publisher's ingest timestamp.
- Extracting `common/monview` now. The folder is a later refactor; this change only keeps the seams clean.
- Block transfer SDO, node guarding, or CANopen over 29-bit identifiers.

## Decisions

### D1 — A separate `nexigen-canview` tool, no shared viewer library yet

`tools/nexigen-canview/` is a self-contained tool with its own model, capture, CANopen, UI, and tests. It links platform libraries (`nexigen_core`, `nexigen_chrono`, `nexigen_com`, `nexigen_hw_can`) and the shared third-party GUI targets, but shares no viewer code with `nexigen-logview` in this change.

Rationale: the user selected a two-binary layout, and the CAN record model differs enough that premature abstraction would couple two tools. The future `common/monview` extraction is a separate refactor after this tool stabilizes.

Alternatives rejected: a shared viewer library now (over-abstraction, destabilizes logview) and a fork of all logview code without boundaries (harder future extraction). Files that are natural future `monview` candidates keep domain types out of their interfaces.

Targets:

```text
nexigen_canview_model   static, no OpenGL/GLFW
nexigen_canview_com     static, capture + queues
nexigen-canview         app, optional GUI, links ImGui/ImPlot/GLFW/PNG
nexigen_canview_tests   headless model + transport tests
```

CMake: `NEXIGEN_BUILD_CANVIEW` (default OFF), `NEXIGEN_CANVIEW_GUI` (default ON when the tool is built), Linux only, `NEXIGEN_ENABLE_COM=ON` required. The app target is installed only when enabled.

### D2 — Capture: one thread, one `EventConsumerSet<16>`, 8 live channels

A single `canview-rx` thread owns one `com::Node`, one `com::EventConsumerSet<16>`, and 8 `EventConsumer<CanMessage>` + 8 `EventConsumer<CanChannelStatus>` (one pair per live channel). The set multiplexes notification, tracks transport sequence, and invokes loss callbacks; the thread never calls `CanClient` or `App::run`.

- Live limit: **8** channels. Config/offline profiles: **32**.
- More than 8 enabled channels is a configuration error before any service is created.
- Service creation failure for one channel is reported as `channel failed: <reason>`; remaining channels continue. If none start, capture stays stopped.
- A `Capture: Start / Stop / Restart` control supports an empty config and recovery after a failed channel.
- The thread uses `SCHED_OTHER`, nice 10, optional validated affinity; the thread name is `canview-rx`.
- `EventConsumerSet::wait_for(50 ms)` provides a stop-check boundary; the WaitSet is created without transport signal handling.

Queues (all SPSC, one producer = capture thread, one consumer = UI):

```text
IngressQueue: 65 536 x CanIngressRecord (128 B) = 8 MiB
StatusQueue:   1 024 x StatusEvent
AnchorQueue:     256 x AnchorEvent
```

`CanIngressRecord` layout (`alignas(64)`, exactly 128 B):

| Offset | Field | Size |
|---|---|---|
| 0 | `CanMessage msg` | 112 B |
| 112 | `uint64_t lost_before` | 8 B |
| 120 | `CanRecordKind kind` | 1 B |
| 121 | `uint16_t time_epoch` | 2 B |
| 123 | `uint8_t co_map_epoch` | 1 B |
| 124 | `uint8_t flags` | 1 B |
| 125 | `uint8_t anchor_source` | 1 B |
| 126 | `uint8_t reserved[2]` | 2 B |

`StatusEvent`: `{ uint16_t channel_id; uint8_t flags; CanChannelStatus status; }`, `alignas(8)`.
`AnchorEvent`: `{ uint16_t channel_id; uint16_t epoch; uint8_t source; uint8_t flags; int64_t utc_offset_ns; int32_t drift_ppb; uint64_t valid_from_hw_ns; }`, `alignas(8)`.

SPSC memory order (the existing `core::SpscRingBuffer` contract):

- producer: `head.load(relaxed)`, `tail.load(acquire)`, write slot, `head.store(release)`;
- consumer: `tail.load(relaxed)`, `head.load(acquire)`, read slot, `tail.store(release)`;
- indices are `alignas(64)`; slots are cache-line aligned, no false sharing between indices and payload.

The capture callback performs only: channel binding check, hardware-sequence bookkeeping, loss/anchor state update, and `try_push`. No `validate()`, formatting, filtering, allocation, or blocking. Queue overflow increments an atomic `pending_ingress_overflow`; the UI turns it into an `IngressOverflow` record.

### D3 — Record model: 126 B/slot, five kinds, explicit gap reasons

Ring storage is one anonymous `mmap` with parallel arrays, each `alignas(64)`:

```text
CanMessage frames[N]                 112 B
int64_t    delta_same_id_ns[N]         8 B
uint16_t   co_node_index[N]            2 B
uint8_t    record_meta[N]              1 B
uint16_t   time_epoch[N]               2 B
uint8_t    co_map_epoch[N]             1 B
------------------------------------------
per slot                              126 B
```

For `Frame`, `delta_same_id_ns` is the signed delta to the previous frame with the same `(channel_id, can_id, extended)`; for synthetic records it carries `gap_count`; for `BusIdleHeartbeat` it is zero. Synthetic records use a zeroed `CanMessage` with a valid `hardware_timestamp_ns`, `channel_id`, and `timestamp_source` for the reference time.

`CanRecordKind`: `Frame`, `BusIdleHeartbeat`, `TransportGap`, `IngressOverflow`, `DriverDrop`.

`record_meta`:

- bits 0..2 kind;
- for `Frame`/`BusIdleHeartbeat`: bit 3 `InvalidStruct`, bit 4 `TimestampInvalid`, bit 5 `DuplicateReorder`, bit 6 `SequenceReset`, bit 7 reserved;
- for synthetic: bits 3..5 `GapReason`, bit 6 `CountExact`, bit 7 reserved.

`GapReason`: `HardwareSequence`, `TransportGap`, `TransportRestart`, `TransportDuplicate`, `IngressOverflow`, `DriverDropped`, `DriverReportedSequenceGap`, `SequenceReset`.

Loss synthesis:

- `Frame.lost_before` is set from the per-channel `hardware_sequence` gap. The UI writes a `TransportGap(HardwareSequence)` immediately before the frame.
- `com::LossInfo` becomes a `TransportGap` ingress record with its `LostReason` mapped to `GapReason`.
- `IngressOverflow` is synthesized from the atomic overflow counter at drain.
- `DriverDrop` is synthesized from the delta of `CanChannelStatus::dropped_count`.
- `CanChannelStatus::sequence_gap_count` is a fallback only when frames lack `has_hardware_sequence`.
- Sequence handling: forward jump -> gap; equal/backward -> duplicate/reorder unless `controller_restart`, `BusOff`, or `Stopped` marks a reset; a forward jump beyond `max_sequence_gap` becomes a `SequenceReset` with a warning instead of a giant gap.

`validate_for_view()` is viewer-local and does not modify `CanMessage`:

- base rules from `CanMessage::validate()`;
- accept `Remote` with `dlc 0..8` (the library validator currently rejects it for transmit);
- accept `FdData` only with legal lengths `0..8, 12, 16, 20, 24, 32, 48, 64`;
- require `has_hardware_timestamp` for a usable HW timestamp and `has_hardware_sequence` for gap detection;
- invalid records are stored as `Frame` + `InvalidStruct`, counted in `rejected`, and never read past `data[64]`.

Counters are measured separately: received frames, BusIdle, synthetic, rejected, evicted, and the loss breakdown by reason; duplicate/reorder/reset are separate counters.

### D4 — Views: `All` / `Channel` / `Node`, LIVE and bounded PAUSED

- `All` is the default and the only kind whose channel mask is editable.
- `Channel` scope is fixed to `channel_id`.
- `Node` scope is fixed to `(channel_id, node_id)`; it resolves to a registry slot at runtime, becomes `orphaned` if the EDS binding disappears, and recovers if it returns.
- Limits: **8 LIVE** including `All`, **16 PAUSED**.
- PAUSED clones copy an immutable snapshot of at most 1 MiB of record data, newest to oldest, materialized in ring order. Selection/bookmarks outside the snapshot are clipped; T0 outside is cleared; find re-runs.
- LIVE views persist; PAUSED, selection, bookmarks, T0, find, and scroll are session-only.
- Left-panel channel/node checkboxes are shortcuts for the **focused** view's filter; they never change subscriptions. Subscription changes require `Restart capture`.
- Focus routes keyboard actions and the Inspector; no focus means global hotkeys are ignored.

Each view owns: filter criteria, a circular `uint32_t` visible index, mode-specific selection, bookmarks, find, minimap, scroll/auto-scroll, pause/frozen history, table mode, time mode/columns, delta mode, anomaly threshold, T0, and export selection.

### D5 — Filtering and find

Per-view criteria: 32-bit channel mask, ID + mask, 128-bit mask over `(channel_id, node_id)` registry slots, kind mask, origin mask, errors-only, show BusIdle, show gaps, and a text/hex query.

- Gaps pass channel + show-gaps/errors-only + textual reason matching; they bypass ID/node/kind/origin. A filter on an ID must not hide the loss that explains its gap.
- BusIdle is hidden by default and matched only by channel/kind/text.
- Errors-only is a shortcut: `Error | Overload | InvalidStruct` plus all gaps; BusIdle hidden.
- Text query is auto-detected as a contiguous hex byte sequence or a case-insensitive text match over channel alias, node alias, gap reason, and printable ASCII data. Decoded signal names are a `Find` feature, not a filter feature, so ingest filtering stays O(1).
- Trace index is a circular vector of `uint32_t` slots; new matching records append, evictions advance the head, criteria changes rebuild in chunks of 20 000 records per frame with progress.
- Overwrite filters apply to at most 4 096/16 384 row states and are immediate.
- Find is independent of filters: Trace scans visible records in chunks (20 000 default, smaller when decoding CANopen) with progress/cancel; Overwrite scans visible rows. Matches are a lazy array of slots/rows; changing mode preserves the query and rebuilds matches.

### D6 — Time: hardware record time, estimated UTC, explicit domains

The record stores `hardware_timestamp_ns` only. No per-record UTC is stored.

- UTC is derived through a per-channel **anchor** estimated in the capture thread from `EventHeader.ingest_timestamp_ns` and `TimeBaseClock::utc_from_canonical()`. The estimator keeps a short window of samples and fits an offset and drift (least-squares or minimum-latency offset); material jumps, source changes, or clock resets allocate a new `time_epoch`.
- The capture thread pushes `AnchorEvent`s before frames that reference them; the UI drains anchors first. The anchor table holds 256 entries per channel; an unknown/evicted epoch renders `UTC -` and sets `TimestampInvalid`/stale in the tooltip.
- `TimeBaseClock` is assumed available in normal operation. If it is absent, UTC is `-` and `Status` warns; HW time remains valid.
- UTC is displayed as `UTC (est.)` with a tooltip showing source, offset, drift, age, and epoch. No ISO timestamp is fabricated.
- Columns: `HW only`, `UTC only`, `Both`; per-view setting with a config default.
- Time modes: `Driver`, `Since session start` (per channel), `Relative T0`.
- Delta modes: `Previous visible`, `Per CAN ID`, `From T0`. All deltas use HW time and signed `int64_t` arithmetic. Cross-channel `Previous`/`T0` are `-` unless `assume_shared_hardware_timebase` is true; `Per ID` is always valid because the key includes the channel.
- T0 is bound to a channel; a T0 on another channel is not comparable unless the shared flag is on.
- Changing `timestamp_source` (`Hardware`/`DriverFallback`/`Simulated`) starts a new time epoch and produces a mixed-source warning.

### D7 — Overwrite: key-based state, bounded LRU, key-based selection

`Overwrite` rows are keyed by `(channel_id, can_id, extended, kind)`. Each row stores a copy of the latest 126 B record, `last_seq`, lifetime `count`, first/last seen, a 10-bucket 1 s rate window, a 32-period delta window, and 64 byte-fade timestamps.

- Row table: open-addressing hash + LRU list; capacity default **4 096**, configurable to 16 384. Eviction increments `overwrite_evictions`; an evicted selected key is removed from selection with a toast.
- `Count` is lifetime; `Msg/s` is sliding 1 s; `Δt Cur` is the latest signed delta; min/max/avg over 32 valid periods; `Jitter = max - min` (peak-to-peak, documented in the tooltip).
- Negative deltas are shown in `Cur` but excluded from period statistics. Gaps/resets clear the affected row's period window (per channel for `TransportGap`, all rows for `IngressOverflow`); the next delta is marked anomalous and excluded.
- Error/Overload rows show `Count`/`Msg/s` but `Δt -`; BusIdle and synthetic gaps are not rows. A `Next gap` action switches to Trace.
- Byte changes compare `data[0..dlc-1]`; fade uses UI monotonic time, default 1 500 ms; timers beyond `dlc` are cleared.
- Selection is **key-based**: click selects a key, Shift+click a visible range, Ctrl+click toggles, Ctrl+A selects visible rows. `Save selected` exports the latest record per selected key. `Export all frames for selected keys` is a separate action.
- `F2` bookmarks the latest record of the focused row; an older bookmarked record makes the row show a hollow bookmark. `F3` to an older bookmark switches to Trace. `Find` searches visible rows only. `F4` cycles error/invalid/anomalous rows; gaps use `Next gap`.

### D8 — Heatmap and bit activity are derived, bounded views

No second long-term history exists.

- `HeatmapWindow` caches at most 2 048 columns for the visible time range: per channel `bit_time_ns`, `idle_ns`, error/overload counts; per ID bin (9 bins, selected channel) `bit_time_ns`.
- Frame load is a bit-time estimate: classic/FD nominal/data phases, BRS splitting, CRC 17/21, and a 1.15 stuffing factor. Missing bitrate -> `n/a`.
- BusIdle measured load is `1 - idle_ns / bucket_ns`, distributed over the interval ending at the BusIdle timestamp. Measured and estimated cells are visually distinct; the 1 s heartbeat limits measured resolution.
- ID-bin mode is per selected channel; aggregating bus load across channels is rejected as physically meaningless.
- Axis switch: `UTC (est.)`, `HW lanes`, `Shared HW`; default follows `assume_shared_hardware_timebase` and the bucket size. At 1 ms and no shared clock, HW lanes are used with a warning.
- Rebuild on zoom/pan/axis/step uses ring scans in 20 000-record chunks; live updates the rightmost bucket incrementally. Click pauses the focused view and scrolls Trace to the first record in the bucket.
- `BitActivity` keeps the last 4 096 records or 10 s (whichever is shorter) for the selected `(channel, id, extended)` and computes per-bit 0->1/1->0 transitions; matrix is 64x8 for FD, 8x8 for classic; ECO updates at most every 250 ms.

### D9 — CANopen: shared EDS dictionaries, passive remapping, bounded state

- `EdsDictionary` is immutable after parse, shared by all bindings of one file; cache key is canonical path + size + mtime + hash. Limits: 32 files, 8 MiB/file, 65 536 objects, 128 B names, 32 MiB default EDS budget.
- `CanOpenRegistry` has 128 `(channel_id, node_id)` slots with alias, colour, NMT state, heartbeat timeout, PDO mapping snapshots, and SDO assemblers. Node IDs are 1..127; CANopen is 11-bit only.
- Per-channel COB-ID route table is 2 048 entries for NMT (`0x000`), SYNC (`0x080`), TIME (`0x100`), EMCY (`0x080+node`, node > 0), SDO (`0x580/0x600+node`), TPDO1..4 (`0x180/0x280/0x380/0x480+node`), RPDO1..4 (`0x200/0x300/0x400/0x500+node`), heartbeat (`0x700+node`), plus custom COB-IDs from `[1400..15FF]`/`[1800..19FF]`. Conflicts keep the first route and warn.
- NMT: commands 1/2/0x80/0x81/0x82 with target node or 0 broadcast; heartbeat/boot-up states; stale after `3 x` heartbeat time (from `[1017]`) or 3 s.
- EMCY: 16-bit error code, error register, manufacturer data; text mapping for common codes.
- SDO: expedited and segmented; 4 KiB assembly per node/direction; 1 s timeout; abort-code table; block transfer unsupported. Mapping/COB-ID writes are applied only after a successful SDO response.
- PDO: up to 8 mapping entries; bit unpack LSB-first; signed/unsigned/REAL/string; unknown object shown by index/sub; mapping outside `dlc*8` is partial/error.
- Dynamic remap: `co_map_epoch` per record (1 B) selects an immutable mapping snapshot. The node keeps the last 8-16 snapshots; an evicted epoch is `MappingStale` and decoded with the nearest snapshot plus a warning. Dynamic COB-ID changes update routing for future records only; history is not reindexed.
- EDS binding add/remove triggers a ring reindex in 20 000-record chunks (`co_node_index`), overwrite refresh, and filter refresh. Dynamic mapping changes do not reindex history.
- Offline CANopen uses the exported `co_decoded` text as authoritative fallback and re-decodes from EDS when the binding/hash is available.

### D10 — Canonical session JSONL v1, legacy import

First line is a `session` header with `format: "nexigen.canview.session"`, `version: 1`, boot token, timebase info, channel profiles, EDS bindings, and counters. Subsequent records are typed `frame`, `gap`, or `status`.

- Frame fields include `seq`, `hw_ts_ns`, optional `utc_est_ns`/`timestamp`/`utc_estimated`/`utc_source`, `delta_ns`, `delta_id_ns`, `channel`, `channel_name`, `can_id`, `extended`, `kind`, `origin`, `ts_source`, `hw_seq`, `dlc`, `brs`, `esi`, `error_flags`, `frame_flags`, `idle_ns`, `data` (hex), `co_node`, `co_service`, `co_map_epoch`, `co_decoded`.
- Gap fields include `kind`, `reason`, `lost`, `count_exact`, reference time. Status records carry `CanChannelStatus` plus channel.
- Import accepts v1 sessions and legacy `nexigen-canmon --json` lines; missing channels/bitrates create synthetic descriptors; imported records receive new local `seq` numbers; malformed lines are counted and skipped; a partial last line waits.
- Export always writes v1. `Save selected` exports selected records (Trace) or latest per selected key (Overwrite); a separate action exports all frames for selected keys. One global writer, 4 096-slot SPSC queue, nice 19, progress/cancel, `.partial` + rename on success, delete on cancel.
- CSV/TXT are export-only tabular formats; they do not carry a session header.

### D11 — Memory ledger and degraded mode

`max_memory_mb` covers preallocated viewer state; EDS has a separate `eds_budget_mb` (default 32 MiB).

Worst-case ledger:

```text
fixed: ingress 8 MiB
     + PAUSED 18 MiB
     + overwrite 4 MiB (4096 rows)
     + heatmap/bit activity 4 MiB
     + export queue 2 MiB
     + UI/model 6 MiB
     + anchors 1 MiB
     = 43 MiB

per slot: ring 126 B
        + visible index 4 B x 8 views = 32 B
        + selection/bookmarks 2 bits x 8 views = 2 B
        + find (one focused view) 4 B
        = 164 B/slot worst case
```

`capacity = bit_floor(max(1024, (max_memory_mb - 43 MiB - 8 MiB) / 164))` gives 1 048 576 at 256 MiB and 2 097 152 at 384 MiB. The 16 MiB configuration runs degraded: ring 1 024, one PAUSED snapshot or none, reduced overwrite rows, minimal heatmap, EDS disabled, raw-only find.

Allocation failures fail the affected feature (ring -> exit before capture; view index -> reject view; snapshot -> reject clone; overwrite table -> disable Overwrite; EDS over budget -> reject load).

### D12 — Resources, ECO, and threads

- `ResourceGuard`: ECO after CPU > 50% for three 1 s windows or rate > 50 000 frames/s; exit after two windows below 70%; ECO runs 15 FPS and recomputes filters/heatmap/bit activity no more than every 250 ms. User input wakes the UI.
- Threads: `canview-rx` nice 10, `canview-ex` nice 19, UI normal. Optional affinity is validated at startup; invalid spec is a startup error.
- The UI drain budget is 2-3 ms/frame at the design rate. Filter rebuild is capped at 20 000 records/frame; find scans are chunked; export enqueues in bounded batches.
- Hot paths never allocate, format to `std::string`, or block. Visible-row formatting uses fixed stack buffers; CANopen decode uses a fixed `CoDecoded` struct; export/import/EDS/config use cold-path allocation.

### D13 — UI layout and interaction contract

Global panels: Toolbar, Bus Load Heatmap, Channels & CANopen, Minimap (focused view), Inspector + Status. Center: `Can: <title>` dock windows.

- Trace columns: bookmark, `#`, `Time (HW)`, optional `Time (UTC est.)`, `Delta`, `Ch`, `Node/Service`, `ID`, `Kind`, `Flags`, `Len`, `Data/Decoded`; find highlight and tooltips.
- Overwrite adds `Count`, `Msg/s`, `Δt Cur/Min/Max/Avg/Jitter`; stable key order with optional sort.
- Left panel channels/nodes show status/NMT/colour and `Open` actions; checkboxes edit the focused view's filter. `Configure channels` dialog starts capture; `Restart capture` recovers failed channels.
- Hotkeys: `Space`, `M`, `F2`, `F3/Shift+F3`, `F4/Shift+F4`, `T`, `G`, `Ctrl+Home/End`, `Ctrl+A`, `Ctrl+F`, `Ctrl+C`, `Enter` in find. Text input suppresses them.
- Docking persists in the ImGui ini; the JSON config stores channels, views, time defaults, budgets, resources, overwrite, heatmap, colours, EDS bindings, and UI scale. Config writes are atomic; a corrupted file leaves defaults and is not rewritten.
- Empty config: GUI opens an empty workspace with `Configure channels`; capture stays stopped. Headless without offline/mock exits 2.
- Splash is 1 s, embedded, skipped in headless/scripted modes; UI scale is 0.5..4.0.

### D14 — Lifecycle and failure policy

Startup: parse CLI/config -> allocate ring/queues/model -> attach timebase -> load EDS -> start capture -> GUI/headless loop. Shutdown: set stop flag -> join capture -> cancel/await export -> save config -> exit.

- SIGINT/SIGTERM/SIGHUP and window close are graceful; exit 0. Invalid args 2; runtime failure 1.
- The capture WaitSet has disabled signal handling; a 50 ms wait timeout checks the stop flag.
- A failed channel does not abort the others; if none start, capture stays stopped and `Restart capture` is available.
- Export cancel removes `.partial` and reports; a second export is rejected. Import cancel keeps already imported records with a banner.
- Transport errors are reported and do not destroy history; missing publishers are a waiting state, not an error.

### D15 — Build and test seams

- Hoist ImGui/GLFW into top-level `nexigen_imgui`/`nexigen_glfw` targets; add pinned `nexigen_implot`. `nexigen-logview` links the hoisted targets without behaviour change.
- `nexigen_canview_model` and headless tests do not link OpenGL/GLFW; only the app GUI target links ImGui/ImPlot/GLFW/PNG.
- Tests: model unit tests, transport test with virtual CAN, offline JSONL/legacy tests, EDS/SDO/PDO fixtures, soak at 50 000 frames/s, and scripted UI under Xvfb (`--ui-smoke`, actions, input, views, focus, splash, scale).
- Debug, Release, TSan, and ASan configurations must pass. Doxygen comments and Mermaid updates in `docs/architecture.md` are part of the tasks.

## Risks / Trade-offs

- **Code duplication with logview** -> keep files layered and domain types out of future-common interfaces; extract `common/monview` only after this tool stabilizes.
- **16-consumer limit** -> 8 live channels exactly fill `EventConsumerSet<16>`; the live limit is a documented configuration error above 8. Offline is not affected.
- **ImPlot revision may not match the pinned ImGui docking commit** -> select and pin a compatible ImPlot revision as the first build task; if incompatible, keep the model/headless path and gate GUI behind a build check.
- **UTC is an estimate** -> label `UTC (est.)`, expose offset/drift/age, support a manual offset, and never fabricate an ISO timestamp when the anchor is invalid.
- **Dynamic PDO remap history** -> `co_map_epoch` + bounded snapshots preserve historical decoding; an evicted snapshot is marked stale instead of silently using the wrong schema.
- **EDS dictionaries can exceed memory** -> separate budget with hard rejection; existing bindings remain usable.
- **Heatmap measured resolution** -> BusIdle heartbeat limits measured load to ~1 s; estimated frame load is shown separately rather than pretending 1 ms precision.
- **Worst-case index memory** -> the 164 B/slot ledger reserves eight full indexes; a 16 MiB budget runs a documented degraded mode.
- **Scripted UI tests can be flaky** -> deterministic fixtures, short timeouts, and headless model coverage for every behaviour that does not require rendering.
- **CANopen parser scope** -> parse only the object dictionary, communication and mapping objects needed for display; reject oversized/malformed files with line diagnostics.
- **Remote-frame validation mismatch** -> use viewer-local `validate_for_view()`; do not change the transport contract in this change.

## Migration Plan

1. Land the build hoist and pinned ImPlot target with `NEXIGEN_BUILD_CANVIEW=OFF`; verify `nexigen-logview` is unchanged.
2. Land the model, capture, session, and headless paths behind the new build option.
3. Land CANopen and heatmap.
4. Land the GUI, scripted tests, and acceptance matrix.
5. Enable the option in CI; keep the existing `nexigen-canmon` tests untouched.

Rollback is the build option: disabling `NEXIGEN_BUILD_CANVIEW` removes the new tool while leaving all platform libraries and the CLI intact. No data migration is required; the JSONL v1 format is additive and legacy CLI files remain importable.

## Open Questions

- The exact ImPlot commit compatible with the pinned ImGui docking revision is a build-time selection, not a spec/approach decision.
- EDS fixture provenance (hand-written minimal EDS vs a vendor sample) can be decided while writing tests; it does not change the parser contract.
