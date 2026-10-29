# Design

## Context

See `proposal.md` — Why. The current `emit_summary_if_needed` in `producer.hpp` only fires on throttle-window transitions and only when `policy_.enabled` is true. `slot_exhausted` and `throttled_count_` are never included. No summary is pushed at flush or destruction. If the summary `try_push` fails, counters are preserved but the summary is lost until the next window.

## Goals / Non-Goals

**Goals:**
- Zero drop information loss: every drop cause reaches the log as a summary record or is visible via `dropped()`.
- Flush-time summary: `flush()` and destructor push a final summary before drain completes.
- Always-on summary: works regardless of throttle state.
- All four causes in one summary: `rate_limited`, `buffer_full`, `slot_exhausted`, `throttled_count_`.
- Retry safety: failed summary push preserves counters.

**Non-Goals:**
- Changing the hot-path latency budget.
- Per-record drop notifications (too expensive).
- Changing `Dropped_stats` structure (already has all causes).

## Decisions

### D1 — Extend `emit_summary_if_needed` to all causes

Add `unregistered_thread_drops_` and `throttled_count_` to the summary text. The summary format becomes:
`"Dropped %llu record(s) in the last window (rate-limited: %llu, buffer-full: %llu, slot-exhausted: %llu, throttled: %llu)."`

### D2 — Always-on summary trigger

Call `emit_summary_if_needed` on every record's window transition **and** unconditionally check pending drops before `try_push` (one relaxed load of the counters — negligible cost). This ensures `buffer_full` drops are reported even without throttle.

### D3 — Flush-time summary

In `flush()` (control-plane path), before publishing the drain barrier, iterate all shards and push a summary for any with pending drops. This is cold-path only (flush is already a barrier).

### D4 — Retry safety

Current code already preserves counters on failed `try_push`. Extend: after a failed summary push, set a `summary_pending_` flag on the shard so the next `emit()` retries the summary before pushing the user record.

## Risks / Trade-offs

- [Summary push on every record when drops are pending] → bounded: one extra `try_push` per record until summary succeeds; counters reset only on success.
- [Flush-time iteration over all shards] → cold path, O(MaxThreads), acceptable.
- [Summary text length] → 192-byte buffer, truncated if needed (flagged).

## Migration Plan

Additive: extend `emit_summary_if_needed`, add flush-time call, add `summary_pending_` flag. No API changes. Rollback: revert `producer.hpp` changes.

## Open Questions

None.
