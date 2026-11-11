# Proposal

## Why

The repository-wide reformat (`838d1b0`) split long string literals into adjacent fragments. In `tests/metrics/prometheus_render_test.cpp` that turned six single searchable expectations into pairs of fragments:

```cpp
NX_CHECK(contains(text, "request_ms_bucket{app=\"LIDR\",...,\"pid="
                        "\"42\",le=\"1\"} 1\n"));
```

The value is unchanged, but the literal can no longer be found with one grep, and the tests read worse than before. The formatter did this because `BreakStringLiterals` defaults to `true` in the LLVM base profile.

## What Changes

- Set `BreakStringLiterals: false` in `.clang-format` with a comment explaining the reason: expected JSON/Prometheus payloads and diagnostic messages must stay greppable as one string.
- Rejoin the six fragment pairs in `tests/metrics/prometheus_render_test.cpp` that the reformat introduced. Those lines now exceed the 100-column limit, which the profile allows for a literal.
- Leave the 133 pre-existing adjacent literals elsewhere untouched — they are deliberate line-wrapped messages (`printf("… " "…")`), not formatter artefacts.

## Capabilities

No capability deltas: formatting policy and test source layout only, no behavior change (`.openspec.yaml` sets `skip_specs: true`).

## Impact

- **Code:** `.clang-format` and one test file. No production source changes.
- **Behavior:** none. The concatenation of all string literals in the test file is byte-identical before and after (verified programmatically); the test suite passes unchanged.
- **Formatting:** future long literals stay on one line; `ColumnLimit` continues to govern everything else.
- **Performance / latency / memory:** none.
- **Risk:** long lines may exceed 100 columns — intentional and limited to literal-bearing lines; the formatter still checks every other aspect of them.
