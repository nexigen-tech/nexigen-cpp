# Proposal

## Why

`nexigen_metrics_tests` fails to compile in Release and ThreadSanitizer builds with
`-Werror=stringop-overflow` in `StateStorage::copy` (`dst[len] = '\0'`). The destination is a raw
`char*`, so GCC 15.2 cannot prove the buffer is `max_state_length + 1` bytes and emits a false
positive that is promoted to an error. Debug and ASan build fine; only the optimizing builds trip
it.

## What Changes

- Change the destination parameters of `StateStorage`'s copy helpers (`copy`, `take`, `observe`,
  and the multi-threaded `copy_under_seqlock`) from `char*` to a fixed-size reference
  `char (&dst)[max_state_length + 1]`, so the compiler knows the buffer size and the warning is
  satisfied by construction.

## Capabilities

Pure build/refactor with no observable behavior change, so it declares no capabilities and sets
`skip_specs: true`.

## Impact

- `include/nexigen/metrics/state_gauge.hpp` only. No API, serialization, or data-model change.
- Performance, latency, and memory allocation: none — identical copy semantics, only the parameter
  type changes so GCC's bounds analysis succeeds.
