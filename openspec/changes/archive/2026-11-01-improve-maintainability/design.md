# Design

## Context

See `proposal.md` — Why. The affected code is cold-path control plane with
strong existing tests (runner/suite/hub suites plus coverage gates). The P3 work
must keep every observable behaviour identical, including fork/exec ordering,
state-file paths, HTTP status codes, and SSE framing.

## Goals / Non-Goals

**Goals:**

- Make each control-plane phase independently readable and testable.
- Exercise the HTTP parser with deterministic pseudo-random inputs.
- Make the `Result::value()` ban fail CI on new violations.
- Remove the flagged conversion and reserve nits.

**Non-Goals:**

- No new HTTP features, routes, status codes, or header parsing rules.
- No change to runner restart policy, fork/exec sequence, or file formats.
- No public API or build-option changes.
- No coverage-guided fuzzing infrastructure or new external dependency.

## Decisions

### D1: Extract phases without changing call order

`Runner::start` keeps its public signature and performs the same steps in the
same order. The extracted helpers receive explicit context structs:

- `resolve_start_paths` — command/options validation, identity resolution,
  instance/artifact/stream path derivation and conflict checks.
- `prepare_instance_layout` — directory creation, telemetry path resolution,
  settings generation, stream-file creation, log-file opening.
- `run_instance_supervisor` — the child branch: second `fork`, application
  setup, `exec`, supervision, exit metadata.

The supervisor context owns the already-open descriptors and copies the
values it needs before `fork`. `child_exit()` and the gcov flush remain the only
exit path for children.

`run_suite_supervisor` keeps its loop but delegates three phases:

- `handle_stop_request` — stop marker handling and final exit.
- `advance_suite_restart` — pending suite restart and launch.
- `advance_app` — per-app exit consumption and retry/restart scheduling.

The helpers return a small enum/bool that tells the loop whether to continue,
so control flow stays explicit.

`Http_server::handle_request` becomes a dispatcher over:

- `handle_health`, `handle_tree`, `handle_streams`, `handle_recent`,
  `handle_events`, and `handle_assets`.

The dispatcher still performs request-line validation, method/version checks,
and keep-alive decisions.

Alternatives considered: mechanical line-based splitting (rejected: creates
helpers with too many parameters and no coherent phase) and rewriting the
control loops (rejected: unnecessary risk for a maintainability change).

### D2: Deterministic malformed-request corpus

Add `test_http_fuzz_corpus` to `tests/hub/hub_test.cpp`. It starts the normal
server once, then generates a fixed-seed set of payloads:

- random bytes including NUL, CR, LF, spaces, and `:`/`%`/`?`;
- truncated request lines and header blocks;
- oversized lines and headers;
- valid request lines with random header combinations.

Each payload is sent over a raw socket with a receive timeout. The test asserts
that the server either responds with a bounded status or closes the connection,
and that a final health request still succeeds. A fixed seed keeps failures
reproducible; the number of cases is bounded so the CTest runtime stays under a
few seconds.

Alternatives considered: libFuzzer (rejected for this change: requires a Clang
fuzz target, a separate CI job, and an extracted parser surface; the live-server
corpus catches the same class of crashes without new infrastructure).

### D3: Receiver-allowlisted `.value()` guard

`cmake/check_result_value.py` scans tracked C++ sources outside build
directories. It strips comments and string literals, finds `.value()` calls, and
extracts the receiver expression. A violation is reported unless the receiver
matches one of:

- a span/trace ID accessor (`trace_id`, `span_id`, `parent_span_id`, `id`);
- a nlohmann JSON iterator (`entry`, `item`);
- an explicitly marked line `// NX_ALLOW_VALUE`.

The script exits non-zero and prints `path:line: receiver.value()` for each
violation. CI runs it in the OpenSpec/quality job. The allowlist is intentionally
small; adding a new non-Result receiver requires a reviewable edit.

Alternative considered: a grep for every `.value()` with no allowlist (rejected:
already produces 27 false positives, so the check would be disabled quickly).

### D4: Static-analysis nits

- In `streaming.hpp` and `http_server.cpp`, write constant products as
  `std::size_t{...} * ...` so the multiplication happens in the target type
  before any narrowing conversion.
- Reserve `standalone_instances` in `discovery.cpp` before the loop.

## Risks / Trade-offs

- **Refactor drift:** extracting phases can accidentally change ordering or
  descriptor lifetime. → The existing runner/suite/hub suites and coverage
  gates run in all five GCC modes plus Clang; descriptor ownership stays in the
  same scope or is moved explicitly.
- **Fuzz corpus flakiness:** random bytes could produce long waits. → Fixed
  seed, bounded payload size, per-socket receive timeout, and a final health
  probe.
- **Allowlist bypass:** a new `.value()` on a receiver named `id` would pass. →
  The allowlist is documented and limited to known non-Result types; the ban
  remains a review rule in addition to the automated check.
- **Coverage shift:** moving lines between functions can change which branch is
  scored. → Re-run the coverage target and keep runner/hub ≥90%.

## Migration Plan

No migration. Revert is a normal revert of the change; no persisted state or API
changes.

## Open Questions

None.
