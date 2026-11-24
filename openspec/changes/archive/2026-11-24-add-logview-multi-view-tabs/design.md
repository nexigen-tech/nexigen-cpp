# Design

## Context

See `proposal.md` — Why. The current `LogViewModel` owns one `RecordRing`, one `FilterIndex` whose `visible[]` lives inside the ring, one `FindState`, one selection/bookmark bitset, one pause state, one `TimeContext`, and one export job. The UI draws a single `Log` window and a single `Sources & levels` panel. Multi-view support therefore requires separating **shared live data** from **per-view presentation state**; adding tabs only in the UI would still leave one filter index and one selection.

The existing history capacity is fixed at startup (`RecordRing`), and the viewer already satisfies the no-allocation-on-ingest rule. Live view indexes and frozen snapshots must be allocated at view creation, not during ingestion.

## Goals / Non-Goals

**Goals:**

- Multiple independent views over one live capture history, with full per-view presentation state.
- One default `All` view plus source-scoped views, presented as independent dock windows/tabs.
- LIVE and PAUSED clones with explicit copy checkboxes.
- A bounded, immutable paused snapshot that survives live eviction.
- Predictable memory: 8 LIVE views including `All`, 16 PAUSED views, 1 MiB snapshot records.
- All+LIVE view definitions restore; PAUSED is session-only.

**Non-Goals:**

- Multiple captures or multiple services in one process.
- Persisting PAUSED snapshots to disk.
- Per-view colors, time base, or UI scale; these remain shared.
- Changing the log wire format, service name, or export format.
- Replacing the existing file `Open` behavior; it continues to import into the shared live history.

## Decisions

### D1. Split `LogSession` from `LogView`

`LogViewModel` keeps its historical name and becomes the session role; the new
per-view type is `LogView`. A `LogSession` alias documents the intent without a
churn-heavy rename of every existing caller and test.

```text
LogSession / LogViewModel (shared, UI-thread owned)
  RecordRing live
  AppRegistry
  ColorManager
  TimeContext time base
  ResourceGuard
  std::vector<std::unique_ptr<LogView>> views
  std::optional<ViewId> focused_view
```

`LogView` owns all per-view state:

```text
ViewId id
ViewKind kind: All | Source
bool live: true | false (LIVE vs PAUSED)
source app id / empty for All
FilterCriteria + ViewIndex
FindState
Selection/Bookmark bitsets
Minimap
PauseState + scroll/auto-scroll
TimeContext (T0, mode, delta, threshold)
Export selection state
```

A source view fixes `AppRegistry` lookup to one app; `All` uses the normal per-app filter. `ViewKind` and `live` are orthogonal: an `All` view can be LIVE or PAUSED, and a source view can be LIVE or PAUSED.

### D2. Decouple the visible index from `RecordRing`

`FilterIndex` currently calls `RecordRing::append_visible()`/`visible_at()` and therefore cannot serve two views with different criteria. Replace that coupling with a `ViewIndex` owned by each view:

```text
ViewIndex
  uint64_t* visible   // one allocation, capacity = history capacity or snapshot count
  size_t count
  FilterCriteria criteria
  rebuild cursor/progress
```

`ViewIndex` scans a `HistorySource` interface (`head`, `tail`, `find(seq)`, `resident`), implemented by the live `RecordRing` and by `FrozenHistory`. The live ring's `visible[]` is removed from the ring layout; its 4/8 MiB moves to each view's index. Filter rebuild remains chunked at 20 000 records per frame per active rebuild, and all rebuilding views share a total per-frame budget.

`FindState` keeps its match vector, but allocation is lazy: no full-history `reserve()` on view creation; the vector grows only when a find is started and is released/cleared when the query is cleared.

### D3. Snapshot is a compact frozen history

`FrozenHistory` is compact immutable storage with the same record layout as the live ring minus eviction/visible arrays:

```text
frames[count]      256 B   alignas(64) LogFrame
kinds[count]         1 B
prev_or_gap[count]   8 B
head/tail/count
```

The record footprint used for the 1 MiB budget is `256 + 1 + 8 = 265 B` (plus allocation alignment). The count is `min(matching records, floor(1 MiB / 265))`. The copy scans the live ring newest-to-oldest, selects records of the view scope, then materialises them oldest-to-newest and preserves absolute `seq`. Gap records in the selected `seq` range are included and count toward the budget. `All` scope copies every source; a source view copies only that application.

Selection/bookmark bitsets are slot-indexed. For a snapshot, slot indexes are snapshot slots, not live-ring slots. Copied selection/bookmarks are clipped to records present in the snapshot. An out-of-snapshot T0 is cleared. A cloned find query is re-run through the new view index.

### D4. Clones are view-state copies, not history copies for LIVE

The clone dialog creates a `LogView` from the focused view:

- `LIVE`: attach to the shared `RecordRing`, start at `tail`, append future matching records; no new history storage.
- `PAUSED`: create a `FrozenHistory` snapshot at clone time. Cloning an existing PAUSED view in PAUSED mode shares the immutable `FrozenHistory` with `std::shared_ptr`; the new view still has independent filters, selection, bookmarks, T0, find, scroll, and export selection.
- Source is always inherited; scroll is never copied.
- Each checkbox controls one copied state group: levels, text, timestamp mode, delta mode, anomaly threshold, selection, bookmarks, T0, find query. All boxes default on.
- If a copied item is not applicable after snapshot clipping, the clone follows the clipping rule above and records the adjustment in its status.

### D5. Dock windows and focus

Each view is an ImGui window named `Log: <display>###logview-<stable id>`. The hidden `###` id prevents collisions when two views have the same source and makes the layout stable across restarts. New live views dock to the focused view's dock node when possible; otherwise they float and can be docked manually.

`focused_view` is updated when a Log window receives focus. Keyboard shortcuts (`Space`, `Ctrl+C`, `F2`, `F3`, `F4`, `Ctrl+F`) and the inspector act on `focused_view`; if no Log window is focused, they do nothing. Local buttons remain inside each window.

### D6. Limits and memory

- `max_live_views = 8`, including `All`.
- `max_paused_views = 16`.
- LIVE view index: `history_capacity * 8 B` (4 MiB at 524 288; 8 MiB at 1 048 576).
- Selection/bookmark bitsets per view: `capacity / 8 B` each.
- Minimap: 1024 buckets.
- PAUSED snapshot: at most 1 MiB of record data; visible index is `snapshot_count * 8 B`.
- Find matches: lazy allocation, no full reserve per view.

View creation validates the limit and allocates before publishing the window. Allocation failure or limit exhaustion leaves all existing views intact and returns a visible error. Closing a view frees its index/snapshot; a shared `FrozenHistory` is freed when the last paused view referencing it closes.

### D7. Persistence

`logview.json` gains a versioned `views` array for `All` and LIVE views:

```json
{
  "views": [
    {"id": 1, "title": "All logs", "source": "", "live": true, "levels": [...],
     "text": "", "timestamp_mode": "absolute", "delta_mode": "previous_visible",
     "anomaly_threshold_ns": 50000000, "hidden_apps": ["NoisyApp"]}
  ]
}
```

`hidden_apps` is populated only by an `All` view's source popup; source views
ignore it.

Only view definitions and filters/modes/thresholds persist. Selection, bookmarks, T0, find query, scroll, and PAUSED snapshots are session-only. If the array is absent or empty, create the default `All` view on first run; if the persisted array intentionally has no LIVE views, show the empty workspace and let the Sources catalogue reopen `All` or a source. ImGui `.ini` continues to own dock geometry and tab order.

`ViewConfig` is a public value type with `to_string()`, `std::formatter`, and non-throwing `to_json`/`from_json`; it is parsed as cold-path configuration only.

### D8. Export and shared actions

Each view owns its selection bitset and local `Save selected` action. One `ExportWriter` remains global. Starting an export snapshots the view's selected records into the existing queue; a second request while `exporting()` is true is rejected. Closing the originating view does not cancel an already-copied export.

`Clear history` remains global: it resets the live ring and all LIVE view indexes; PAUSED snapshots remain while their views are open. `Open file` continues to import into the shared live ring; PAUSED views are unaffected. Colors, time base, ECO, and UI scale remain global.

## Risks / Trade-offs

- **Eight live indexes at 1M history** → ~65 MiB of view indexes; documented limit and lazy find storage keep it bounded.
- **Filter rebuild cost across views** → only the active/required view rebuilds are scheduled; rebuild work is chunked and shared with UI work.
- **Dynamic window IDs and layout** → stable `###logview-<id>` IDs are persisted with view definitions; stale layout entries for removed views are harmless.
- **PAUSED snapshots are session-only** → explicit UX; no hidden persistence promise.
- **Focus-based keyboard routing** → the last focused view is tracked; no focused Log window means shortcuts are ignored rather than guessing.
- **All-source paused snapshot** → copies the newest 1 MiB across all sources; older source slices are intentionally absent and the window shows the snapshot range.

## Migration Plan

1. Existing installations have no `views` array: create the default `All` view and preserve their current filter/time behavior.
2. The old single `Log` window is replaced by `Log: All logs`; ImGui layout may place it as a new window on first run, after which the layout persists.
3. PAUSED clones are only created by an explicit clone action; no existing behavior creates one automatically.
4. Rollback: the new view definitions are additive; removing them restores the single-All behavior. No service, wire, or on-disk export state changes.

## Open Questions

None that change the specified behavior. The exact default dock placement of the first `All` window may be revisited during implementation without changing the view contract.
