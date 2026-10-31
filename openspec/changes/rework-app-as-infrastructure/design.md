# Design

## Context

See `proposal.md` — Why, and the `core/result` and `app/application` deltas for requirements.

Current state:

- `App<Derived, Logger, Metrics_sink, Trace_sink, C, T>` is CRTP, owns the components, and exposes `current()`; applications inherit and provide emplacement hooks.
- `App` is non-copyable/non-movable but can still be declared automatically. With default tracer settings it is **10 499 776 bytes**; the stack limit here is 8 MiB, so `My_app app;` crashes before `initialize`.
- `signals_` is installed per App; two initialized Apps (even different specializations) can install competing signalfd watchers.
- `core::ErrorCode` has no code for "another owner holds a process-global resource".

## Goals / Non-Goals

**Goals:**

- Replace inheritance with composition: `Infrastructure` is a service the application owns.
- Make stack/static `Infrastructure` impossible: heap-only factory.
- Make signal ownership process-global and explicit; second initialization returns `Conflict`.
- Remove `current()`; access is explicit through the owned `Infrastructure&`.
- Keep settings, run loop, signal semantics, flush order and idempotent shutdown.
- Keep the hot path unchanged.

**Non-Goals:**

- Multiple simultaneously initialized infrastructures in one process.
- A global service locator / singleton.
- Changing `app/settings` or the log/metric/trace provider APIs.
- Providing a `static_runtime` factory (heap is the only construction mode).
- Reintroducing CRTP compatibility wrappers.

## Decisions

### D1 — Composition replaces inheritance

```
+-----------------------------+        +--------------------------------------+
| My_app                      | owns   | Infrastructure<Policy, C, T>         |
|  unique_ptr<Infrastructure> |------->| logger / sinks / providers / watcher |
|  application state          |        | run / shutdown / stop reason         |
+-----------------------------+        +--------------------------------------+
```

`My_app` is an ordinary class; it does not derive from anything. It uses `infra_->logger()`, `infra_->meter()`, `infra_->tracer_for()`, `infra_->run()`, `infra_->shutdown()`. `main` may use `Infrastructure` directly if no application class is needed.

Alternatives considered: keeping CRTP (rejected: implicit dependency, hook boilerplate, `current()` global state); fixed non-template infrastructure (rejected: logger formatting is compile-time typed).

### D2 — Stateless policy contract

```cpp
struct My_policy {
    using Logger = My_logger;
    using Metrics_sink = My_metrics_sink;
    using Trace_sink = My_trace_sink;

    static core::Status emplace_logger(
        std::optional<Logger>&, const logging::Logging_settings&) noexcept;
    static core::Status emplace_metrics_sink(
        std::optional<Metrics_sink>&, const app::Metrics_settings&) noexcept;
    static core::Status emplace_trace_sink(
        std::optional<Trace_sink>&, const app::Tracing_settings&) noexcept;
};
```

- No state: all configuration arrives in `initialize(settings)`.
- `Infrastructure` calls the static methods and `static_assert`s `logging::concepts::Logger<Logger>`, `metrics::concepts::MetricSink<Metrics_sink>`, `tracing::concepts::TraceSink<Trace_sink>`.
- Policy is reusable and testable; a `Null_policy`-style test policy needs no inheritance.

### D3 — `Infrastructure` API and member order

```cpp
template <class Policy, std::size_t Tracer_capacity = 1024,
          std::size_t Tracer_threads = 32>
class Infrastructure {
public:
    using Logger = typename Policy::Logger;
    using Metrics_sink = typename Policy::Metrics_sink;
    using Trace_sink = typename Policy::Trace_sink;
    using Tracer_provider = tracing::TracerProvider<Tracer_capacity, Tracer_threads>;
    using Tracer_type = tracing::Tracer<Tracer_capacity, Tracer_threads>;

    Infrastructure(const Infrastructure&) = delete;
    Infrastructure(Infrastructure&&) = delete;
    ~Infrastructure();

    [[nodiscard]] core::Status initialize(const App_settings& settings) noexcept;
    template <class Tick> [[nodiscard]] Infrastructure_stop_reason run(Tick&& tick) noexcept;
    void request_stop(Infrastructure_stop_reason reason) noexcept;
    [[nodiscard]] bool stop_requested() const noexcept;
    [[nodiscard]] Infrastructure_stop_reason stop_reason() const noexcept;
    [[nodiscard]] core::Status shutdown() noexcept;

    [[nodiscard]] Logger& logger() noexcept;
    [[nodiscard]] metrics::MetricProvider& metrics() noexcept;
    [[nodiscard]] core::Result<metrics::Meter*> meter(std::string_view, std::string_view) noexcept;
    [[nodiscard]] Tracer_provider& tracer() noexcept;
    [[nodiscard]] core::Result<Tracer_type*> tracer_for(std::string_view) noexcept;

private:
    Infrastructure() noexcept = default;
    template <class P, std::size_t C, std::size_t T>
    friend core::Result<std::unique_ptr<Infrastructure<P, C, T>>>
    make_infrastructure() noexcept;

    std::optional<Logger> logger_;
    std::optional<Metrics_sink> metrics_sink_;
    std::optional<metrics::MetricProvider> metrics_;
    std::optional<Trace_sink> trace_sink_;
    std::optional<Tracer_provider> tracer_;
    detail::Signal_watcher signals_;
    ...
};
```

Member order preserves the provider/sink lifetime contract: tracer/sink are destroyed before their provider, and the logger is last. No `current_` member.

### D4 — Heap-only factory

```cpp
template <class Policy, std::size_t C = 1024, std::size_t T = 32>
[[nodiscard]] core::Result<std::unique_ptr<Infrastructure<Policy, C, T>>>
make_infrastructure() noexcept;
```

- Constructor is private; only the factory instantiates the object.
- The factory uses `new (std::nothrow) Infrastructure<...>{}`; on failure returns `SystemError`, never `std::terminate`.
- `std::unique_ptr` is the sole owner; `Infrastructure` is non-movable, so stack/static construction is impossible from user code.
- Cold path: exactly one allocation at startup; no allocation on `run`, recording, or shutdown.

Alternatives: shared_ptr (rejected: unnecessary atomic refcount and shared ownership); public constructor + warning (rejected: too weak); static storage factory (out of scope; heap covers the requirement).

### D5 — Process-global signal ownership guard

```cpp
namespace detail {
alignas(64) inline std::atomic<const void*> signal_owner{nullptr};
}
```

| Operation | Atomic | Order |
|---|---|---|
| claim on `initialize` | `compare_exchange_strong(nullptr, this)` | `acq_rel` |
| release on rollback/shutdown | `store(nullptr)` | `release` |
| diagnostic (optional) | `load` | `acquire` |

- The guard is non-template, so different `Policy` specializations conflict too.
- Claim happens **before** `pthread_sigmask` and before any worker exists.
- A second `initialize` returns `core::ErrorCode::Conflict` and installs nothing.
- Rollback and `shutdown` release after the watcher is stopped, the flushes complete, and the previous mask is restored.
- No waiting/spinning: the loser fails immediately (control-plane semantics).

### D6 — Initialization order and rollback

1. If this instance is already initialized → `Ok`.
2. Claim signal ownership → `Conflict` if taken.
3. Block `SIGINT`/`SIGTERM`, saving the previous mask.
4. `Policy::emplace_logger` → `Policy::emplace_metrics_sink` → construct `MetricProvider`.
5. `Policy::emplace_trace_sink` → construct `TracerProvider` → `initialize()`.
6. Start the signalfd watcher.
7. `initialized_ = true`.

Any failure runs the same teardown as `shutdown()` (only for created components), restores the mask, releases ownership, and returns the failure.

### D7 — Shutdown

`shutdown()` remains idempotent (sequential calls), stops/joins the watcher, flushes the logger before and after writing the single final reason record, then flushes tracer → metrics → logger, restores the mask, releases signal ownership, and destroys the optional components in reverse order. The destructor calls it when initialized. Concurrent `shutdown()` remains unsupported and documented.

### D8 — Stop reason rename

`App_stop_reason` becomes `Infrastructure_stop_reason` with the same values (`None`, `Requested`, `Sigint`, `Sigterm`), the same lowercase names and JSON/formatter contract. No compatibility alias: the API is intentionally broken.

### D9 — `ErrorCode::Conflict`

`core::ErrorCode` gains `Conflict` as value **7** (append-only; existing values are not renumbered). `to_string` returns `"Conflict"`, `from_string` accepts it, JSON/formatter work through the existing generic enum machinery. Semantics: an operation rejected because another owner holds a process-global resource/state, distinct from `NotInitialized` (operation attempted before setup) and `InvalidArgument` (bad caller value).

`core/result` spec and tests are updated; no switch outside `result.hpp` needs a new arm because all other switches use `default` or compare specific codes.

### D10 — Migration and tests

- `include/nexigen/app/app.hpp` is replaced by `infrastructure.hpp`; `app_stop_reason.hpp` → `infrastructure_stop_reason.hpp`.
- `tests/app/app_test.cpp` rewrites `Test_app`/`Failing_app` hierarchies as policies (`Test_policy`, `Fail_logger_policy`, `Fail_metrics_policy`, `Fail_trace_policy`, `Flush_fail_policy`), and uses `make_infrastructure`.
- `examples/app/ex_app_lifecycle.cpp` uses a `Demo_policy` and `auto infra = make_infrastructure<Demo_policy>()`.
- README/architecture update: `Infrastructure`, heap-only factory, `Conflict`, no `current()`.
- New tests: stack construction compile-fail (scratch TU), factory allocation path (nothrow is hard to force; at least success), second initialization `Conflict`, different policies conflict, release after shutdown/failure, all existing lifecycle/signal/settings tests.

## Risks / Trade-offs

- **Breaking API** → accepted; project is v0.1 and `App` is new. All references migrate in this change.
- **One heap allocation at startup** → allowed cold path; `SystemError` on failure. No runtime allocations.
- **Signal guard means one infrastructure per process** → intended; tests must shut down before creating the next. No wait queue.
- **`Conflict` added to core enum** → append-only value 7; update core spec/tests; ABI-safe.
- **No `current()`** → deep code must receive an explicit reference; this is deliberate DI. Applications can expose their own accessor.
- **Destructor uses the guard release path** → ensure release happens after all flushes; tests cover double shutdown and sequential instances.
- **Policy with state is not supported** → documented; all configuration comes from `App_settings`.

## Migration Plan

Additive scaffolding then breaking cut:

1. Add `ErrorCode::Conflict` + core tests.
2. Add `Infrastructure`, `Policy` concept, `make_infrastructure`, guard.
3. Rewrite tests/app and example.
4. Remove `App`/`current()`/`App_stop_reason`.
5. Update README/architecture.
6. Full four-mode matrix.

Rollback: revert the change; core enum value 7 can remain unused if needed (append-only, harmless), or be reverted atomically before release.

## Open Questions

None blocking. A future change may add a `static_infrastructure` factory (placement-new in static storage) or a `Null_policy` convenience; both are out of scope here.
