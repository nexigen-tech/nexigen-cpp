# Proposal

## Why

When the runner's suite supervisor or CLI control thread fails to apply a best-effort `runner_control_thread`/`runner_logger_thread` spec, it prints a fixed message — `runner_control_cpus is invalid; suite supervisor is unpinned` — regardless of the actual failure. The failure may be a name, scheduler, or validation rejection rather than a CPU problem, and under `BestEffort` some attributes (name, affinity) may have applied while another failed, so "unpinned" misrepresents the real state. The detailed `ThreadApplyReport` is already computed inside `apply_runner_thread_spec` but discarded before the caller can use it.

## What Changes

- Surface the `core::ThreadApplyReport` from `runner::apply_runner_thread_spec` via a report-out overload, keeping the existing two-argument form as a wrapper.
- Replace the fixed control-thread and CLI affinity diagnostics with messages derived from the report: which of name/affinity/scheduler were requested, applied, and failed, plus the first error.
- Add a cold-path human-readable rendering of a `ThreadApplyReport` (per-attribute status) so operator-facing warnings name the real attribute instead of numeric masks.
- Correct the suite supervisor's message so it no longer claims "unpinned" or "invalid cpus" when a non-CPU attribute failed or when partial attributes succeeded.

## Capabilities

### New Capabilities

None.

### Modified Capabilities

- `runner/logging`: the "Affinity failures are non-fatal warnings" requirement is strengthened so that a best-effort warning SHALL report the actual per-attribute application outcome from the `ThreadApplyReport` (requested/applied/failed and first error) rather than a generic invalid/unpinned message.

## Impact

- Code: `include/nexigen/runner/runner_logging.hpp`, `src/runner/runner_logging.cpp`, `src/runner/app_suite.cpp`, `tools/nexigen-run/main.cpp`.
- Tests: `tests/runner/runner_logging_test.cpp` (and any runner diagnostics assertions).
- API: additive overload of `apply_runner_thread_spec`; the existing signature is unchanged, so no breaking change.
- Performance/latency/allocation: none on hot paths — this is control-plane startup code. The report is the existing 64-byte `alignas(64)` `ThreadApplyReport`; the new diagnostic string formatting is cold-path only and runs once per control-thread/logger-thread startup.
