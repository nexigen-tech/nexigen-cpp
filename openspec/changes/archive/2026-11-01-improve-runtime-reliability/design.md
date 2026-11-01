# Design

## Context

See `proposal.md` for motivation. Two current mechanics shape the approach:

- The runner starts detached supervisors with `fork()` and terminates them with
  `_exit()`. `_exit()` skips libgcov's exit-time dump, so every line executed in
  a supervisor is invisible to `coverage_report.py`. The same applies to the
  application child between its second `fork()` and `execvp()`.
- The hub has no logger and no error channel. `Http_server::Impl::timers()`
  discards `Discovery::rescan()` and `Stream_manager::sync()` is `void`, so
  tailer open failures cannot be observed by callers.

All existing contracts remain in force: C++23, `-fno-exceptions`, `-fno-rtti`,
`core::Status`/`Result`, Linux syscalls only, and no new dependency.

## Goals / Non-Goals

**Goals:**

- Make coverage of forked control-plane processes measurable without changing
  non-coverage builds.
- Give the hub a testable diagnostic channel for refresh failures.
- Make `Last-Event-ID` and `Connection` header handling RFC-correct and cover
  malformed requests.
- Pin the JSON dependency and offer a portable Release build.
- Add enough runner/hub tests to hold both modules at or above 90%.

**Non-Goals:**

- No new logging subsystem for the hub, no log file rotation, and no change to
  the `runner.jsonl` schema beyond using the existing warning level.
- No HTTP/1.1 feature work such as chunked bodies, TLS, authentication, or
  request pipelining beyond the existing keep-alive loop.
- No change to hot-path modules, wire formats, or public headers other than the
  documented constructor/setter additions.

## Decisions

### D1: Flush gcov counters explicitly in forked children

Add `src/runner/coverage_flush.hpp` with an inline `coverage_flush()` that is a
no-op unless `NEXIGEN_ENABLE_COVERAGE` is defined, in which case it calls
`__gcov_dump()`. The coverage branch in `CMakeLists.txt` adds
`NEXIGEN_ENABLE_COVERAGE=1` to `nexigen_options`.

Call the helper immediately before every `_exit()` in a runner child and before
`execvp()` in the application child, plus before the suite supervisor's
`_exit()`. A small `child_exit(int)` wrapper keeps the call sites consistent.

Alternatives considered:

- Replace `_exit()` with `std::exit()`: rejected. The child inherits the
  parent's C++ runtime locks after `fork()`, so running static destructors is
  exactly what `_exit()` avoids.
- Use `GCOV_PREFIX` per child: rejected. It changes file placement, not the
  missing dump.
- Leave child coverage unmeasured: rejected. It keeps the 90% gate
  unreachable and hides real branches.

Risk: several children merging into the same `.gcda` concurrently. libgcov
merges counters with file locking; for line coverage only the `> 0` property
matters, so merged counts are safe. Coverage builds are single-purpose and not
used for latency measurement.

### D2: Per-module coverage gates in the report tool

Extend `cmake/coverage_report.py` with repeatable
`--module-fail-under <module>=<percent>` options. After printing the table, the
tool compares each requested module and returns non-zero on the first
violation. The `coverage` target passes `runner=90` and `hub=90`; the CI
coverage job keeps an overall `--fail-under` gate at the achieved floor.

Alternatives considered: hard-code thresholds in Python (rejected: hides policy
in code) and enforce only in CI (rejected: local `coverage` runs would not
catch regressions).

### D3: Function-pointer diagnostic sink

Use a plain function pointer plus opaque context, not `std::function`:

```cpp
using Diagnostic_sink = void (*)(void* context, std::string_view message) noexcept;
```

`Http_server` and `Stream_manager` expose a defaulted constructor parameter and
a setter for the sink. This keeps the call zero-allocation, avoids `<functional>`
on a control path, and remains testable with a static capture buffer. Messages
are preformatted JSON-ish text such as
`event=stream_open_failed stream=<id> path=<path> errno=<n>`.

`Http_server::timers()` reports a failed `Discovery::rescan()` and then still
calls `sync()`; it reports the `sync()` result if non-successful. A failed
refresh never closes the listener or cancels other timers.

Alternatives considered: adding `logging::FileLogger` to the hub (rejected:
pulls a worker thread and a dependency into a read-only observer) and writing
directly to `stderr` from the library (rejected: not testable and not
embeddable).

### D4: Status-returning stream reconciliation

`Stream_manager::sync` changes from `void` to `core::Status`, returning
`SystemError` after the loop if at least one tailer failed to open or refresh.
`open_tailer` reports the individual stream/path/errno through the sink and
returns a boolean; `sync` aggregates the first failure. Existing callers that
ignore the result still compile; the hub uses it and the tests assert it.

`open_tailer`/`poll_tailer` also report `stat`/`read` failures through the same
sink without changing their recovery behavior.

### D5: Header parsing helper

Replace substring searches with a `header_value(request, name)` helper that:

1. Skips the request line.
2. Splits header lines on `\r\n`, stopping at the empty line.
3. Splits each line at the first `:`, trims spaces and tabs on both sides.
4. Compares names with an ASCII case-insensitive comparison.

`Last-Event-ID` and `Connection` both use it. The cursor parser accepts optional
whitespace, rejects non-digits by resetting to zero, and ignores an empty value.
This removes accidental matches such as `X-Last-Event-ID`.

### D6: One warning at the state-write helper

`write_json_file` in `src/runner/instance_lifecycle.cpp` is the single choke
point for runner JSON state writes. On failure it calls
`warn_runner("event=json_write_failed path=... ")` before returning the error.
This covers instance `exit.json`, suite `suite-state.json`, suite
`suite-exit.json`, and the forced-stop writes without duplicating logging at
six call sites. Callers keep their existing error propagation.

`warn_runner` writes a warning record when runner logging is active and one
`nexigen-run:` stderr line otherwise, matching `specs/runner/logging`.

### D7: Build pinning and portable mode

Add `URL_HASH SHA256=4b92eb0c06d10683f7447ce9406cb97cd4b453be18d7279320f7b2f025c10187`
to the existing `v3.12.0` FetchContent declaration.

Add `option(NEXIGEN_NATIVE_ARCH "Use -march=native/-mtune=native in Release" ON)`
and pass it into `nexigen_set_optimizations`. The function emits a generator
expression that always adds `-O3`, `-fno-signed-zeros`, `-fno-trapping-math`,
and `-fomit-frame-pointer`, and adds `-march=native -mtune=native` only when
the option is `ON`. LTO remains governed by `check_ipo_supported`.

## Risks / Trade-offs

- **Duplicate gcov writes** from concurrent children can interleave. → libgcov
  uses file locks and merges counters; only hit/non-hit matters.
- **A diagnostic sink callback could allocate or throw.** → callbacks are
  `noexcept` and expected to be `fprintf`-only; tests use a static buffer.
- **Changing `sync()` to return `core::Status` touches all call sites.** →
  default-compatible return type; ignored results are still valid and tests
  cover the failure result.
- **Reporting a write failure at the helper can duplicate diagnostics when a
  caller also logs.** → call sites do not log the same failure; only the helper
  does.
- **Portable mode may hide a performance regression** because no CI job
  benchmarks it. → the Release latency job keeps `NEXIGEN_NATIVE_ARCH=ON`; a
  separate portable build only runs correctness tests.

## Migration Plan

No migration is required for consumers. Existing builds keep native Release,
existing construction of `Http_server`/`Stream_manager` keeps working through
default arguments, and no file format changes. Rollback is a revert of the
change; generated state files remain compatible.

## Open Questions

None.
