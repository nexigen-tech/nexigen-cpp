# Proposal

## Why

The `format-check` gate is red on committed `HEAD`: `cmake/RunClangFormat.cmake` reports 115 of 449 tracked C++ files (883 diagnostics, 1 925 lines) as not matching the project's own `.clang-format` under the pinned clang-format **22.1.8** — the same version the CI `format` job installs. The gate therefore cannot pass in CI and produces no signal, exactly like the two lint gates fixed in `fix-naming-and-result-value-gates`.

This is accumulated drift, not a version regression. Formatting the tree with clang-format 17.0.6, 18.1.8, 19.1.7, 20.1.8, 21.1.8 and 22.1.8 fails 126, 123, 117, 115, 115 and 115 files respectively — no released version reproduces the committed bytes, so pinning CI to a different version cannot fix it.

## What Changes

- Run the project's own `format` target (clang-format 22.1.8, `file:.clang-format`) across `include/`, `src/`, `tests/`, `examples/` and `tools/`, and land the result as one formatting-only commit.
- Accept the one place where the formatter rewrites a deliberately compressed construct instead of only whitespace: `tests/logging/log_daemon_test.cpp` defines its `NX_CHECK` harness on a single line, and formatting expands it with `\` continuations. Keep the expanded form (consistent with every other test translation unit and fully covered by the gate) rather than fencing it with `// clang-format off`.
- Record the formatting commit in `.git-blame-ignore-revs` and point the README Formatting section at it, so `git blame` stays useful.
- No configuration, version, tool or target change: `.clang-format`, `cmake/Format.cmake`, `RunClangFormat.cmake` and the CI pin stay exactly as they are.

## Capabilities

No capability deltas: the change is pure formatting with no observable behavior change (`.openspec.yaml` sets `skip_specs: true`).

## Impact

- **Code:** up to 115 files (73 public headers, 20 `src/`, 17 `tests/`, 4 `tools/`, 1 `examples/`). Layout only.
- **APIs / ABI / behavior:** none. No identifier, declaration, default value, include order (`SortIncludes: Never`, `IncludeBlocks: Preserve`) or preprocessor condition changes. The one non-whitespace case is the macro line continuations described above, which the preprocessor treats identically.
- **Performance / latency / memory:** none; no instruction, layout or allocation changes. Latency gates must reproduce their previous numbers.
- **Tooling:** `format-check` turns green and starts catching real drift in new code. `.git-blame-ignore-revs` is additive.
- **Risk:** a 1 925-line whitespace diff obscures review — mitigated by keeping it a single commit with no logic changes, and by an explicit `git diff -w` verification (empty for every file except the macro expansion).
