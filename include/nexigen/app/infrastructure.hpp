// nexigen::app — heap-only infrastructure service (change
// rework-app-as-infrastructure; spec app/application).
//
// Composition replaces the old CRTP App: an application owns an
// Infrastructure through make_infrastructure<Policy>() and uses it as a
// provider of logging, metrics and tracing. The constructor is private, so
// automatic/static objects are impossible; the factory allocates once on the
// heap (cold path) and reports allocation failure as ErrorCode::SystemError.
//
// Process-global signal ownership: only one Infrastructure (of any Policy)
// may be initialized at a time. A second initialize returns
// ErrorCode::Conflict; rollback and shutdown release ownership.
#pragma once

#include <atomic>
#include <cerrno>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <new>
#include <optional>
#include <string_view>
#include <thread>
#include <type_traits>
#include <utility>

#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <sys/signalfd.h>
#include <unistd.h>

#include <nexigen/app/app_settings.hpp>
#include <nexigen/app/command_line.hpp>
#include <nexigen/app/infrastructure_stop_reason.hpp>
#include <nexigen/chrono/provider.hpp>
#include <nexigen/chrono/settings.hpp>
#include <nexigen/chrono/worker.hpp>
#include <nexigen/chrono/concepts/sink_concept.hpp>
#include <nexigen/core/log_level.hpp>
#include <nexigen/core/result.hpp>
#include <nexigen/core/thread_management.hpp>
#include <nexigen/core/worker_thread.hpp>
#include <nexigen/logging/concepts/logger_concept.hpp>
#include <nexigen/metrics/concepts/metric_sink_concept.hpp>
#include <nexigen/metrics/metric_provider.hpp>
#include <nexigen/tracing/concepts/tracer_concept.hpp>
#include <nexigen/tracing/tracer_provider.hpp>

namespace nexigen::app {

/// @brief Forward declaration of the heap-only infrastructure service.
///
/// @tparam Policy Stateless policy type satisfying `InfrastructurePolicy`.
/// @tparam TracerCapacity Ring capacity forwarded to `tracing::TracerProvider`.
/// @tparam TracerThreads Per-thread lane count forwarded to the tracer provider.
template <class Policy, std::size_t TracerCapacity = 1024, std::size_t TracerThreads = 32>
class Infrastructure;

/// @brief Stateless policy contract: component types plus static emplacement hooks.
///
/// A policy names the four component types and provides the static hooks that
/// construct them into the infrastructure's optionals. Every hook is `noexcept`
/// and returns `core::Status`; the logger, metrics-sink and trace-sink hooks also
/// receive the application name, while the chrono-sink hook does not. A policy
/// holds no runtime state.
///
/// @tparam Policy Candidate policy type checked for the nested aliases and the
///         four static hooks.
/// @note Spec `app/application`, requirement "Stateless infrastructure policy".
/// @note Cold path only: the hooks run once during `Infrastructure::initialize`.
template <class Policy>
concept InfrastructurePolicy =
    requires(std::optional<typename Policy::Logger>& logger,
             std::optional<typename Policy::MetricsSink>& metrics_sink,
             std::optional<typename Policy::TraceSink>& trace_sink,
             std::optional<typename Policy::Sink>& chrono_sink,
             const logging::Settings& logging_settings, const metrics::Settings& metrics_settings,
             const tracing::Settings& tracing_settings, const chrono::Settings& chrono_settings,
             std::string_view app_name) {
        typename Policy::Logger;
        typename Policy::MetricsSink;
        typename Policy::TraceSink;
        typename Policy::Sink;
        {
            Policy::emplace_logger(logger, logging_settings, app_name)
        } noexcept -> std::same_as<core::Status>;
        {
            Policy::emplace_metrics_sink(metrics_sink, metrics_settings, app_name)
        } noexcept -> std::same_as<core::Status>;
        {
            Policy::emplace_trace_sink(trace_sink, tracing_settings, app_name)
        } noexcept -> std::same_as<core::Status>;
        {
            Policy::emplace_chrono_sink(chrono_sink, chrono_settings)
        } noexcept -> std::same_as<core::Status>;
    };

namespace detail {

/// @brief Non-template process-global signal ownership token.
///
/// Holds the address of the one initialized `Infrastructure` (of any policy),
/// or null. `alignas(64)` keeps the contended CAS off neighbouring state; this
/// is control-plane state only, never touched by a tick or sink.
///
/// @note Spec `app/application`, requirement "Process-global signal ownership
///       guard". Acquisition and release are cold path only.
alignas(64) inline std::atomic<const void*> signal_owner{nullptr};

/// @brief Dedicated `signalfd` watcher translating signals into stop requests.
///
/// The owning `Infrastructure` blocks `SIGINT`/`SIGTERM` before any worker
/// starts; this watcher is then the only consumer of those signals and calls
/// `Target::request_stop` with `Sigint` or `Sigterm`. Application code never
/// runs here. The type is non-copyable and non-movable because it directly owns
/// a signalfd descriptor and one `core::WorkerThread`; the worker is the last
/// member so it joins before the descriptor closes.
///
/// @note Spec `app/application`, requirement "SIGINT and SIGTERM request a
///       graceful stop".
/// @note Cold path only: one watcher thread that polls every 100 ms and runs no
///       application callbacks.
class SignalWatcher {
public:
    /// @brief Construct an inactive watcher (no descriptor, no thread).
    SignalWatcher() noexcept = default;

    /// @brief Copying is impossible: the watcher owns a descriptor and a thread.
    SignalWatcher(const SignalWatcher&) = delete;

    /// @brief Copy assignment is impossible: the watcher owns a descriptor and a thread.
    SignalWatcher& operator=(const SignalWatcher&) = delete;

    /// @brief Moving is impossible: the watcher owns a descriptor and a thread.
    SignalWatcher(SignalWatcher&&) = delete;

    /// @brief Move assignment is impossible: the watcher owns a descriptor and a thread.
    SignalWatcher& operator=(SignalWatcher&&) = delete;

    /// @brief Stop and join the watcher, then close the descriptor.
    ~SignalWatcher() { stop(); }

    /// @brief Create the signalfd and start the watcher thread.
    ///
    /// A second start while a watcher is active or joinable is rejected before
    /// a descriptor or thread is created. On a worker-start failure the
    /// descriptor and dispatch state are rolled back.
    ///
    /// @tparam Target Type providing `request_stop(InfrastructureStopReason)`.
    /// @param target Receiver of the translated stop request; must outlive the
    ///        watcher.
    /// @param mask Signal mask to consume; the caller must already have blocked
    ///        `SIGINT`/`SIGTERM` (the watcher blocks nothing itself).
    /// @param spec Thread specification applied by the watcher on its own
    ///        thread before polling; an empty spec preserves inheritance.
    /// @return OK when the thread started; `Conflict` when a watcher is already
    ///         active or joinable; `SystemError` when `signalfd` fails; the
    ///         worker's status when the thread cannot start.
    /// @note Spec `app/application`, requirement "SIGINT and SIGTERM request a
    ///       graceful stop".
    /// @note Spec `app/settings`, requirement "Application settings carry the
    ///       signal-watcher thread spec".
    /// @warning `Target` and `mask` must outlive the watcher; the mask must be
    ///          blocked by the caller before any worker thread exists.
    template <class Target>
    [[nodiscard]] core::Status start(Target* target, const sigset_t& mask,
                                     const core::ThreadSpec& spec) noexcept {
        // Double-start guard (app/application "the second call returns
        // Conflict and installs no watcher"; analysis #22): a second
        // signalfd would leak a descriptor and a second watcher thread.
        if (active() || worker_.joinable()) {
            return std::unexpected(core::ErrorCode::Conflict);
        }
        fd_ = ::signalfd(-1, &mask, SFD_CLOEXEC | SFD_NONBLOCK);
        if (fd_ < 0) {
            return std::unexpected(core::ErrorCode::SystemError);
        }
        target_ = target;
        dispatch_ = [](void* raw, SignalWatcher& self, std::stop_token token) noexcept {
            self.run<Target>(static_cast<Target*>(raw), token);
        };
        const core::Status started = worker_.start(spec, Body{this});
        if (!started) {
            target_ = nullptr;
            dispatch_ = nullptr;
            static_cast<void>(::close(fd_));
            fd_ = -1;
            return started;
        }
        return {};
    }

    /// @brief Request a stop, join the watcher thread, close the descriptor.
    ///
    /// Safe to call when the watcher is inactive; the watcher does not release
    /// process-global signal ownership, which remains the infrastructure's job.
    ///
    /// @note Spec `app/application`, requirement "Shutdown is idempotent and
    ///       flushes in order".
    void stop() noexcept {
        worker_.stop();
        if (fd_ >= 0) {
            static_cast<void>(::close(fd_));
            fd_ = -1;
        }
        target_ = nullptr;
        dispatch_ = nullptr;
    }

    /// @brief Whether a signalfd is currently installed.
    ///
    /// @return `true` while a descriptor is open (started and not yet stopped).
    [[nodiscard]] bool active() const noexcept { return fd_ >= 0; }

    /// @brief Whether the watcher thread published its readiness snapshot.
    ///
    /// @return The worker's `ready()` flag.
    /// @note Spec `core/worker-thread`, requirement "Start publishes a readable
    ///       readiness snapshot".
    [[nodiscard]] bool thread_ready() const noexcept { return worker_.ready(); }

    /// @brief Diagnostic affinity snapshot of the watcher thread.
    ///
    /// @return The legacy 64-bit mask view of the applied affinity; CPUs above
    ///         63 are dropped by that diagnostic form.
    /// @note Spec `core/worker-thread`, requirement "WorkerThread keeps its
    ///       lifetime contract observable".
    [[nodiscard]] std::uint64_t thread_mask() const noexcept { return worker_.mask(); }

    /// @brief Startup attribute report of the watcher thread.
    ///
    /// @return The published `ThreadApplyReport`; safe to read after `start`.
    /// @note Spec `core/thread-management`, requirement "ThreadApplyReport is a
    ///       serializable startup snapshot".
    [[nodiscard]] const core::ThreadApplyReport& thread_report() const noexcept {
        return worker_.report();
    }

private:
    /// @brief Type-erased dispatch shim binding `Target` to the worker body.
    using Dispatch = void (*)(void*, SignalWatcher&, std::stop_token) noexcept;

    /// @brief Worker body invoking the bound `Target` dispatch once per thread.
    struct Body {
        SignalWatcher* self{nullptr}; ///< Owning watcher; outlived by the worker.

        /// @brief Worker entry point; forwards to the bound `Target` dispatch.
        ///
        /// @param token Stop token owned by the worker thread.
        void run(std::stop_token token) noexcept { self->dispatch_(self->target_, *self, token); }
    };

    /// @brief Poll the signalfd until the stop token is requested.
    ///
    /// A 100 ms poll timeout keeps the loop stop-aware without another
    /// descriptor. Only `SIGINT`/`SIGTERM` map to a stop reason; a short read or
    /// timeout simply re-checks the token.
    ///
    /// @tparam Target Receiver of `request_stop`.
    /// @param target Receiver bound at `start`.
    /// @param token Stop token owned by the worker thread.
    template <class Target> void run(Target* target, std::stop_token token) noexcept {
        while (!token.stop_requested()) {
            pollfd descriptor{fd_, POLLIN, 0};
            const int ready = ::poll(&descriptor, 1, 100);
            if (ready <= 0) {
                continue; // timeout: re-check the stop token
            }
            signalfd_siginfo info{};
            const ssize_t got = ::read(fd_, &info, sizeof info);
            if (got != static_cast<ssize_t>(sizeof info)) {
                continue;
            }
            if (info.ssi_signo == static_cast<std::uint32_t>(SIGINT)) {
                target->request_stop(InfrastructureStopReason::Sigint);
            } else if (info.ssi_signo == static_cast<std::uint32_t>(SIGTERM)) {
                target->request_stop(InfrastructureStopReason::Sigterm);
            }
        }
    }

    int fd_{-1};
    void* target_{nullptr};
    Dispatch dispatch_{nullptr};
    core::WorkerThread<Body> worker_{}; ///< LAST member: joins before fd_ closes
};

/// @brief Bridges the application logger to the chrono worker's log hook.
///
/// Chrono must not depend on logging, so the worker reports through a
/// module-local hook; this adapter forwards a pre-formatted message through the
/// application logger at the matching `core::LogLevel`. The pointer is borrowed
/// and must outlive the chrono worker.
///
/// @tparam Logger Logger type satisfying `logging::concepts::Logger`.
/// @note Spec `app/application`, requirement "Initialization follows a fixed
///       order"; the bridge lives on the cold worker path only.
template <class Logger> struct ChronoLoggerBridge {
    Logger* logger{nullptr}; ///< Borrowed application logger (non-null while running).

    /// @brief Forward one worker message at the equivalent log level.
    ///
    /// @param level Severity reported by the chrono worker; `Off` produces no
    ///        record.
    /// @param message Pre-formatted message forwarded as a `{}` argument.
    /// @warning Only valid while `logger` points at a live logger, i.e. while
    ///          the chrono worker started by the infrastructure is running.
    void log(core::LogLevel level, std::string_view message) noexcept {
        switch (level) {
            case core::LogLevel::Verbose: logger->verbose("{}", message); break;
            case core::LogLevel::Debug:   logger->debug("{}", message); break;
            case core::LogLevel::Info:    logger->info("{}", message); break;
            case core::LogLevel::Warning: logger->warning("{}", message); break;
            case core::LogLevel::Error:   logger->error("{}", message); break;
            case core::LogLevel::Fatal:   logger->fatal("{}", message); break;
            case core::LogLevel::Off:     break; // filtered: no record
        }
    }
};

} // namespace detail

/// @brief Heap-only infrastructure service composing logging, metrics, tracing and time.
///
/// An application owns exactly one `Infrastructure` through
/// `make_infrastructure<Policy>()` and uses it as the provider of logging,
/// metrics, tracing and chrono. The constructor is private, so automatic and
/// static objects are impossible; the factory allocates once on the cold path.
/// Only one infrastructure (of any policy) may hold process-global signal
/// ownership at a time. The type is non-copyable and non-movable because it owns
/// threads and borrowed component relationships; component members are optional
/// and ordered so that reverse-declaration teardown releases each dependency
/// after its users.
///
/// @tparam Policy Stateless policy type constrained by `InfrastructurePolicy`.
/// @tparam TracerCapacity Ring capacity of the `tracing::TracerProvider`.
/// @tparam TracerThreads Per-thread lane count of the tracer provider.
/// @note Spec `app/application`, requirement "Heap-only infrastructure factory".
/// @note Spec `app/application`, requirement "Stateless infrastructure policy".
/// @note Spec `app/application`, requirement "Initialization follows a fixed
///       order".
template <class Policy, std::size_t TracerCapacity, std::size_t TracerThreads>
class Infrastructure {
public:
    /// @brief Logger component type produced by `Policy::emplace_logger`.
    using Logger = typename Policy::Logger;
    /// @brief Metrics sink type produced by `Policy::emplace_metrics_sink`.
    using MetricsSink = typename Policy::MetricsSink;
    /// @brief Trace sink type produced by `Policy::emplace_trace_sink`.
    using TraceSink = typename Policy::TraceSink;
    /// @brief Chrono sink type produced by `Policy::emplace_chrono_sink`.
    using Sink = typename Policy::Sink;
    /// @brief Tracer provider owned by this infrastructure.
    using TracerProvider = tracing::TracerProvider<TracerCapacity, TracerThreads>;
    /// @brief Tracer handle type returned by `tracer_for`.
    using TracerType = tracing::Tracer<TracerCapacity, TracerThreads>;

    static_assert(InfrastructurePolicy<Policy>,
                  "Policy must declare Logger/MetricsSink/TraceSink/Sink and static "
                  "emplace hooks");
    static_assert(logging::concepts::Logger<Logger>,
                  "Policy::Logger must satisfy nexigen::logging::concepts::Logger");
    static_assert(metrics::concepts::MetricSink<MetricsSink>,
                  "Policy::MetricsSink must satisfy metrics::concepts::MetricSink");
    static_assert(tracing::concepts::TraceSink<TraceSink>,
                  "Policy::TraceSink must satisfy tracing::concepts::TraceSink");
    static_assert(chrono::concepts::Sink<Sink>, "Policy::Sink must satisfy chrono::concepts::Sink");

    /// @brief Copying is impossible: the infrastructure owns threads and signals.
    Infrastructure(const Infrastructure&) = delete;
    /// @brief Copy assignment is impossible: the infrastructure owns threads and signals.
    Infrastructure& operator=(const Infrastructure&) = delete;
    /// @brief Moving is impossible: the infrastructure owns threads and signals.
    Infrastructure(Infrastructure&&) = delete;
    /// @brief Move assignment is impossible: the infrastructure owns threads and signals.
    Infrastructure& operator=(Infrastructure&&) = delete;

    /// @brief Shut down when initialization succeeded; otherwise do nothing.
    ///
    /// Faults are swallowed because a destructor cannot report them; call
    /// `shutdown()` explicitly to observe the flush status.
    ~Infrastructure() {
        if (initialized_) {
            static_cast<void>(shutdown());
        }
    }

    /// @brief Claim signals, build components, and start the watcher.
    ///
    /// Claims process-global signal ownership, blocks `SIGINT`/`SIGTERM`, builds
    /// the chrono provider, logger, chrono worker, metrics provider and tracer
    /// provider in that order, then starts the signal watcher. A failure at any
    /// step rolls back partial state, restores the previous mask, releases
    /// ownership and returns the failure status. Repeated calls on this instance
    /// are idempotent.
    ///
    /// @param settings Merged settings applied to every component.
    /// @return OK when initialized (or already initialized); `Conflict` when
    ///         another infrastructure owns the signals; `NotInitialized` after
    ///         `shutdown()` began; `SystemError` on a mask or required
    ///         thread-attribute failure; otherwise the failing component status.
    /// @note Spec `app/application`, requirement "Initialization follows a fixed
    ///       order".
    /// @note Spec `app/application`, requirement "Process-global signal
    ///       ownership guard".
    /// @note Spec `app/application`, scenario "Required worker-attribute failure
    ///       starts no non-RT fallback".
    /// @note Cold path only; never called from a tick.
    [[nodiscard]] core::Status initialize(const Settings& settings) noexcept {
        if (initialized_) {
            return {};
        }
        if (shutdown_started_) {
            return std::unexpected(core::ErrorCode::NotInitialized);
        }

        const void* no_owner = nullptr;
        if (!detail::signal_owner.compare_exchange_strong(no_owner, static_cast<const void*>(this),
                                                          std::memory_order_acq_rel,
                                                          std::memory_order_relaxed)) {
            return std::unexpected(core::ErrorCode::Conflict);
        }

        sigset_t mask;
        sigemptyset(&mask);
        sigaddset(&mask, SIGINT);
        sigaddset(&mask, SIGTERM);
        if (::pthread_sigmask(SIG_BLOCK, &mask, &previous_mask_) != 0) {
            detail::signal_owner.store(nullptr, std::memory_order_release);
            return std::unexpected(core::ErrorCode::SystemError);
        }

        // Build order: chrono is the time foundation (construct it deferred,
        // then the logger, then bind the logger and start the chrono worker),
        // then metrics, then tracing, then the signal watcher.
        if (const core::Status step = build_time_and_logging(settings); !step) {
            return step;
        }
        if (const core::Status step = build_metrics(settings); !step) {
            return step;
        }
        if (const core::Status step = build_tracing(settings); !step) {
            return step;
        }

        if (const core::Status status = signals_.start(this, mask, settings.signal_thread);
            !status) {
            return fail(status);
        }

        initialized_ = true;
        return {};
    }

    /// @brief Load command-line settings and initialize through the existing path.
    ///
    /// Loading is fail-fast: on a loader diagnostic this returns the failure
    /// without claiming signals or creating components, while `loaded` keeps the
    /// best-effort settings and diagnostics. Help is a caller action and is
    /// rejected as an initialization input.
    ///
    /// @param options Parsed command line; a set help flag is rejected.
    /// @param loaded Destination carrying the loaded settings even on failure.
    /// @return `InvalidArgument` when help was requested or no settings directory
    ///         was supplied; otherwise the loader status; otherwise the result of
    ///         `initialize(loaded)`.
    /// @note Spec `app/application`, requirement "Initialization follows a fixed
    ///       order".
    /// @note Spec `app/command-line`, requirement "At least one settings
    ///       directory is required".
    /// @note Cold path only.
    [[nodiscard]] core::Status initialize(const CommandLineOptions& options,
                                          Settings& loaded) noexcept {
        if (options.help) {
            return std::unexpected(core::ErrorCode::InvalidArgument);
        }
        Settings settings{};
        const core::Status status = load_app_settings(options, settings);
        loaded = std::move(settings);
        if (!status) {
            return status;
        }
        return initialize(loaded);
    }

    /// @brief Run the caller's tick until a stop is requested.
    ///
    /// A bool-returning tick requests a host stop when it yields false and the
    /// loop stops before calling it again; a void tick runs until a signal or an
    /// explicit `request_stop` call. The loop performs no allocation and no
    /// locking.
    ///
    /// @tparam Tick Callable invoked as `tick()`; a `bool` return value makes it
    ///         a host-stop source.
    /// @param tick Callback executed once per iteration on the caller's thread.
    /// @return The first recorded stop reason, or `None` if the loop never saw a
    ///         request.
    /// @note Spec `app/application`, requirement "Run loop observes the stop
    ///       request".
    /// @note Signal delivery is observed through the watcher thread's atomic
    ///       stop reason, not through the caller's signal mask.
    template <class Tick> [[nodiscard]] InfrastructureStopReason run(Tick&& tick) noexcept {
        while (!stop_requested()) {
            if constexpr (std::is_invocable_r_v<bool, Tick&>) {
                if (!tick()) {
                    request_stop(InfrastructureStopReason::Requested);
                    break;
                }
            } else {
                tick();
            }
        }
        return stop_reason();
    }

    /// @brief Record the first stop cause; later requests leave it unchanged.
    ///
    /// `None` is ignored so it can never overwrite a real cause. Safe to call
    /// from the signal watcher and the host thread concurrently.
    ///
    /// @param reason Cause to record; ignored when `None`.
    /// @note Spec `app/application`, scenario "First stop reason wins".
    void request_stop(InfrastructureStopReason reason) noexcept {
        if (reason == InfrastructureStopReason::None) {
            return;
        }
        InfrastructureStopReason expected = InfrastructureStopReason::None;
        static_cast<void>(stop_reason_.compare_exchange_strong(
            expected, reason, std::memory_order_acq_rel, std::memory_order_relaxed));
    }

    /// @brief Whether any stop cause has been recorded.
    ///
    /// @return `true` once a non-`None` reason is visible.
    /// @note Spec `app/application`, requirement "Run loop observes the stop
    ///       request". Acquire load; allocation-free and lock-free.
    [[nodiscard]] bool stop_requested() const noexcept {
        return stop_reason_.load(std::memory_order_acquire) != InfrastructureStopReason::None;
    }

    /// @brief The recorded stop cause.
    ///
    /// @return The first reason recorded, or `None` when no stop was requested.
    /// @note Spec `app/application`, scenario "First stop reason wins".
    [[nodiscard]] InfrastructureStopReason stop_reason() const noexcept {
        return stop_reason_.load(std::memory_order_acquire);
    }

    /// @brief Stop the watcher, flush every component, and release ownership.
    ///
    /// Stops and joins the watcher, drains the logger, logs exactly one final
    /// record naming the stop reason, flushes the tracer and metrics, then lets
    /// the chrono worker deliver its final snapshot and flush. Afterwards the
    /// caller's previous signal mask is restored, process-global ownership is
    /// released, and components are destroyed in reverse dependency order.
    /// Repeated calls after the first perform no work.
    ///
    /// @return OK, or the first flush failure encountered; the remaining flushes
    ///         still run.
    /// @note Spec `app/application`, requirement "Shutdown is idempotent and
    ///       flushes in order".
    /// @note The logger is drained both before and after the final record, which
    ///       is what makes the "every accepted record is flushed" guarantee hold
    ///       for a full ring.
    [[nodiscard]] core::Status shutdown() noexcept {
        if (!initialized_) {
            return {};
        }
        bool expected = false;
        if (!shutdown_started_.compare_exchange_strong(expected, true)) {
            return {};
        }

        signals_.stop();

        core::Status first{};
        const auto capture = [&first](core::Status status) noexcept {
            if (first && !status) {
                first = status;
            }
        };

        // Flush order: the logger is drained first (so the final shutdown
        // record is not dropped by a full ring), the reason is logged, and the
        // logger is flushed again before tracer/metrics/chrono. This deviates
        // from the spec's textual "tracer → metrics → logger → chrono" order
        // deliberately: the logger must be drained both before and after the
        // final record for the "every accepted record is flushed" guarantee.
        if (logger_) {
            capture(logger_->flush()); // drain first so the final record fits
            switch (stop_reason()) {
                case InfrastructureStopReason::Sigint:
                    logger_->warning("shutdown reason: sigint (2)");
                    break;
                case InfrastructureStopReason::Sigterm:
                    logger_->warning("shutdown reason: sigterm (15)");
                    break;
                case InfrastructureStopReason::Requested:
                    logger_->info("shutdown reason: requested");
                    break;
                case InfrastructureStopReason::None:
                default:                             logger_->info("shutdown reason: none"); break;
            }
            capture(logger_->flush());
        }

        if (tracer_) {
            capture(tracer_->flush());
        }
        if (metrics_) {
            capture(metrics_->collect_now());
            capture(metrics_->flush());
        }
        // The chrono worker's destructor stops the thread and delivers the
        // final snapshot + flush; reset it first so the provider outlives it.
        worker_.reset();

        static_cast<void>(::pthread_sigmask(SIG_SETMASK, &previous_mask_, nullptr));

        tracer_.reset();
        trace_sink_.reset();
        metrics_.reset();
        metrics_sink_.reset();
        provider_.reset();
        chrono_sink_.reset();
        logger_.reset();
        initialized_ = false;
        detail::signal_owner.store(nullptr, std::memory_order_release);
        return first;
    }

    /// @brief Access the application logger.
    ///
    /// @return Reference to the logger built at initialization; every record
    ///         goes through it and is flushed on shutdown.
    /// @warning Only valid after a successful `initialize`; the returned handle
    ///          stays valid while this infrastructure is alive.
    /// @note Spec `app/application`, requirement "Infrastructure exposes logger,
    ///       metric and tracer factories".
    [[nodiscard]] Logger& logger() noexcept { return *logger_; }

    /// @brief Access the metrics provider.
    ///
    /// @return Reference to the provider built at initialization.
    /// @warning Only valid after a successful `initialize`; instruments created
    ///          from it are owned by this provider.
    /// @note Spec `app/application`, requirement "Infrastructure exposes logger,
    ///       metric and tracer factories".
    [[nodiscard]] metrics::MetricProvider& metrics() noexcept { return *metrics_; }

    /// @brief Access the tracer provider.
    ///
    /// @return Reference to the provider built at initialization.
    /// @warning Only valid after a successful `initialize`.
    /// @note Spec `app/application`, requirement "Infrastructure exposes logger,
    ///       metric and tracer factories".
    [[nodiscard]] TracerProvider& tracer() noexcept { return *tracer_; }

    /// @brief Access the chrono provider.
    ///
    /// @return Reference to the provider built at initialization.
    /// @warning Only valid after a successful `initialize`; snapshots are
    ///          delivered through the configured chrono sink and flushed on
    ///          shutdown.
    /// @note Spec `app/application`, requirement "Infrastructure exposes logger,
    ///       metric and tracer factories".
    [[nodiscard]] chrono::Provider<>& chrono() noexcept { return *provider_; }

    /// @brief Create or fetch a meter through the metrics provider.
    ///
    /// @param name Meter name.
    /// @param version Meter version string.
    /// @return The meter handle on success; `NotInitialized` before a successful
    ///         `initialize`, otherwise the provider's status.
    /// @note Spec `app/application`, requirement "Infrastructure exposes logger,
    ///       metric and tracer factories".
    [[nodiscard]] core::Result<metrics::Meter*> meter(std::string_view name,
                                                      std::string_view version) noexcept {
        if (!metrics_) {
            return std::unexpected(core::ErrorCode::NotInitialized);
        }
        return metrics_->get_meter(name, version);
    }

    /// @brief Create or fetch a tracer through the tracer provider.
    ///
    /// @param scope Tracer scope name.
    /// @return The tracer handle on success; `NotInitialized` before a successful
    ///         `initialize`, otherwise the provider's status.
    /// @note Spec `app/application`, requirement "Infrastructure exposes logger,
    ///       metric and tracer factories".
    [[nodiscard]] core::Result<TracerType*> tracer_for(std::string_view scope) noexcept {
        if (!tracer_) {
            return std::unexpected(core::ErrorCode::NotInitialized);
        }
        return tracer_->get_tracer(scope);
    }

private:
    /// @brief Private constructor: only `make_infrastructure` may allocate one.
    Infrastructure() noexcept = default;

    /// @brief Factory access to the private constructor.
    ///
    /// @tparam P Policy of the infrastructure being allocated.
    /// @tparam C Tracer ring capacity.
    /// @tparam T Traced thread-lane count.
    /// @return The allocated infrastructure, or `SystemError` on allocation
    ///         failure.
    template <class P, std::size_t C, std::size_t T>
    friend core::Result<std::unique_ptr<Infrastructure<P, C, T>>> make_infrastructure() noexcept;

    /// @brief Roll back a partially built infrastructure and surface the status.
    ///
    /// @param status Failure to return after the rollback.
    /// @return `status` unchanged.
    /// @note Spec `app/application`, scenario "Failure rolls back partial
    ///       initialization".
    [[nodiscard]] core::Status fail(core::Status status) noexcept {
        rollback();
        return status;
    }

    /// @brief Shared guard for one policy emplacement step.
    ///
    /// Every policy hook either constructs a component or fails: a failing
    /// status rolls back; a success that left the optional empty (an
    /// invalid-policy stub returning a plain null) maps to `InvalidArgument` and
    /// also rolls back.
    ///
    /// @param status Result returned by the policy hook.
    /// @param constructed Whether the hook actually populated the optional.
    /// @return OK when the step produced a component; otherwise the hook status
    ///         or `InvalidArgument`, after rollback.
    [[nodiscard]] core::Status emplace_checked(core::Status status, bool constructed) noexcept {
        if (!status) {
            return fail(status);
        }
        if (!constructed) {
            return fail(std::unexpected(core::ErrorCode::InvalidArgument));
        }
        return {};
    }

    /// @brief Build the chrono sink/provider, logger, bridge and chrono worker.
    ///
    /// Chrono is the time foundation, so its sink and provider are constructed
    /// first (the worker is not started yet); the logger is configured next, the
    /// bridge is bound, and only then does the chrono worker start.
    ///
    /// @param settings Merged settings supplying the chrono and logging
    ///        configuration plus the application name.
    /// @return OK when all four pieces exist; otherwise the failing status after
    ///         rollback.
    /// @note Spec `app/application`, scenario "Logger is ready before providers
    ///       start".
    /// @note Spec `app/settings`, requirement "Application name is app-level".
    [[nodiscard]] core::Status build_time_and_logging(const Settings& settings) noexcept {
        const core::Status chrono_status =
            Policy::emplace_chrono_sink(chrono_sink_, settings.chrono);
        if (const core::Status step = emplace_checked(chrono_status, chrono_sink_.has_value());
            !step) {
            return step;
        }
        provider_.emplace(settings.chrono.options, *chrono_sink_);

        const core::Status logger_status =
            Policy::emplace_logger(logger_, settings.logging, settings.app_name);
        if (const core::Status step = emplace_checked(logger_status, logger_.has_value()); !step) {
            return step;
        }
        chrono_logger_bridge_.logger = &*logger_;
        worker_.emplace(*provider_, settings.chrono.options);
        const core::Status started = worker_->start(chrono_logger_bridge_);
        if (!started) {
            return fail(started);
        }
        return {};
    }

    /// @brief Build the metrics sink and provider, applying the startup gate.
    ///
    /// When the provider's worker thread spec uses `ThreadApplyMode::Required`,
    /// the provider's startup status must be OK or the step fails and rolls back;
    /// otherwise a best-effort partial application is accepted.
    ///
    /// @param settings Merged metrics settings plus the application name.
    /// @return OK when the provider exists and any required startup check
    ///         passed; otherwise the failing status after rollback.
    /// @note Spec `app/application`, scenario "Required worker-attribute failure
    ///       starts no non-RT fallback".
    /// @note Spec `app/application`, scenario "Best-effort partial application
    ///       does not trigger rollback".
    [[nodiscard]] core::Status build_metrics(const Settings& settings) noexcept {
        const core::Status sink_status =
            Policy::emplace_metrics_sink(metrics_sink_, settings.metrics, settings.app_name);
        if (const core::Status step = emplace_checked(sink_status, metrics_sink_.has_value());
            !step) {
            return step;
        }
        metrics_.emplace(settings.metrics.options, *metrics_sink_);
        if (settings.metrics.options.worker_thread.apply_mode() ==
            core::ThreadApplyMode::Required) {
            const core::Status startup = metrics_->startup_status();
            if (!startup) {
                return fail(startup);
            }
        }
        return {};
    }

    /// @brief Build the trace sink and initialize the tracer provider.
    ///
    /// @param settings Merged tracing settings plus the application name.
    /// @return OK when the provider initialized; otherwise the failing status
    ///         after rollback.
    /// @note Spec `app/application`, scenario "Failure rolls back partial
    ///       initialization".
    [[nodiscard]] core::Status build_tracing(const Settings& settings) noexcept {
        const core::Status sink_status =
            Policy::emplace_trace_sink(trace_sink_, settings.tracing, settings.app_name);
        if (const core::Status step = emplace_checked(sink_status, trace_sink_.has_value());
            !step) {
            return step;
        }
        tracer_.emplace(settings.tracing.options, *trace_sink_);
        const core::Status started = tracer_->initialize();
        if (!started) {
            return fail(started);
        }
        return {};
    }

    /// @brief Tear down every partially built component and release ownership.
    ///
    /// Stops the watcher first, then destroys providers, sinks, the chrono
    /// worker and the logger in reverse dependency order; restores the previous
    /// signal mask and clears process-global signal ownership.
    ///
    /// @note Spec `app/application`, scenario "Failure rolls back partial
    ///       initialization".
    /// @note Safe on a partially built instance: every component is an optional
    ///       and every release is idempotent.
    void rollback() noexcept {
        signals_.stop();
        tracer_.reset();
        trace_sink_.reset();
        metrics_.reset();
        metrics_sink_.reset();
        worker_.reset();
        provider_.reset();
        chrono_sink_.reset();
        logger_.reset();
        static_cast<void>(::pthread_sigmask(SIG_SETMASK, &previous_mask_, nullptr));
        initialized_ = false;
        detail::signal_owner.store(nullptr, std::memory_order_release);
    }

    // Declaration order is the lifetime contract: the worker references the
    // provider, which references the chrono sink, so reverse-declaration
    // teardown destroys worker -> provider -> sink. The chrono worker is
    // declared after the logger/sink bridge it references, so the bridge, the
    // chrono sink, and the logger all outlive the chrono worker.
    std::optional<Logger> logger_{};                            ///< Logger component, built first.
    detail::ChronoLoggerBridge<Logger> chrono_logger_bridge_{}; ///< Borrows `logger_`.
    std::optional<MetricsSink> metrics_sink_{};                 ///< Metrics sink component.
    std::optional<metrics::MetricProvider> metrics_{};          ///< Metrics provider over the sink.
    std::optional<TraceSink> trace_sink_{};                     ///< Trace sink component.
    std::optional<TracerProvider> tracer_{};                    ///< Tracer provider over the sink.
    std::optional<Sink> chrono_sink_{};                         ///< Chrono sink component.
    std::optional<chrono::Provider<>> provider_{};              ///< Chrono provider over the sink.
    std::optional<chrono::ChronoWorker<chrono::Provider<>>> worker_{}; ///< Chrono worker.
    detail::SignalWatcher signals_{}; ///< Signal watcher, stopped first on teardown.

    sigset_t previous_mask_{};                  ///< Mask restored by shutdown/rollback.
    bool initialized_{false};                   ///< Set once every component and the watcher exist.
    std::atomic<bool> shutdown_started_{false}; ///< One-shot shutdown latch.
    /// @brief First recorded stop cause; cache-line isolated from the latch.
    alignas(64) std::atomic<InfrastructureStopReason> stop_reason_{InfrastructureStopReason::None};
};

/// @brief Heap-only factory for an `Infrastructure`.
///
/// The private constructor makes automatic and static `Infrastructure` objects
/// impossible: this factory is the only construction path and it allocates once
/// on the cold path.
///
/// @tparam Policy Stateless policy type satisfying `InfrastructurePolicy`.
/// @tparam TracerCapacity Ring capacity of the tracer provider.
/// @tparam TracerThreads Per-thread lane count of the tracer provider.
/// @return A fully constructed infrastructure on the heap, or `SystemError` when
///         the allocation fails (never `std::terminate`).
/// @note Spec `app/application`, requirement "Heap-only infrastructure factory".
/// @note The returned pointer must be initialized before use and destroyed
///       before the process exits so `shutdown()` can flush.
template <class Policy, std::size_t TracerCapacity = 1024, std::size_t TracerThreads = 32>
[[nodiscard]] core::Result<std::unique_ptr<Infrastructure<Policy, TracerCapacity, TracerThreads>>>
make_infrastructure() noexcept {
    using Type = Infrastructure<Policy, TracerCapacity, TracerThreads>;
    Type* const raw = new (std::nothrow) Type{};
    if (raw == nullptr) {
        return std::unexpected(core::ErrorCode::SystemError);
    }
    return std::unique_ptr<Type>{raw};
}

} // namespace nexigen::app
