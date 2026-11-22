# Proposal

## Why

Every application that uses `app::Infrastructure` currently re-implements the same stateless policy boilerplate: console wiring, file wiring, null wiring, or iceoryx2 wiring with an application tag and a shared node. The copies drift: service names, app tags, sink start calls, file-path validation, and the `logger()`/`meter()` accessor usage diverge between examples and real applications. A ready-made policy set makes the common choices explicit and gives at least one supported “everything to iceoryx2” path.

## What Changes

- Add `nexigen::app::policies` with four stateless policies:
  - `ConsolePolicy` — console logger, console metric sink, console trace sink.
  - `FilePolicy` — file logger plus JSONL metric and trace sinks, with application name injection and capacity validation.
  - `NullPolicy` — null logger, metric sink and trace sink for benchmarks and shadow runs.
  - `Iceoryx2Policy<A,B,C,D>` — `Iceoryx2Logger`, `Iceoryx2MetricSink` and `Iceoryx2TraceSink`; all telemetry is published as fixed frames, with a four-character application tag.
- Add `include/nexigen/app/policies/policies.hpp` for the COM-free policies and a separate `iceoryx2_policy.hpp` for the COM-gated one.
- Add a shared `examples/app/wait_loop_common.hpp` and two runnable examples: console policy and all-iceoryx2 policy.
- Document the policy set in `docs/articles/app.md`.
- Align the stale `app/application` "Stateless infrastructure policy" requirement with the implemented three-component contract (`Logger`, `MetricsSink`, `TraceSink`); the unused `Chrono_sink`/`emplace_chrono_sink` wording is removed.
- No **BREAKING** changes: existing application-local policies remain valid.

## Capabilities

### New Capabilities

- `app/policies`: ready-made stateless Infrastructure policies, their telemetry mappings, failure semantics, COM gating, and the shared-node/app-tag contract for iceoryx2.

### Modified Capabilities

- `app/application`: the "Stateless infrastructure policy" requirement is corrected to the implemented three-component contract (`Logger`, `MetricsSink`, `TraceSink`) and the unused `Chrono_sink`/`emplace_chrono_sink` wording is removed. Existing behavior is otherwise unchanged.

## Impact

- **Code:** `include/nexigen/app/policies/` (header-only), `examples/app/` and `examples/app/CMakeLists.txt`.
- **Dependencies:** `ConsolePolicy`, `FilePolicy` and `NullPolicy` need only `nexigen::app`; `Iceoryx2Policy` additionally needs `nexigen::logging_com`, `nexigen::metrics_com`, `nexigen::tracing_com` and `nexigen::com`, and is available only with `NEXIGEN_ENABLE_COM=ON`.
- **Performance:** policy hooks run once during `Infrastructure::initialize`; they are cold path. No producer hot path changes. `FilePolicy` performs the existing capacity check; `Iceoryx2Policy` publishes through the existing worker-side sinks. No new runtime allocations are introduced in tick loops.
- **Behavior:** `FilePolicy` rejects invalid paths through the existing `core::Status` returns; `Iceoryx2Policy` returns `SystemError` when the shared node cannot be created.
- **Docs:** `docs/articles/app.md` gains a policy table and links to the two wait-loop examples.
