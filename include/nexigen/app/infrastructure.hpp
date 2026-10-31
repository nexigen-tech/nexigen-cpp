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

template <class Policy, std::size_t Tracer_capacity = 1024,
          std::size_t Tracer_threads = 32>
class Infrastructure;

/// Stateless policy contract: component types plus static emplacement hooks.
template <class Policy>
concept InfrastructurePolicy = requires(
    std::optional<typename Policy::Logger>& logger,
    std::optional<typename Policy::Metrics_sink>& metrics_sink,
    std::optional<typename Policy::Trace_sink>& trace_sink,
    std::optional<typename Policy::Chrono_sink>& chrono_sink,
    const logging::Logging_settings& logging_settings,
    const Metrics_settings& metrics_settings,
    const Tracing_settings& tracing_settings,
    const chrono::Chrono_settings& chrono_settings) {
    typename Policy::Logger;
    typename Policy::Metrics_sink;
    typename Policy::Trace_sink;
    typename Policy::Chrono_sink;
    { Policy::emplace_logger(logger, logging_settings) } noexcept -> std::same_as<core::Status>;
    { Policy::emplace_metrics_sink(metrics_sink, metrics_settings) } noexcept
        -> std::same_as<core::Status>;
    { Policy::emplace_trace_sink(trace_sink, tracing_settings) } noexcept
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
class Signal_watcher {
public:
    Signal_watcher() noexcept = default;
    Signal_watcher(const Signal_watcher&) = delete;
    Signal_watcher& operator=(const Signal_watcher&) = delete;
    Signal_watcher(Signal_watcher&&) = delete;
    Signal_watcher& operator=(Signal_watcher&&) = delete;

    ~Signal_watcher() { stop(); }

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
                target->request_stop(Infrastructure_stop_reason::Sigint);
            } else if (info.ssi_signo == static_cast<std::uint32_t>(SIGTERM)) {
                target->request_stop(Infrastructure_stop_reason::Sigterm);
            }
        }
    }

    int fd_{-1};
    std::jthread thread_{};
};

/// Bridges the application logger to the chrono worker's module-local log hook
/// (chrono must not depend on logging): maps Clock_log_level to LogLevel and
/// forwards a pre-formatted message through the logger.
template <class Logger>
struct Chrono_logger_bridge {
    Logger* logger{nullptr};

    void log(chrono::Clock_log_level level, std::string_view message) noexcept {
        switch (level) {
            case chrono::Clock_log_level::info:
                logger->info("{}", message);
                break;
            case chrono::Clock_log_level::warning:
                logger->warning("{}", message);
                break;
            case chrono::Clock_log_level::error:
                logger->error("{}", message);
                break;
        }
    }
};

} // namespace detail

template <class Policy, std::size_t Tracer_capacity, std::size_t Tracer_threads>
class Infrastructure {
public:
    using Logger = typename Policy::Logger;
    using Metrics_sink = typename Policy::Metrics_sink;
    using Trace_sink = typename Policy::Trace_sink;
    using Chrono_sink = typename Policy::Chrono_sink;
    using Tracer_provider = tracing::TracerProvider<Tracer_capacity, Tracer_threads>;
    using Tracer_type = tracing::Tracer<Tracer_capacity, Tracer_threads>;

    static_assert(InfrastructurePolicy<Policy>,
                  "Policy must declare Logger/Metrics_sink/Trace_sink/Chrono_sink and static "
                  "emplace hooks");
    static_assert(logging::concepts::Logger<Logger>,
                  "Policy::Logger must satisfy nexigen::logging::concepts::Logger");
    static_assert(metrics::concepts::MetricSink<Metrics_sink>,
                  "Policy::Metrics_sink must satisfy metrics::concepts::MetricSink");
    static_assert(tracing::concepts::TraceSink<Trace_sink>,
                  "Policy::Trace_sink must satisfy tracing::concepts::TraceSink");
    static_assert(chrono::concepts::ChronoSink<Chrono_sink>,
                  "Policy::Chrono_sink must satisfy chrono::concepts::ChronoSink");

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
    [[nodiscard]] core::Status initialize(const App_settings& settings) noexcept {
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

        if (const core::Status status = Policy::emplace_logger(logger_, settings.logging);
            !status || !logger_) {
            return fail(status ? std::unexpected(core::ErrorCode::InvalidArgument) : status);
        }
        chrono_logger_bridge_.logger = &*logger_;
        if (const core::Status status = chrono_->start(chrono_logger_bridge_); !status) {
            return fail(status);
        }

        if (const core::Status status =
                Policy::emplace_metrics_sink(metrics_sink_, settings.metrics);
            !status || !metrics_sink_) {
            return fail(status ? std::unexpected(core::ErrorCode::InvalidArgument) : status);
        }
        metrics_.emplace(settings.metrics.options, *metrics_sink_);

        if (const core::Status status =
                Policy::emplace_trace_sink(trace_sink_, settings.tracing);
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

    /// Run the caller's tick until a stop is requested; returns the reason.
    /// A bool-returning tick requests a host stop when it yields false.
    template <class Tick>
    [[nodiscard]] Infrastructure_stop_reason run(Tick&& tick) noexcept {
        while (!stop_requested()) {
            if constexpr (std::is_invocable_r_v<bool, Tick&>) {
                if (!tick()) {
                    request_stop(Infrastructure_stop_reason::Requested);
                    break;
                }
            } else {
                tick();
            }
        }
        return stop_reason();
    }

    /// First stop cause wins; later requests leave the reason unchanged.
    void request_stop(Infrastructure_stop_reason reason) noexcept {
        if (reason == Infrastructure_stop_reason::None) {
            return;
        }
        Infrastructure_stop_reason expected = Infrastructure_stop_reason::None;
        static_cast<void>(stop_reason_.compare_exchange_strong(
            expected, reason, std::memory_order_acq_rel, std::memory_order_relaxed));
    }

    [[nodiscard]] bool stop_requested() const noexcept {
        return stop_reason_.load(std::memory_order_acquire) != Infrastructure_stop_reason::None;
    }

    [[nodiscard]] Infrastructure_stop_reason stop_reason() const noexcept {
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

        if (logger_) {
            capture(logger_->flush()); // drain first so the final record fits
            switch (stop_reason()) {
                case Infrastructure_stop_reason::Sigint:
                    logger_->warning("shutdown reason: sigint (2)");
                    break;
                case Infrastructure_stop_reason::Sigterm:
                    logger_->warning("shutdown reason: sigterm (15)");
                    break;
                case Infrastructure_stop_reason::Requested:
                    logger_->info("shutdown reason: requested");
                    break;
                case Infrastructure_stop_reason::None:
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
    [[nodiscard]] Tracer_provider& tracer() noexcept { return *tracer_; }
    [[nodiscard]] chrono::ChronoProvider& chrono() noexcept { return *chrono_; }

    /// Thin factory conveniences; NotInitialized before initialize().
    [[nodiscard]] core::Result<metrics::Meter*> meter(std::string_view name,
                                                      std::string_view version) noexcept {
        if (!metrics_) {
            return std::unexpected(core::ErrorCode::NotInitialized);
        }
        return metrics_->get_meter(name, version);
    }

    [[nodiscard]] core::Result<Tracer_type*> tracer_for(std::string_view scope) noexcept {
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
    detail::Chrono_logger_bridge<Logger> chrono_logger_bridge_{};
    std::optional<Metrics_sink> metrics_sink_{};
    std::optional<metrics::MetricProvider> metrics_{};
    std::optional<Trace_sink> trace_sink_{};
    std::optional<Tracer_provider> tracer_{};
    std::optional<Chrono_sink> chrono_sink_{};
    std::optional<chrono::ChronoProvider> chrono_{};
    detail::Signal_watcher signals_{};

    sigset_t previous_mask_{};
    bool initialized_{false};
    std::atomic<bool> shutdown_started_{false};
    alignas(64) std::atomic<Infrastructure_stop_reason> stop_reason_{
        Infrastructure_stop_reason::None};
};

/// Heap-only factory: the private constructor makes automatic/static
/// Infrastructure objects impossible. Allocation failure is a SystemError.
template <class Policy, std::size_t Tracer_capacity = 1024,
          std::size_t Tracer_threads = 32>
[[nodiscard]] core::Result<std::unique_ptr<
    Infrastructure<Policy, Tracer_capacity, Tracer_threads>>>
make_infrastructure() noexcept {
    using Type = Infrastructure<Policy, Tracer_capacity, Tracer_threads>;
    Type* const raw = new (std::nothrow) Type{};
    if (raw == nullptr) {
        return std::unexpected(core::ErrorCode::SystemError);
    }
    return std::unique_ptr<Type>{raw};
}

} // namespace nexigen::app
