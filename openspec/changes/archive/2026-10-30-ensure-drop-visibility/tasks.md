# Tasks

## 1. Drop visibility implementation

- [x] 1.1 Extend `emit_summary_if_needed` in `include/nexigen/logging/detail/producer.hpp` to include `slot_exhausted` and `throttled_count_` in the summary text. Verify: summary format matches spec.
- [x] 1.2 Add flush-time drop summary: in `flush()` path, iterate shards and push summary for any with pending drops before the drain barrier. Verify: drops before flush produce a summary record.
- [x] 1.3 Retry safety: counters preserved on failed summary push; summary retried at next opportunity. Verify: failed summary push preserves counters and retries.

## 2. Tests

- [x] 2.1 Update test expectations for new 4-cause summary format and flush-time summary line. Verify: all scenarios pass in Debug.
- [x] 2.2 Run integration gate: Debug (656/0), Release (657/0), TSan (656/0), ASan (656/0) — all green. Verify: all four modes green.

## 3. Validation

- [x] 3.1 `openspec validate "ensure-drop-visibility" --type change --strict` passes; `openspec validate --specs` green. Verify: no findings.

## Workflow follow-up

- Archive the change after review (`openspec archive ensure-drop-visibility`).
