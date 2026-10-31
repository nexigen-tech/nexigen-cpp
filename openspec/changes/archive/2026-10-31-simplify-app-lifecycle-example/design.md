# Design

## Context

See `proposal.md` — Why. The current `ex_app_lifecycle.cpp` is ~260 lines: three settings files, composite logger with file sink, JSONL metric/trace sinks, SIGINT helper thread and read-back verification. `Infrastructure` itself is unchanged; only the example and its spec requirement change.

## Goals / Non-Goals

**Goals:**

- A reader can see the whole `Infrastructure` flow in one short `main`.
- Demonstrate all three infrastructure paths: logger, metric factory, tracer factory.
- No application class, no settings files, no signal helper.
- Keep the executable name and CTest registration (`ex_app_lifecycle`).

**Non-Goals:**

- Re-testing settings loading or SIGINT (covered by `nexigen_app_tests`).
- File sinks or output verification.
- A new library policy type (the example defines its own local `Simple_policy`).

## Decisions

### D1 — Inline stateless policy

```cpp
struct Simple_policy {
    using Logger = logging::ConsoleLogger<>;
    using Metrics_sink = metrics::ConsoleMetricSink;
    using Trace_sink = tracing::ConsoleTraceSink;

    static core::Status emplace_logger(std::optional<Logger>& out,
                                       const logging::Logging_settings&) noexcept {
        out.emplace();
        return {};
    }
    static core::Status emplace_metrics_sink(std::optional<Metrics_sink>& out,
                                             const app::Metrics_settings&) noexcept {
        out.emplace();
        return {};
    }
    static core::Status emplace_trace_sink(std::optional<Trace_sink>& out,
                                           const app::Tracing_settings&) noexcept {
        out.emplace(1);
        return {};
    }
};
```

The policy lives in the example TU; no library/default-policy addition. `ConsoleTraceSink{1}` writes to stdout.

### D2 — Direct main, default settings

```cpp
auto created = app::make_infrastructure<Simple_policy>();
if (!created) return 1;
auto infra = std::move(*created);
if (!infra->initialize(app::App_settings{}).has_value()) return 1;
```

No `App_settings_paths`, no JSON, no file I/O. Defaults are `./logs`, Info level, collectors disabled (config defaults), which is fine because only console sinks are used. This makes initialization deterministic and side-effect-free.

### D3 — Demonstrate all three paths

```cpp
infra->logger().info("infrastructure ready");
const auto meter = infra->meter("demo", "1.0");
const auto ticks = (*meter)->create_counter<ST, Readable>("ticks");
(*ticks)->inc();
const auto tracer = infra->tracer_for("demo.loop");
{ auto span = (*tracer)->start_span("tick", tracing::SpanKind::Internal); }

int count = 0;
const auto reason = infra->run([&] {
    (*ticks)->inc();
    if (++count == 3) {
        infra->request_stop(app::Infrastructure_stop_reason::Requested);
    }
});
if (reason != Requested) return 1;
if (!infra->shutdown().has_value()) return 1;
```

`run` stops deterministically via `request_stop`, so no helper thread or timing. The tracer span is finished before shutdown (required for the tracer flush to see it).

### D4 — Failure visibility

Every fallible step (`make_infrastructure`, `meter`, `create_counter`, `tracer_for`, `initialize`, `shutdown`) is checked with `has_value()` and returns 1 with a printed message. No `.value()`.

## Risks / Trade-offs

- **Example no longer covers settings/signals** → unit tests keep that coverage; the example's purpose is first-contact readability.
- **Console output in CTest** → already common for other examples; exit status is the smoke assertion.
- **Policy uses only console sinks** → no file paths/cleanup; deterministic.
- **Tracer span must end before shutdown** → explicit scope block before `shutdown`.

## Migration Plan

Replace the example TU and its CMake entry stays the same. Rollback: restore the previous example file; no library/API changes.
