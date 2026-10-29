# Proposal

## Why

Drop information in `nexigen::logging` can be silently lost in five scenarios: (1) pending drop counts are not flushed as a summary at shutdown/flush, (2) the drop summary is only emitted on throttle-window transitions — when throttle is disabled, `buffer_full` drops never reach the log, (3) `slot_exhausted` drops are counted but never reported as an in-stream record, (4) `SpamProtection` `throttled_count_` is tracked but never surfaced in the log, (5) if the summary's own `try_push` fails, the summary is lost until the next window. The user requires that drop information is **never** silently lost.

## What Changes

- **Flush-time drop summary**: `flush()` and logger destruction SHALL push a final drop summary record reporting all pending per-cause counts before the drain completes, so no drop count is lost at shutdown.
- **Always-on drop summary**: the drop summary SHALL be emitted regardless of whether the throttle is enabled — any pending `buffer_full`, `rate_limited`, `slot_exhausted`, or `throttled_count_` triggers a summary on the next record or at flush.
- **`slot_exhausted` in summaries**: the drop summary SHALL include `unregistered_thread_drops_` alongside `rate_limited` and `buffer_full`.
- **Spam-throttled in summaries**: the drop summary SHALL include `throttled_count_` from `SpamProtection`.
- **Summary retry safety**: if the summary `try_push` fails, the counters SHALL be preserved (not reset) and the summary retried at the next opportunity (next record, next window, or flush).

## Capabilities

### New Capabilities

None.

### Modified Capabilities

- `logging/multi-producer`: add flush-time drop summary, always-on drop summary, `slot_exhausted` and `throttled` in summary, summary retry safety.

## Impact

- **Headers**: `include/nexigen/logging/detail/producer.hpp` — `emit_summary_if_needed` extended; `flush()`/destructor path calls summary push.
- **Tests**: `tests/logging/logging_test.cpp` — new scenarios for flush-time summary, always-on summary, `slot_exhausted`/`throttled` in summary, retry safety.
- **Performance / latency / allocations**: zero hot-path impact. Summary push is a cold-path bounded `try_push` on window transition or flush; no new atomics on the steady-state path.
- **Dependencies**: none new.
