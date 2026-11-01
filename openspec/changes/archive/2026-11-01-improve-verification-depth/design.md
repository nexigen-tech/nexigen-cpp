# Design

## Context

See `proposal.md` — Why. P4 already established parser-level deterministic
tests, a `.clang-tidy` profile, and coverage gates. The HTTP helpers currently
live in an anonymous namespace inside `http_server.cpp`, so a fuzzer cannot
link them. Runner/hub start functions call syscalls directly, so failure
branches are unreachable in tests.

## Goals / Non-Goals

**Goals:**

- Give the parser a stable, socket-free surface for coverage-guided fuzzing.
- Make cold-path syscall failures injectable without production overhead.
- Deepen analyzer coverage without accepting intentional-padding noise.

**Non-Goals:**

- No fuzzing of the live server/socket loop (the pure parser is the target).
- No production fault-injection API or runtime knob.
- No hot-path instrumentation or changes to alignment/layout.

## Decisions

### D1: Extract only pure helpers

`include/nexigen/hub/detail/http_parse.hpp` contains:

- `url_decode`, `equals_ignore_case`, `trim_header`, `header_value`,
  `query_value`;
- `Request_line`, `Request_line_error`, and `parse_request_line`.

They take `string_view`s and return values; no sockets, globals, or logging.
`Http_server` includes the header and maps `Request_line_error` to the existing
400/405 responses. The header is in `detail/` and is not part of the documented
API contract.

Alternatives considered: fuzzing the live server (rejected: socket setup makes
throughput and determinism poor) and exposing the old anonymous helpers
(rejected: they cannot be linked).

### D2: libFuzzer harness behind an option

`NEXIGEN_BUILD_FUZZERS` (default `OFF`) requires Clang and adds
`nexigen_http_parse_fuzz` with `-fsanitize=fuzzer,address`. The harness:

- validates request-line invariants (`None` ⇒ `GET` and HTTP/1.0/1.1);
- checks header views stay inside the input;
- checks `url_decode`/`query_value` never expand beyond the input;
- runs the same calls the server performs.

The CI Clang job configures a side build, builds only the fuzz target, and runs
`-runs=20000` over `tests/fuzz/corpus/http_parse`. Corpus seeds cover a valid
request, percent-encoded lowercase headers, a malformed line, and a bad
method/version.

### D3: Inline test-only fault seam

`include/nexigen/core/detail/fault_injection.hpp` defines
`Fault_point`, `Fault_hook`, and `fault()`.

- With `NEXIGEN_ENABLE_FAULT_INJECTION`, `fault()` consults an inline global
  hook; tests install a function pointer and reset it afterwards.
- Without the option, `fault()` is `constexpr false` and compiles away.

Injected points: runner `pipe2`, `fork`, `/dev/null` open; hub `socket`, `bind`,
`epoll_create1`, `eventfd`. Each failure path reuses the existing cleanup code.
Debug and coverage CI builds enable the option; Release/portable/sanitizer/
Clang builds keep it off so production code paths stay unchanged.

Alternatives considered: `LD_PRELOAD` syscall shims (rejected: environment-heavy
and not portable) and dependency-injected syscall tables (rejected: a larger
refactor for the same branch coverage).

### D4: Analyzer opt-ins

Enable `clang-analyzer-optin.cplusplus.*` and
`clang-analyzer-optin.portability.*`; keep
`clang-analyzer-optin.performance.Padding` disabled with a documented rationale
because it flags intentional cache-line alignment in hot-path containers. The
enabled opt-ins currently produce zero findings on `src/*.cpp`.

## Risks / Trade-offs

- **Fuzzer drift from server behavior.** → The server calls the same header
  functions; request-line mapping is covered by existing HTTP tests.
- **Fuzz corpus growth in CI.** → The smoke run uses `-runs=20000` and an
  ephemeral workspace; the committed corpus stays at four seeds.
- **Fault hook leaking between tests.** → Each test resets the hook to `nullptr`
  and the hook is inert without the compile option.
- **Header install surface.** → `detail/http_parse.hpp` is installed with the
  rest of the headers but documented as internal; changing it is not an API
  promise.
- **Analyzer version drift.** → Only stable opt-in families are enabled; CI uses
  the distribution clang-tidy paired with the configured Clang.

## Migration Plan

No migration. Fuzzing and fault injection are opt-in build modes; default builds
and installs are unchanged.

## Open Questions

None.
