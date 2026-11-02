# Design

## Context

`StateStorage` copies a NUL-terminated state value out of its cell/shards into a caller buffer via
`take`/`observe`, which forward to `copy` (single-threaded) or `copy_under_seqlock`
(multi-threaded). All callers pass a `char[max_state_length + 1]` buffer (a `thread_local` buffer,
a local buffer, or `MetricRecord::state.value`). The helpers take `char* dst`, and the terminal
`dst[len] = '\0'` write trips GCC 15.2 `-Wstringop-overflow` at `-O3`/`-O2` because the pointer
carries no size.

## Goals / Non-Goals

**Goals:**

- Make `nexigen_metrics_tests` compile under Release and TSan without suppressing a useful warning.

**Non-Goals:**

- No behavior change; no suppression pragma; no change to the seqlock protocol or the value length
  limit.

## Decisions

### D1 — Fix the off-by-one and make the destination size explicit

Two changes together:

- Bound the single-threaded copy loop with `len < max_state_length` instead of
  `len <= max_state_length`, so the trailing `dst[len] = '\0'` can never write past
  `dst[max_state_length]` (the buffer holds `max_state_length + 1` bytes). This was a real
  off-by-one, not merely a false positive: if the stored value were not NUL-terminated, `len`
  could reach `max_state_length + 1` and write out of bounds.
- Change the copy helpers' destination from `char*` to `char (&dst)[max_state_length + 1]`, so the
  compiler knows the buffer size and its bounds analysis succeeds without a suppression.

- *Rationale:* the actual defect is the loop bound; the reference type makes the contract explicit
  and keeps `-Wstringop-overflow` enabled rather than suppressing a useful check.
- *Alternative considered:* `#pragma GCC diagnostic ignored "-Wstringop-overflow"`. Rejected: it
  would hide this real off-by-one and any future one.

## Risks / Trade-offs

- [Signature churn] Six internal helpers change type, but they are `detail`-internal and only used
  by `StateGauge` in the same header. → Mitigation: the callers already pass matching fixed-size
  arrays, so no caller changes are needed beyond compilation.

## Migration Plan

Not applicable — internal refactor with no persisted format or API change.

## Open Questions

None.
