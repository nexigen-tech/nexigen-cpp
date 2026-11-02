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
#include <nexigen/chrono/chrono_provider.hpp>
#include <nexigen/chrono/chrono_settings.hpp>
#include <nexigen/chrono/concepts/chrono_sink_concept.hpp>
#include <nexigen/core/result.hpp>
#include <nexigen/logging/concepts/logger_concept.hpp>
#include <nexigen/metrics/concepts/metric_sink_concept.hpp>
#include <nexigen/metrics/metric_provider.hpp>
#include <nexigen/tracing/concepts/tracer_concept.hpp>
#include <nexigen/tracing/tracer_provider.hpp>

namespace nexigen::app {

template <class Policy, std::size_t TracerCapacity = 1024,
          std::size_t TracerThreads = 32>
class Infrastructure;

/// Stateless policy contract: component types plus static emplacement hooks.
template <class Policy>
concept InfrastructurePolicy = requires(
    std::optional<typename Policy::Logger>& logger,
    std::optional<typename Policy::MetricsSink>& metrics_sink,
    std::optional<typename Policy::TraceSink>& trace_sink,
    std::optional<typename Policy::ChronoSink>& chrono_sink,
    const logging::Settings& logging_settings,
    const metrics::Settings& metrics_settings,
    const tracing::Settings& tracing_settings,
    const chrono::Settings& chrono_settings,
    std::string_view app_name) {
    typename Policy::Logger;
    typename Policy::MetricsSink;
    typename Policy::TraceSink;
    typename Policy::ChronoSink;
    { Policy::emplace_logger(logger, logging_settings, app_name) } noexcept
        -> std::same_as<core::Status>;
    { Policy::emplace_metrics_sink(metrics_sink, metrics_settings, app_name) } noexcept
        -> std::same_as<core::Status>;
    { Policy::emplace_trace_sink(trace_sink, tracing_settings, app_name) } noexcept
        -> std::same_as<core::Status>;
    { Policy::emplace_chrono_sink(chrono_sink, chrono_settings) } noexcept
        -> std::same_as<core::Status>;
};

namespace detail {

/// Non-template process-global signal ownership. alignas(64) keeps the
/// contended CAS off neighbouring state; control plane only.
alignas(64) inline std::atomic<const void*> signal_owner{nullptr};

/// signalfd watcher: masks are blocked by initialize before any worker
/// starts; this thread is the only consumer of SIGINT/SIGTERM.
class SignalWatcher {
public:
    SignalWatcher() noexcept = default;
    SignalWatcher(const SignalWatcher&) = delete;
    SignalWatcher& operator=(const SignalWatcher&) = delete;
    SignalWatcher(SignalWatcher&&) = delete;
    SignalWatcher& operator=(SignalWatcher&&) = delete;

    ~SignalWatcher() { stop(); }

    template <class Target>
    [[nodiscard]] core::Status start(Target* target, const sigset_t& mask) noexcept {
        fd_ = ::signalfd(-1, &mask, SFD_CLOEXEC | SFD_NONBLOCK);
        if (fd_ < 0) {
            return std::unexpected(core::ErrorCode::SystemError);
        }
        // Under -fno-exceptions a failed thread creation terminates; this is
        // the same cold-path deployment assumption as the providers.
        thread_ = std::jthread{[this, target](std::stop_token token) { run(target, token); }};
        return {};
    }

    void stop() noexcept {
        if (thread_.joinable()) {
            thread_.request_stop();
            thread_.join();
        }
        if (fd_ >= 0) {
            static_cast<void>(::close(fd_));
            fd_ = -1;
        }
    }

    [[nodiscard]] bool active() const noexcept { return fd_ >= 0; }

private:
    template <class Target>
    void run(Target* target, std::stop_token token) noexcept {
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
    std::jthread thread_{};
};

/// Bridges the application logger to the chrono worker's module-local log hook
/// (chrono must not depend on logging): maps ClockLogLevel to LogLevel and
/// forwards a pre-formatted message through the logger.
template <class Logger>
struct ChronoLoggerBridge {
    Logger* logger{nullptr};

    void log(chrono::ClockLogLevel level, std::string_view message) noexcept {
        switch (level) {
            case chrono::ClockLogLevel::info:
                logger->info("{}", message);
                break;
            case chrono::ClockLogLevel::warning:
                logger->warning("{}", message);
                break;
            case chrono::ClockLogLevel::error:
                logger->error("{}", message);
                break;
        }
    }
};

} // namespace detail

template <class Policy, std::size_t TracerCapacity, std::size_t TracerThreads>
class Infrastructure {
public:
    using Logger = typename Policy::Logger;
    using MetricsSink = typename Policy::MetricsSink;
    using TraceSink = typename Policy::TraceSink;
    using ChronoSink = typename Policy::ChronoSink;
    using TracerProvider = tracing::TracerProvider<TracerCapacity, TracerThreads>;
    using TracerType = tracing::Tracer<TracerCapacity, TracerThreads>;

    static_assert(InfrastructurePolicy<Policy>,
                  "Policy must declare Logger/MetricsSink/TraceSink/ChronoSink and static "
                  "emplace hooks");
    static_assert(logging::concepts::Logger<Logger>,
                  "Policy::Logger must satisfy nexigen::logging::concepts::Logger");
    static_assert(metrics::concepts::MetricSink<MetricsSink>,
                  "Policy::MetricsSink must satisfy metrics::concepts::MetricSink");
    static_assert(tracing::concepts::TraceSink<TraceSink>,
                  "Policy::TraceSink must satisfy tracing::concepts::TraceSink");
    static_assert(chrono::concepts::ChronoSink<ChronoSink>,
                  "Policy::ChronoSink must satisfy chrono::concepts::ChronoSink");

    Infrastructure(const Infrastructure&) = delete;
    Infrastructure& operator=(const Infrastructure&) = delete;
    Infrastructure(Infrastructure&&) = delete;
    Infrastructure& operator=(Infrastructure&&) = delete;

    ~Infrastructure() {
        if (initialized_) {
            static_cast<void>(shutdown());
        }
    }

    /// Claim signal ownership, block SIGINT/SIGTERM, build the policy
    /// components and providers, start the watcher. Idempotent on this
    /// instance; Conflict when another infrastructure owns the signals.
    [[nodiscard]] core::Status initialize(const Settings& settings) noexcept {
        if (initialized_) {
            return {};
        }
        if (shutdown_started_) {
            return std::unexpected(core::ErrorCode::NotInitialized);
        }

        const void* no_owner = nullptr;
        if (!detail::signal_owner.compare_exchange_strong(
                no_owner, static_cast<const void*>(this), std::memory_order_acq_rel,
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

        const auto fail = [this](core::Status status) noexcept -> core::Status {
            rollback();
            return status;
        };

        // 1. chrono is the time foundation: construct it (deferred worker),
        //    then the logger, then bind the logger and start the chrono worker.
        if (const core::Status status =
                Policy::emplace_chrono_sink(chrono_sink_, settings.chrono);
            !status || !chrono_sink_) {
            return fail(status ? std::unexpected(core::ErrorCode::InvalidArgument) : status);
        }
        chrono_.emplace(settings.chrono.options, *chrono_sink_);

        if (const core::Status status =
                Policy::emplace_logger(logger_, settings.logging, settings.app_name);
            !status || !logger_) {
            return fail(status ? std::unexpected(core::ErrorCode::InvalidArgument) : status);
        }
        chrono_logger_bridge_.logger = &*logger_;
        if (const core::Status status = chrono_->start(chrono_logger_bridge_); !status) {
            return fail(status);
        }

        if (const core::Status status =
                Policy::emplace_metrics_sink(metrics_sink_, settings.metrics, settings.app_name);
            !status || !metrics_sink_) {
            return fail(status ? std::unexpected(core::ErrorCode::InvalidArgument) : status);
        }
        metrics_.emplace(settings.metrics.options, *metrics_sink_);

        if (const core::Status status =
                Policy::emplace_trace_sink(trace_sink_, settings.tracing, settings.app_name);
            !status || !trace_sink_) {
            return fail(status ? std::unexpected(core::ErrorCode::InvalidArgument) : status);
        }
        tracer_.emplace(settings.tracing.options, *trace_sink_);
        if (const core::Status status = tracer_->initialize(); !status) {
            return fail(status);
        }

        if (const core::Status status = signals_.start(this, mask); !status) {
            return fail(status);
        }

        initialized_ = true;
        return {};
    }

    /// Load settings from parsed command-line options and initialize through
    /// the existing path. Loading is fail-fast: on a loader diagnostic this
    /// returns the failure without claiming signals or creating components,
    /// while `loaded` keeps the best-effort settings and diagnostics. Help is a
    /// caller action and is rejected as an initialization input.
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

    /// Run the caller's tick until a stop is requested; returns the reason.
    /// A bool-returning tick requests a host stop when it yields false.
    template <class Tick>
    [[nodiscard]] InfrastructureStopReason run(Tick&& tick) noexcept {
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

    /// First stop cause wins; later requests leave the reason unchanged.
    void request_stop(InfrastructureStopReason reason) noexcept {
        if (reason == InfrastructureStopReason::None) {
            return;
        }
        InfrastructureStopReason expected = InfrastructureStopReason::None;
        static_cast<void>(stop_reason_.compare_exchange_strong(
            expected, reason, std::memory_order_acq_rel, std::memory_order_relaxed));
    }

    [[nodiscard]] bool stop_requested() const noexcept {
        return stop_reason_.load(std::memory_order_acquire) != InfrastructureStopReason::None;
    }

    [[nodiscard]] InfrastructureStopReason stop_reason() const noexcept {
        return stop_reason_.load(std::memory_order_acquire);
    }

    /// Stop the watcher, log the reason once, flush tracer → metrics → logger,
    /// restore the mask, release signal ownership and destroy the components.
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
                default:
                    logger_->info("shutdown reason: none");
                    break;
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
        if (chrono_) {
            capture(chrono_->snapshot_now());
            capture(chrono_->flush());
        }

        static_cast<void>(::pthread_sigmask(SIG_SETMASK, &previous_mask_, nullptr));

        tracer_.reset();
        trace_sink_.reset();
        metrics_.reset();
        metrics_sink_.reset();
        chrono_.reset();
        chrono_sink_.reset();
        logger_.reset();
        initialized_ = false;
        detail::signal_owner.store(nullptr, std::memory_order_release);
        return first;
    }

    /// Precondition: initialized successfully.
    [[nodiscard]] Logger& logger() noexcept { return *logger_; }
    [[nodiscard]] metrics::MetricProvider& metrics() noexcept { return *metrics_; }
    [[nodiscard]] TracerProvider& tracer() noexcept { return *tracer_; }
    [[nodiscard]] chrono::ChronoProvider& chrono() noexcept { return *chrono_; }

    /// Thin factory conveniences; NotInitialized before initialize().
    [[nodiscard]] core::Result<metrics::Meter*> meter(std::string_view name,
                                                      std::string_view version) noexcept {
        if (!metrics_) {
            return std::unexpected(core::ErrorCode::NotInitialized);
        }
        return metrics_->get_meter(name, version);
    }

    [[nodiscard]] core::Result<TracerType*> tracer_for(std::string_view scope) noexcept {
        if (!tracer_) {
            return std::unexpected(core::ErrorCode::NotInitialized);
        }
        return tracer_->get_tracer(scope);
    }

private:
    Infrastructure() noexcept = default;

    template <class P, std::size_t C, std::size_t T>
    friend core::Result<std::unique_ptr<Infrastructure<P, C, T>>>
    make_infrastructure() noexcept;

    void rollback() noexcept {
        signals_.stop();
        tracer_.reset();
        trace_sink_.reset();
        metrics_.reset();
        metrics_sink_.reset();
        chrono_.reset();
        chrono_sink_.reset();
        logger_.reset();
        static_cast<void>(::pthread_sigmask(SIG_SETMASK, &previous_mask_, nullptr));
        initialized_ = false;
        detail::signal_owner.store(nullptr, std::memory_order_release);
    }

    // Declaration order is the lifetime contract: providers before the sinks
    // they reference; the watcher is destroyed first. The chrono provider is
    // declared after the logger/sink bridge it references, so the bridge, the
    // chrono sink, and the logger all outlive the chrono worker.
    std::optional<Logger> logger_{};
    detail::ChronoLoggerBridge<Logger> chrono_logger_bridge_{};
    std::optional<MetricsSink> metrics_sink_{};
    std::optional<metrics::MetricProvider> metrics_{};
    std::optional<TraceSink> trace_sink_{};
    std::optional<TracerProvider> tracer_{};
    std::optional<ChronoSink> chrono_sink_{};
    std::optional<chrono::ChronoProvider> chrono_{};
    detail::SignalWatcher signals_{};

    sigset_t previous_mask_{};
    bool initialized_{false};
    std::atomic<bool> shutdown_started_{false};
    alignas(64) std::atomic<InfrastructureStopReason> stop_reason_{
        InfrastructureStopReason::None};
};

/// Heap-only factory: the private constructor makes automatic/static
/// Infrastructure objects impossible. Allocation failure is a SystemError.
template <class Policy, std::size_t TracerCapacity = 1024,
          std::size_t TracerThreads = 32>
[[nodiscard]] core::Result<std::unique_ptr<
    Infrastructure<Policy, TracerCapacity, TracerThreads>>>
make_infrastructure() noexcept {
    using Type = Infrastructure<Policy, TracerCapacity, TracerThreads>;
    Type* const raw = new (std::nothrow) Type{};
    if (raw == nullptr) {
        return std::unexpected(core::ErrorCode::SystemError);
    }
    return std::unique_ptr<Type>{raw};
}

} // namespace nexigen::app
