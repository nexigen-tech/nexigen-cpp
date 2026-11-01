# Proposal

## Why

`parse_u64` in `tools/nexigen-hub/main.cpp` and `parse_uint` in `tools/nexigen-run/main.cpp` are
the same decimal-to-`uint64_t` parser (identical loop and overflow guard, different names). The
tools have no shared header, so the logic is copy-pasted. A shared helper removes the
duplication and gives future tools one place to put CLI parsers.

## What Changes

- Add a header-only `tools/detail/cli.hpp` exposing `nexigen::tools::parse_u64`.
- Replace the local parsers in `nexigen-run` and `nexigen-hub` with the shared helper.
- Add the `tools/` directory to both tools' include paths.

## Capabilities

This is a pure refactor with no observable behavior change, so it declares no capabilities and
sets `skip_specs: true`.

## Impact

- New `tools/detail/cli.hpp`; edits to `tools/nexigen-run/main.cpp`, `tools/nexigen-hub/main.cpp`,
  and `tools/CMakeLists.txt`. No library, API, or serialization change.
- Performance, latency, and memory allocation: none — identical parsing logic, now shared.
