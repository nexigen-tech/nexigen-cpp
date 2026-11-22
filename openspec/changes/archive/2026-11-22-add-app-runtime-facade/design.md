# Design

## Context

See `proposal.md` — Why. The current state that shapes the facade:

- `Infrastructure` is heap-only, owns logging/metrics/tracing, the time-base client and the signal
  watcher, and exposes `logger()`, `meter()`, `tracer_for()`, `run()` and `shutdown()`.
- `app::Settings` and `load_app_settings` already merge the four subsystem files; `--app-id` and
  `--app-name` already reach the settings through the standard CLI.
- The existing short examples prove the shape (`ex_app_wait_loop_iceoryx2.cpp` is 14 lines), but the
  reusable body lives in `examples/app/wait_loop_common.hpp` (~200 lines) and similar code is copied
  into `sim_common.hpp` (~580 lines).
- `EventChannel<T>`, `EventProducer<T>`, `EventConsumer<T>` and `EventConsumerSet<N>` already provide
  the event transport, app identity and WaitSet dispatch needed by the facade.
- The project is built with `-fno-exceptions` and `-fno-rtti`; all failures are `core::Result`/
  `core::Status`. `SIGINT`/`SIGTERM` are blocked in `Infrastructure::initialize` and watched through
  `signalfd`; the stop reason is first-wins and serializable.

## Goals / Non-Goals

**Goals:**

- A `main` body of roughly 20–30 lines for a one-input application.
- One explicit object owning bootstrap, infrastructure, ports and shutdown.
- External time base by default, embedded only by explicit request.
- A single diagnostics gate before the loop starts.
- Consistent signal/exit behavior across applications.

**Non-Goals:**

- Multiple input types or a general timer/scheduler in the first slice.
- Hot reconfiguration or SIGHUP reload.
- Replacing low-level `Infrastructure` or existing examples.
- Hot-path changes; the facade is cold-path control code.

## Decisions

### D1 — `App<Policy>` is an explicit object

```cpp
enum class TimeBaseMode : std::uint8_t { External, Embedded };

template <class Policy>
class App {
public:
    static core::Result<std::unique_ptr<App>>
    create(int argc, const char* const argv[], std::string_view app_name,
           TimeBaseMode mode = TimeBaseMode::External) noexcept;

    [[nodiscard]] core::Result<InfrastructureStopReason> run() noexcept;
    [[nodiscard]] core::Status shutdown() noexcept;

    [[nodiscard]] metrics::Counter<...>* counter(std::string_view name,
                                                 std::string_view unit = {}) noexcept;
    template <class T>
    [[nodiscard]] com::EventProducer<T>* publish(std::string_view service) noexcept;
    template <class T, class Callback>
    bool subscribe(std::string_view service, Callback&& callback) noexcept;

    [[nodiscard]] std::span<const core::SettingsDiagnostic> diagnostics() const noexcept;
    [[nodiscard]] std::string to_string() const;
};
```

`App` is non-copyable and non-movable. It is allocated by the factory so its address stays stable
for the WaitSet attachments and borrowed producer pointers. The destructor calls `shutdown()` if the
caller did not.

Alternative considered: `app::run<Policy>(argc, argv, body)` with one callback. Rejected for the
first slice because it hides initialization, instruments and shutdown; the user asked for an
explicit sequence. It can be added later as a convenience wrapper over `App`.

### D2 — `create` result and help

`create` returns `Result<unique_ptr<App>>`. A successful `--help` path prints usage and returns
success with a null pointer; the caller distinguishes it:

```cpp
auto created = App<Policy>::create(argc, argv, "doubler", TimeBaseMode::Embedded);
if (!created) { print(created.error()); return 1; }
if (!*created) return 0; // help
auto app = std::move(*created);
```

This avoids a second public result struct and keeps the non-help failure path a normal `Status`.

### D3 — Bootstrap defaults and required identity

`App::create` first checks whether a settings directory was supplied:

- **With a directory:** use `parse_command_line` + `load_app_settings`, preserving the existing
  strict CLI/settings contract.
- **Without a directory:** use a small identity-only parser for `-an/--app-name`, `-ai/--app-id`,
  `-lgl/--log-level`, and `-h/--help`; build `Settings::create(app_name)` and apply those values.
  This is the standalone quickstart path and does not change the public `app/command-line`
  requirement that a direct caller of `parse_command_line` must supply a settings source.

After either path, `Settings::app_id` must be set. Empty identity is `InvalidArgument` with a
diagnostic naming `--app-id`; there is no app-name or compiled-tag fallback in the facade.

Alternative considered: relax `parse_command_line` globally. Rejected: it would change an existing
contract for all callers; the relaxed behavior belongs to `App`.

### D4 — Time-base modes

`External` is the default: `Infrastructure::initialize` attaches to `settings.chrono.segment_name`
and fails when the base is absent. `Embedded` creates a local published base before initialization.

The local publisher currently lives in `examples/app/time_base_fixture.hpp` as
`nexigen::examples::LocalTimeBase`. It moves into the app library as an internal
`app::detail::EmbeddedTimeBase` (a header-only detail header under
`include/nexigen/app/detail/`), so examples and `App` share one implementation. `App` owns it in a `std::optional`/`unique_ptr` before the
`Infrastructure` member, so destruction order is infrastructure then time base.

`Embedded` is explicit and documented as examples/tests only; there is no silent fallback when
`External` cannot attach.

### D5 — Diagnostics gate

`App` owns `std::vector<core::SettingsDiagnostic> diagnostics_`. Every `counter`, `gauge`,
`histogram`, `subscribe` and `publish` failure appends one entry with `SettingsSource::Computed`,
the declaration name and the failing code.

The instrument/port methods return existing library handles rather than new public wrapper types:

- `counter`/`gauge`/`histogram` return the provider-owned pointer or `nullptr`;
- `publish<T>` returns a borrowed `com::EventProducer<T>*` owned by `App`;
- `subscribe<T>` stores the channel/consumer inside `App` and returns `bool`.

`run()` scans the diagnostics first and returns `InvalidArgument` without entering the loop when any
entry is an error. The caller can print `app->diagnostics()`. This keeps each declaration one line
while still refusing to run with a partially constructed application.

Alternative considered: a no-op handle with `valid()`. Rejected as the primary mechanism because it
could silently drop events; the gate makes failure visible before any callback runs.

### D6 — One input and optional output

`subscribe<T>(service, callback)` creates one `com::EventChannel<T>` and one
`com::EventConsumer<T, callback, loss>` stored behind a small type-erased
`std::unique_ptr<SubscriptionBase>`. The base exposes the `IEventConsumer&` needed by
`EventConsumerSet<1>`; the channel is kept alive beside the consumer.

`publish<T>(service)` creates an `EventChannel<T>`, then an `EventProducer<T>` with the application
`app_id`, stores both in a type-erased slot, and returns a borrowed producer pointer. Both the input
and output headers carry the same `app_id`.

A second `subscribe` call is a diagnostic error in the first slice; the public API can grow to
`EventConsumerSet<N>` later without changing the one-input call shape.

### D7 — Run loop

`run()` uses `EventConsumerSet<1>` with a 100 ms `wait_for` timeout. On every wake-up the set drains
the input and invokes the callback; on timeout the loop re-checks the infrastructure stop request and
time-base state. This bounded wait is mandatory: a `wait()` without timeout would not observe
`SIGTERM`/`SIGHUP` while idle.

If `wait_for` fails, `App` records the error and stops the loop with `Requested`; `run()` returns the
recorded failure instead of pretending the application stopped normally. No callback runs if the
diagnostics gate failed.

### D8 — Signals, SIGPIPE and exit codes

`SIGHUP` is added to the existing `signalfd` mask and mapped to `InfrastructureStopReason::Sighup`.
`SIGINT`, `SIGTERM` and `SIGHUP` are graceful stops; `TimeBaseLost` remains exit code 75.

`App` installs `SIGPIPE = SIG_IGN` with a save/restore guard during `create`, before any logger or
port writes. `shutdown()` restores the previous `sigaction` only after `Infrastructure::shutdown()`
has drained the logger and sinks. The guard also restores the disposition when `create` fails.

Exit code helper:

| Reason | Code |
|---|---:|
| `Requested`, `Sigint`, `Sigterm`, `Sighup` | 0 |
| `TimeBaseLost` | 75 |
| `None` or `run()` error | 1 |

`App` documents that it must be created on the thread that will call `run()` / `shutdown()`, because
`Infrastructure` blocks termination signals on the calling thread and only one `App` may own the
process-global signal state.

### D9 — Node ownership

`App` owns one `com::Node` for event channels and producers. The telemetry policy remains
responsible for its own node(s): `Iceoryx2Policy` uses its process-wide shared node and the logger
owns its internal node. This means a facade application may use up to three iceoryx2 nodes; the
machine-wide budget is 20, so a three-application suite stays within it. Reusing one node across the
policy and ports would require a new policy-level node provider and is deferred.

## Risks / Trade-offs

- **Three nodes per application.** → Documented; a future policy/node-provider API can collapse it.
- **`SIGPIPE` is process-global.** → Guarded and restored; `App` must be the only runtime owner in
  the process, which the existing signal-ownership guard already enforces.
- **One input only.** → Explicit first-slice limit; second subscription is a diagnostic error.
- **`std::function` callback storage is cold-path allocation.** → Registration happens before
  `run()`; no callback storage or allocation occurs per event.
- **Long callbacks delay stop.** → Bounded WaitSet timeout only helps between callbacks; callbacks
  must remain short or move heavy work to worker threads.
- **Embedded is not production time.** → Explicit mode and warning; `External` remains the default.
- **SIGHUP is termination, not reload.** → Documented; reload would need a separate app-level
  control path because `Infrastructure` is not hot-reconfigurable.

## Migration Plan

1. Add `Sighup` to the stop-reason vocabulary, mask, watcher and shutdown log; update tests.
2. Move the local time base into the app library and add `TimeBaseMode`.
3. Add `App<Policy>` with lifecycle, diagnostics, metrics and one-input/optional-output ports.
4. Add the quickstart example and migrate `ex_app_wait_loop_iceoryx2` only if it stays within its
   existing example contract.
5. Keep `Infrastructure` and every existing example working; no removal or wire/API break.
