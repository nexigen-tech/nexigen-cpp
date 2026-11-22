# Design

## Context

See `proposal.md` — Why. The relevant current state:

- `InfrastructurePolicy` in `include/nexigen/app/infrastructure.hpp` requires `Logger`, `MetricsSink`, `TraceSink` and three static emplacement hooks; it does not require a chrono sink.
- Every example currently defines its own policy struct. The console and Iceoryx2 wiring already exist in `ex_app_lifecycle.cpp` and `examples/integration/suite_2_apps/ex_receiver.cpp`, but they are duplicated and not reusable.
- `Iceoryx2Logger` owns its own `com::Node`; `Iceoryx2MetricSink` and `Iceoryx2TraceSink` borrow a `com::Node*`, so a policy that publishes all three needs a shared node lifetime.
- `FileLogger` and the JSONL metric/trace sinks already expose the needed configuration structures; `logging::apply_to`, `metrics::Settings::sink`, and `tracing::Settings::sink` are the mapping points.

## Goals / Non-Goals

**Goals:**

- Provide one obvious policy per common destination: console, JSONL files, null, iceoryx2.
- Keep the policies stateless and header-only so they satisfy the existing compile-time contract.
- Make the “all telemetry to iceoryx2” case a first-class, runnable example.
- Keep the COM-free parts usable without `NEXIGEN_ENABLE_COM`.

**Non-Goals:**

- No new dynamic configuration or runtime policy switching.
- No new CMake library target for the basic policies; they are header-only over `nexigen::app`.
- No changes to the telemetry sinks or their service names.
- No attempt to fix unrelated stale wording in `app/application` beyond the policy contract correction described below.

## Decisions

### D1 — Location and headers

Place the policies in `include/nexigen/app/policies/`:

- `console_policy.hpp`, `file_policy.hpp`, `null_policy.hpp` — COM-free.
- `iceoryx2_policy.hpp` — COM-only.
- `policies.hpp` — umbrella for the three COM-free policies; it deliberately does not include the iceoryx2 header.

Rationale: the policies belong to the application layer; headers are installed with `include/nexigen`, and basic policies need only `nexigen::app`. The iceoryx2 policy is a separate header because including `com` headers in a non-COM consumer would break the build; consumers link `nexigen::logging_com`, `nexigen::metrics_com`, `nexigen::tracing_com` and `nexigen::com` explicitly.

Alternative considered: a new `nexigen_app_policies` library target. Rejected for now: there is no compiled code, and a target would only duplicate the include path. If the set grows, an interface target can be added later.

### D2 — Console policy

`ConsolePolicy` uses `ConsoleLogger<>`, `ConsoleMetricSink`, `ConsoleTraceSink`. The logger config is mapped through `logging::apply_to`; the trace sink is constructed with capacity 1 (the same wiring as the existing lifecycle example). Metrics use the sink default.

### D3 — File policy

`FilePolicy` uses `FileLogger<>`, `JsonFileMetricSink`, `JsonFileTraceSink`.

- Logger: `apply_to(settings, config)`, `check_capacity<capacity, max_threads, 128>`, and `app_name` injection; the default hourly rotation is preserved.
- Metrics: copy `settings.sink`; reject an empty `path` with `InvalidArgument`; inject `app_name`.
- Traces: copy `settings.sink`; reject an empty `path` with `InvalidArgument`; inject `app_name`.

Rationale: a ready-made file policy that silently writes nowhere is worse than an explicit failure. The path checks move the failure to `initialize`, before the application enters its run loop.

### D4 — Null policy

`NullPolicy` uses `NullLogger`, `NullMetricSink`, `NullTraceSink`; every hook emplaces the default object and returns OK. It exists for benchmarks, unit tests and shadow runs where the call shape must remain identical.

### D5 — Iceoryx2 policy and shared node

`Iceoryx2Policy<A,B,C,D>` uses `Iceoryx2Logger<>`, `Iceoryx2MetricSink`, `Iceoryx2TraceSink`.

- The four template characters form the application tag (`std::string(k_tag, 4)`), stamped into every frame.
- Log service: `Nexigen/Log/nexigen` (the logger default). Metric service: `nexigen/metrics/stream`. Trace service: `nexigen/trace/stream`.
- The logger creates its own node during `initialize()`.
- Metric and trace sinks share one process-wide node from `iceoryx2_shared_node()`, a function-local `static std::optional<com::Node>`. The node is created lazily and lives until process exit; sinks and providers live only until the `Infrastructure` destructor, which is earlier.
- `emplace_metrics_sink` calls `start()` immediately (the existing example behavior); `emplace_trace_sink` leaves `start()` to the provider.
- A null shared node returns `SystemError`; the policy never calls `std::terminate`.

Alternative considered: pass a node through a policy object or a context. Rejected because `InfrastructurePolicy` is stateless and static; a shared node is the existing pattern in `ex_receiver.cpp`/`sim_common.hpp`. A future interface target could expose an explicit node provider, but that is out of scope.

### D6 — Examples and shared wait loop

Move the wait-loop body into `examples/app/wait_loop_common.hpp`, parameterized by policy:

- `ex_app_wait_loop.cpp` uses `ConsolePolicy`.
- `ex_app_wait_loop_iceoryx2.cpp` uses `Iceoryx2Policy<'W','A','I','T'>`; added to CMake only under `NEXIGEN_ENABLE_COM`.
- Both use the same producer/queue/condition-variable loop with a 100 ms timeout, so the run loop can still observe signals and time-base loss.

Rationale: one behavior, two destinations; no duplicated wait-loop logic.

### D7 — Diagnostics

Each policy exposes `to_string()`, a free `to_string`, a `std::formatter` specialization, and non-throwing `to_json`/`from_json`. The JSON identifies the policy type (and the iceoryx2 application tag); `from_json` is a no-op because the policies are stateless. This follows the project-wide serialization mandate and gives the spec a testable diagnostics surface.

### D8 — Spec correction

The existing `app/application` “Stateless infrastructure policy” requirement mentions `Chrono_sink`/`emplace_chrono_sink`, which the implemented `InfrastructurePolicy` concept does not require. The change replaces that requirement with the three-component contract used by the code and the new policies. No other `app/application` behavior changes.

## Risks / Trade-offs

- **[Static shared node lifetime]** The iceoryx2 node is a function-local static; an `Infrastructure` stored in another static with a later destruction order could outlive it. → Mitigation: document that the infrastructure must be destroyed before process exit; the policy is intended for ordinary `main` scopes. A future explicit node provider can remove the singleton.
- **[FilePolicy path requirements]** File sinks need explicit metric/trace paths; callers used to defaults may see `InvalidArgument`. → Mitigation: the error is early and named; the policy documentation and `app.md` state the requirement.
- **[COM header isolation]** Including `iceoryx2_policy.hpp` without COM fails the build. → Mitigation: keep it out of `policies.hpp`, document the required link targets and gate, and compile the iceoryx2 example only under `NEXIGEN_ENABLE_COM`.
- **[Policy diagnostics noise]** The policies are stateless, so JSON round-trip carries type identity only. → Mitigation: keep `from_json` a no-op and document that the policies are not configuration values.
- **[Spec correction scope]** The `app/application` modification is a wording correction, not a behavior change; a reader may expect it to be separate. → Mitigation: call it out in the proposal and design; no code behavior changes.

## Migration Plan

No migration is required: existing application-local policies remain valid. Consumers can switch by replacing their local policy type with `nexigen::app::policies::ConsolePolicy`/`FilePolicy`/`NullPolicy`/`Iceoryx2Policy<...>`. Rollback is deleting the new headers and examples; no data or ABI changes.

## Open Questions

None at this point. A future `nexigen_app_policies` interface target or an explicit node-provider abstraction can be added without changing the policy behavior or the specs.
