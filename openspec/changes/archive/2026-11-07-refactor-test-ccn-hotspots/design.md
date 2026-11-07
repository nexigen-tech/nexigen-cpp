# Design

## Context

See `proposal.md` — Why. All 14 functions are test-suite bodies that grew by
accretion: each packs many logically independent scenario blocks into one
function, and lizard counts every `if`/`loop`/`?:`/`&&`/`||` across all of
them. Each test TU already has an anonymous namespace holding its harness
(`NX_CHECK`, `run_suite`, allocation counters, fixtures); the decomposition
reuses that namespace.

Constraints that matter here: every test TU overrides global `operator new`
with a static bump arena and asserts allocation freedom over measured windows,
so the decomposition must not introduce allocations; metrics/tracing suites
observe thread-slot and end-order semantics, so statement order must be
preserved exactly.

## Goals / Non-Goals

**Goals:**

- Bring the 14 test functions to CCN ≤ 15.
- Preserve every assertion, its order, every `std::printf`, and every
  side effect byte-for-byte.
- Keep `main()` suite registration unchanged.

**Non-Goals:**

- No production code changes, no new test cases, no removed test cases.
- No changes to latency methodology (same loops, same rdtsc pairs, same
  prints, same gates).
- No harness changes (`NX_CHECK`, `run_suite`, arena counters).

## Decisions

### 1. Decomposition pattern (all files)

Split the body into `static`-linkage helper functions inside the TU's existing
anonymous namespace, named after the parent suite with a descriptive suffix
(e.g. `test_sinks_console`, `test_sinks_jsonl_basic`), called from the test
function in the original order. Code moves verbatim: only the function
signature and the scope boundaries change. Helpers are defined above their
first use (or forward-declared), so no ordering surprises.

### 2. Shared state

Locals that cross a block boundary (fixtures, ports, crafted-record makers,
UTC window helpers, sink/provider pointers, base counters) are hoisted to the
parent function and passed to helpers as parameters (by value for scalars,
by reference for mutable state). Where a value is recomputed identically in
several blocks (e.g. the tracing suite's mid-window UTC instant), a tiny
file-local helper replaces the repeated lambda so the arithmetic stays
identical. Function-local lambdas that already factor request plumbing (the
hub suite's `request`/`raw`/`sse_with_header`) become helpers taking the
fixture/port as a parameter.

### 3. Per-file plan

- `tests/tracing/tracing_test.cpp` (4 suites):
  - `test_sinks` → console-sink block, shared UTC-window helpers,
    JSONL basic/overflow/escape/rotation/rate-limit/open-failure blocks,
    config-diagnostics block, ChromeTraceSink block, CompositeTraceSink block.
  - `test_scoped_span` → the 16 numbered producer-thread scenarios as
    helpers taking `(tracer, provider, sink)`; the concurrency-flip section
    becomes its own helper.
  - `test_latency` → pin/placement setup, warmup+calibration, the measured
    pass loop, the report+gate block; non-x86 path stays a helper too.
  - `test_ids_and_context` → sentinel checks, uniqueness batch,
    allocation-free batch, concurrent generation, hex/JSON rendering,
    SPSC handoff, diagnostics block.
- `tests/app/app_test.cpp`:
  - `test_command_line_parser` → long options, short forms, level spellings,
    level rejections/aliases, argv[0] handling, help, terminator, unknown/
    missing/repeated/positional args, directory-shape validation,
    required-directory rule, usage-text check.
- `tests/hub/hub_test.cpp`:
  - `test_http_server` → socket helpers (`request`/`raw` become helpers),
    REST routes, malformed requests, method rejection, query/escape handling,
    keep-alive, SSE snapshot+live, tree topic, all-stream SSE,
    Last-Event-ID resume, case-insensitive headers, failure reporting,
    shutdown.
- `tests/metrics/metrics_test.cpp` (6 suites):
  - `test_exponential_histogram`, `test_explicit_histogram_mt`,
    `test_serialization`, `test_sinks_json`, `test_latency`,
    `test_provider_harvester` → split at the suites' own numbered/commented
    scenario blocks; thread-slot and end-order checks stay in the same
    relative order.
- `tests/chrono/chrono_test.cpp` (`test_provider`) and
  `tests/core/core_test.cpp` (`test_sink_io`) → split at their commented
  scenario blocks with the same pattern.

### 4. No ODR / linkage impact

All helpers live in the anonymous namespace of the TU that uses them, so no
external symbols are added and no other TU can see them.

## Risks / Trade-offs

- [A hoisted helper subtly reorders an assertion or drops one] → move text
  verbatim; compare `NX_CHECK` counts before/after per suite (`run_suite`
  prints per-suite check counts — the numbers must match the pre-refactor
  run exactly).
- [Shared state passed incorrectly changes a thread-slot/order invariant] →
  keep every statement in its original order across helpers; the metrics
  suites' printed thread-slot diagnostics are the canary.
- [Latency gates drift because measurement code changed] → the measurement
  loops move unmodified; only the enclosing scope changes.
- [A helper introduces an allocation and trips the arena counters] → helpers
  take references; no new containers or strings are created.

## Migration Plan

None. Land as one commit; rollback is a revert of the six test files.

## Open Questions

None.
