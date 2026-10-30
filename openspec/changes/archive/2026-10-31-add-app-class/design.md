# Design

## Context

See `proposal.md` — Why, and the `app/application` and `app/settings` deltas for requirements.

Current state that shapes the approach:

- No `nexigen::app` module exists; there is no signal handling anywhere in the tree.
- `logging::load_logging_settings` already implements the explicit global/local JSON pipeline for `LoggerOptions`.
- `metrics::MetricProvider_config` and `tracing::TracerProvider_config` are plain aggregates with `to_json`/`from_json` and `to_string`.
- `MetricProvider`, `TracerProvider`, async loggers and composite sinks are **non-movable**; providers hold sink references, so sinks must outlive providers. Logger types also differ in how they are configured (`FileLogger::initialize(settings)`, `CompositeLogger` configures its members individually).
- Threads must inherit a blocked `SIGINT`/`SIGTERM` mask, so masking has to happen before any logger/metrics/tracing worker starts.

## Goals / Non-Goals

**Goals:**

- One correct-by-default bootstrap: settings → logger → sinks → providers → signal watcher → `current()`.
- Predictable, idempotent shutdown with a flushed, logged stop reason.
- SIGINT/SIGTERM handled without async-signal-unsafe work in a signal handler.
- Metrics/tracing settings loaded from files with the same semantics as logging.
- A runnable example that exercises the real signal path.

**Non-Goals:**

- A process-wide singleton owner.
- SIGHUP reload, hot settings reload, or runtime provider reconfiguration.
- Windows/portable signal abstraction (target is Linux).
- Changing existing logger/provider APIs or their specs.
- A general dependency-injection container; the CRTP hooks are the only customization point.

## Decisions

### D1 — New module and dependency direction

New STATIC target `nexigen_app` (alias `nexigen::app`):

```
include/nexigen/app/app.hpp, app_stop_reason.hpp, app_settings.hpp
src/app/app_settings.cpp
tests/app/app_test.cpp
examples/app/ex_app_lifecycle.cpp
```

Dependencies are one-way: `app → logging, metrics, tracing, chrono, core`. The module never consumes another module's `detail/`; the small cold-path file/JSON reader needed for metrics/tracing settings is duplicated inside `src/app/app_settings.cpp` (the architecture rule allows duplication over cross-module `detail/` use).

### D2 — CRTP contract and member order

```cpp
template <class Derived, class Logger_type, class Metrics_sink_type, class Trace_sink_type,
          std::size_t Tracer_capacity = 1024,
          std::size_t Tracer_threads = 32>
class App {
public:
    using Logger = Logger_type;
    using Metrics_sink = Metrics_sink_type;
    using Trace_sink = Trace_sink_type;
    using Tracer_provider = tracing::TracerProvider<Tracer_capacity, Tracer_threads>;
    using Tracer_type = tracing::Tracer<Tracer_capacity, Tracer_threads>;

    ~App();
    App(const App&) = delete;
    App& operator=(const App&) = delete;

    [[nodiscard]] core::Status initialize(const App_settings& settings) noexcept;
    template <class Tick> [[nodiscard]] App_stop_reason run(Tick&& tick) noexcept;
    void request_stop(App_stop_reason reason) noexcept;
    [[nodiscard]] bool stop_requested() const noexcept;
    [[nodiscard]] App_stop_reason stop_reason() const noexcept;
    [[nodiscard]] core::Status shutdown() noexcept;

    [[nodiscard]] Logger& logger() noexcept;
    [[nodiscard]] metrics::MetricProvider& metrics() noexcept;
    [[nodiscard]] core::Result<metrics::Meter*> meter(std::string_view name,
                                                     std::string_view version) noexcept;
    [[nodiscard]] Tracer_provider& tracer() noexcept;
    [[nodiscard]] core::Result<Tracer_type*> tracer_for(std::string_view scope) noexcept;

    [[nodiscard]] static App* current() noexcept;

protected:
    App() noexcept = default;
    [[nodiscard]] Derived& derived() noexcept { return *static_cast<Derived*>(this); }
};
```

The derived application provides three component types as template arguments and three emplacement hooks:

```cpp
using Logger = logging::CompositeLogger<logging::ConsoleLogger<>, logging::FileLogger<>>;
using Metrics_sink = metrics::CompositeMetricSink<metrics::ConsoleMetricSink,
                                                  metrics::JsonFileMetricSink>;
using Trace_sink = tracing::CompositeTraceSink<tracing::ConsoleTraceSink,
                                               tracing::JsonFileTraceSink>;

core::Status emplace_logger(std::optional<Logger>& out,
                            const logging::Logging_settings& settings) noexcept;
core::Status emplace_metrics_sink(std::optional<Metrics_sink>& out,
                                  const Metrics_settings& settings) noexcept;
core::Status emplace_trace_sink(std::optional<Trace_sink>& out,
                                const Tracing_settings& settings) noexcept;
```

Hooks construct in place (`out.emplace(...)`), so non-movable loggers/sinks are supported and can use loaded settings (e.g. JSONL paths) before providers exist.

Base member order is the lifetime contract:

```cpp
std::optional<Logger> logger_;
std::optional<Metrics_sink> metrics_sink_;
std::optional<metrics::MetricProvider> metrics_;
std::optional<Trace_sink> trace_sink_;
std::optional<Tracer_provider> tracer_;
Signal_watcher signals_;
```

Destruction is reverse: tracer → trace_sink → metrics → metrics_sink → logger. A provider can never outlive its sink. `std::optional<T>` stores `T` inline, so no heap allocation is introduced; `emplace` constructs exactly once. `static_assert` pins `concepts::Logger<Logger>`, `concepts::MetricSink<Metrics_sink>`, `concepts::TraceSink<Trace_sink>`.

Alternatives considered: derived-owned components (rejected: sinks needing settings cannot be constructed before settings are loaded); a fixed default bundle (rejected: forces console/file sinks on every application); raw references to externally owned components (rejected: the base could not guarantee lifetime).

The provider accessors are the creation entry points defined by the existing specs: `metrics().get_meter(name, version)` returns the only meter factory, and `tracer().get_tracer(scope)` returns the only tracer factory. `meter()` and `tracer_for()` are thin conveniences over those calls so application code does not repeat null checks; both return `NotInitialized` when the corresponding provider is not initialized. Handles returned by them stay valid while the App object is alive (the providers own their registries), so the base class never wraps or copies meter/tracer handles.

### D3 — Initialization order and rollback

`initialize(settings)`:

1. If already initialized → `Ok` (idempotent).
2. `pthread_sigmask(SIG_BLOCK, {SIGINT, SIGTERM}, &previous_mask_)` — **before any worker thread exists**; workers created later inherit the mask.
3. `derived().emplace_logger(logger_, settings.logging)`; then `logger_->...` is ready for diagnostics.
4. `derived().emplace_metrics_sink(metrics_sink_, settings.metrics)`; `metrics_.emplace(settings.metrics.options, *metrics_sink_)`.
5. `derived().emplace_trace_sink(trace_sink_, settings.tracing)`; `tracer_.emplace(settings.tracing.options, *trace_sink_)`; `tracer_->initialize()`.
6. Start `Signal_watcher` (signalfd + `std::jthread`).
7. Set `initialized_ = true`; `current_.store(static_cast<Derived*>(this), std::memory_order_release)`.

Any failure calls the same teardown path as `shutdown()` (only for components already emplaced), restores the saved mask, leaves `current()` null, and returns the first failure. All steps are cold path.

### D4 — Stop flag, run loop and atomics

```cpp
alignas(64) std::atomic<App_stop_reason> stop_reason_{App_stop_reason::None};
static inline alignas(64) std::atomic<App*> current_{nullptr};
```

| Operation | Atomic | Order |
|---|---|---|
| `request_stop(reason)` | `stop_reason_.compare_exchange_strong(None, reason)` | `acq_rel`; first reason wins, later calls leave it unchanged |
| `stop_requested()` | `stop_reason_.load()` | `acquire` |
| `stop_reason()` | `stop_reason_.load()` | `acquire` |
| `current()` publish/read/clear | `current_.load/store` | `release` store, `acquire` load |

`stop_reason_` is `alignas(64)` so the per-tick load never shares a cache line with writers. `run(Tick&&)`:

```cpp
while (!stop_requested()) {
    if constexpr (std::is_invocable_r_v<bool, Tick&>) {
        if (!tick()) { request_stop(App_stop_reason::Requested); break; }
    } else {
        tick();
    }
}
return stop_reason();
```

No allocation, no lock, no syscall on a tick that does not cross a stop.

### D5 — SIGINT/SIGTERM via signalfd watcher

- `Signal_watcher` stores the saved `sigset_t previous_mask_`, a `signalfd` descriptor, and a `std::jthread`.
- The signalfd is created with `signalfd(-1, &mask, SFD_CLOEXEC | SFD_NONBLOCK)`; `SIGINT` and `SIGTERM` are added to the blocked mask.
- The watcher owns the only consumer of those signals; it loops on `poll(fd, ..., 100 ms)`, reads `signalfd_siginfo`, maps `SIGINT`/`SIGTERM` to the reason, and calls `request_stop`. The 100 ms poll timeout is the upper bound for observing the stop token during shutdown, so `join()` cannot hang.
- The watcher does **not** log: only a normal thread performs I/O. Exactly one final record is emitted during `shutdown()`.
- `shutdown()` restores `previous_mask_` on the calling thread, so an application can be embedded in a host that had its own mask.
- A signalfd creation or `sigprocmask` failure returns `SystemError` and rolls initialization back.

Alternatives considered: logging directly in a `sigaction` handler (rejected: not async-signal-safe); handler setting a `volatile sig_atomic_t` (simpler, but the requirement asks for a dedicated, testable shutdown-reason path and signalfd is available on the target).

### D6 — Shutdown order and idempotency

`shutdown()` returns `core::Status` (the destructor calls it and ignores the result):

1. Sequential idempotency guard: if shutdown already started, return OK. Concurrent shutdown from multiple threads is explicitly unsupported and documented.
2. Stop and join the watcher (sets the stop token; poll timeout bounds the join).
3. `logger_->flush()` once, then log the final reason record, then flush again — so the reason record is accepted even if the ring was nearly full, and durable on return.
4. `tracer_->flush()`, then `metrics_->collect_now()` + `metrics_->flush()` (tracer first so spans can reference the final tick).
5. Restore `previous_mask_`; clear `current_` with `release`; destroy optional components in the member order defined in D2.
6. Return the first non-OK status encountered; each step runs even if an earlier one failed, so one failed flush cannot hide another component.

The final record text is stable: `"shutdown reason: <reason>"`, plus the signal number for `Sigint`/`Sigterm` (e.g. `"shutdown reason: sigint (2)"`).

### D7 — Settings model and loaders

New `include/nexigen/app/app_settings.hpp`:

```cpp
namespace nexigen::app {
using Settings_paths = logging::Settings_paths;            // reuse explicit file paths
using Settings_diagnostic = logging::Settings_diagnostic;  // reuse source/field/message

struct Metrics_settings {
    metrics::MetricProvider_config options{};
    bool global_loaded{false};
    bool local_loaded{false};
    std::vector<Settings_diagnostic> diagnostics{};
};

struct Tracing_settings {
    tracing::TracerProvider_config options{};
    bool global_loaded{false};
    bool local_loaded{false};
    std::vector<Settings_diagnostic> diagnostics{};
};

struct App_settings_paths {
    logging::Settings_paths logging{};
    logging::Settings_paths metrics{};
    logging::Settings_paths tracing{};
};

struct App_settings {
    logging::Logging_settings logging{};
    Metrics_settings metrics{};
    Tracing_settings tracing{};
};

core::Status load_metrics_settings(const Settings_paths&, Metrics_settings&) noexcept;
core::Status load_tracing_settings(const Settings_paths&, Tracing_settings&) noexcept;
core::Status load_app_settings(const App_settings_paths&, App_settings&) noexcept;
}
```

Per-subsystem semantics (matching `logging/logging-settings`):

- Defaults → global JSON → local JSON → environment, field-by-field; absent fields inherit.
- Missing file: no diagnostic, source flag false. Unreadable file: `SystemError`. Malformed/wrong type/invalid range: diagnostic, last value kept, `InvalidArgument`.
- `load_app_settings` calls `logging::load_logging_settings` plus the two new loaders and returns the worst status (`SystemError` > `InvalidArgument` > `Ok`), with every subsystem populated best-effort.
- JSON key tables: metrics `collect_interval_ns`, `core_mask`, `max_meters`, `max_counters`, `max_gauges`, `max_statistics`, `max_explicit_histograms`, `max_exponential_histograms`; tracing `core_mask`, `max_tracers`, `max_spans_per_window`, `window_ns`.
- Environment: `NEXIGEN_METRICS_COLLECT_INTERVAL_NS`, `NEXIGEN_METRICS_CORE_MASK`, `NEXIGEN_TRACING_CORE_MASK`, `NEXIGEN_TRACING_MAX_TRACERS`, `NEXIGEN_TRACING_MAX_SPANS_PER_WINDOW`, `NEXIGEN_TRACING_WINDOW_NS`. Parsing uses `std::from_chars`; empty/unset is ignored.
- Serialization: `to_string` + free helper + `std::formatter` for all four types; `to_json` for all; `from_json` only for `Settings_paths` (loader outputs are factory values, pinned absent in the test TU).

The duplicated cold-path reader lives in `src/app/app_settings.cpp` (open/read/stat, `nlohmann::json::parse(..., false)`, `is_discarded`, NUL checks), mirroring `src/logging/logging_settings.cpp:36-160` without importing another module's `detail/`.

### D8 — Example and tests

- `ex_app_lifecycle` defines `Demo_app : App<Demo_app>` with `CompositeLogger<ConsoleLogger<>, FileLogger<>>`, `CompositeMetricSink<ConsoleMetricSink, JsonFileMetricSink>`, `CompositeTraceSink<ConsoleTraceSink, JsonFileTraceSink>` and the three emplacement hooks. It writes temporary logging/metrics/tracing pairs, loads them with `load_app_settings`, initializes, logs through `logger()`, creates and records a metric through `meter()`, opens and finishes a span through `tracer_for()`, runs ticks, raises `SIGINT` from a helper thread after a few ticks, checks `run` returned `Sigint`, calls `shutdown()`, verifies the JSONL log contains the shutdown-reason record, verifies the metric snapshot, and exits 0.
- `tests/app/app_test.cpp` uses the existing zero-dependency harness (`NX_CHECK`/`run_suite`) and covers: settings merge/env/diagnostics/serialization, compile-time `from_json` absence, initialize/shutdown idempotency, rollback on a failing hook, `run` stop reasons, `current()` publish/clear, and a real `raise(SIGINT)` watcher test.

## Risks / Trade-offs

- **Signals are process-global** → one App is the signal owner while initialized; document that `current()` is not an owner and that nested Apps must not both install the watcher. Tests restore the previous mask in `shutdown`.
- **Concurrent `shutdown()` is unsupported** → sequential idempotency is guaranteed; document control-plane usage from the main thread only.
- **A signal arriving during a tick is observed only at the next loop check** → that is the intended graceful-stop semantics; a tick that never returns cannot be interrupted (documented, same as any cooperative loop).
- **Ring-full could drop the final reason record** → `shutdown()` flushes first, then logs, then flushes again; drops remain visible through the logger's `dropped()` counters.
- **`std::jthread` creation failure cannot throw meaningfully under `-fno-exceptions`** → cold-path thread creation failure terminates the process; this matches existing logger/provider behavior and is documented as a deployment assumption.
- **Duplicated file/JSON reader** → accepted to keep dependency arrows one-way; a future shared cold-path utility would require its own change/spec.
- **`current()` can dangle if the application is destroyed without `shutdown()`** → the destructor clears it; documented as non-owning.

## Migration Plan

Additive: new module, new target, new example and tests; no existing API changes. CMake adds `nexigen_app`, `examples/app`, `tests/app`. Rollback: delete the new module/example/tests and their CMake entries; existing modules are untouched.

## Open Questions

None blocking. A future change may add SIGHUP-based settings reload, a dynamic component bundle, or a portable signal abstraction; none is required for this base class.
