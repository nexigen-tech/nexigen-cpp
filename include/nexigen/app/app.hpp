// nexigen::app — the application lifecycle base (change add-app-class; spec
// app/application).
//
// CRTP: the derived application declares Logger/Metrics_sink/Trace_sink and
// three emplacement hooks. The base owns the components in a lifetime-safe
// order, initializes them from App_settings, watches SIGINT/SIGTERM on a
// signalfd, exposes the logger/metric/tracer factories, and provides an
// idempotent shutdown that flushes everything and logs the stop reason once.
//
// Cold path only: initialize/shutdown are control-plane. run() performs one
// acquire load per tick and never allocates or locks.
#pragma once

#include <atomic>
#include <cerrno>
#include <cstddef>
#include <cstdint>
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
#include <nexigen/app/app_stop_reason.hpp>
#include <nexigen/core/result.hpp>
#include <nexigen/logging/concepts/logger_concept.hpp>
#include <nexigen/metrics/concepts/metric_sink_concept.hpp>
#include <nexigen/metrics/metric_provider.hpp>
#include <nexigen/tracing/concepts/tracer_concept.hpp>
#include <nexigen/tracing/tracer_provider.hpp>

namespace nexigen::app::detail {

/// signalfd watcher: masks are blocked by App::initialize before any worker
/// starts; this thread is the only consumer of SIGINT/SIGTERM. It records the
/// reason and requests the stop — it never formats or writes to a sink.
class Signal_watcher {
public:
    Signal_watcher() noexcept = default;
    Signal_watcher(const Signal_watcher&) = delete;
    Signal_watcher& operator=(const Signal_watcher&) = delete;
    Signal_watcher(Signal_watcher&&) = delete;
    Signal_watcher& operator=(Signal_watcher&&) = delete;

    ~Signal_watcher() { stop(); }

    template <class App>
    [[nodiscard]] core::Status start(App* app, const sigset_t& mask) noexcept {
        fd_ = ::signalfd(-1, &mask, SFD_CLOEXEC | SFD_NONBLOCK);
        if (fd_ < 0) {
            return std::unexpected(core::ErrorCode::SystemError);
        }
        // Under -fno-exceptions a failed thread creation terminates; this is
        // the same cold-path deployment assumption as the existing providers.
        thread_ = std::jthread{[this, app](std::stop_token token) { run(app, token); }};
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
    template <class App>
    void run(App* app, std::stop_token token) noexcept {
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
                app->request_stop(App_stop_reason::Sigint);
            } else if (info.ssi_signo == static_cast<std::uint32_t>(SIGTERM)) {
                app->request_stop(App_stop_reason::Sigterm);
            }
        }
    }

    int fd_{-1};
    std::jthread thread_{};
};

} // namespace nexigen::app::detail

namespace nexigen::app {

template <class Derived, class Logger_type, class Metrics_sink_type, class Trace_sink_type,
          std::size_t Tracer_capacity = 1024, std::size_t Tracer_threads = 32>
class App {
public:
    using Logger = Logger_type;
    using Metrics_sink = Metrics_sink_type;
    using Trace_sink = Trace_sink_type;
    using Tracer_provider = tracing::TracerProvider<Tracer_capacity, Tracer_threads>;
    using Tracer_type = tracing::Tracer<Tracer_capacity, Tracer_threads>;

    static_assert(logging::concepts::Logger<Logger>,
                  "Derived::Logger must satisfy nexigen::logging::concepts::Logger");
    static_assert(metrics::concepts::MetricSink<Metrics_sink>,
                  "Derived::Metrics_sink must satisfy metrics::concepts::MetricSink");
    static_assert(tracing::concepts::TraceSink<Trace_sink>,
                  "Derived::Trace_sink must satisfy tracing::concepts::TraceSink");

    App(const App&) = delete;
    App& operator=(const App&) = delete;
    App(App&&) = delete;
    App& operator=(App&&) = delete;

    ~App() {
        if (initialized_) {
            static_cast<void>(shutdown());
        }
    }

    /// Configure the derived components, construct the providers, start the
    /// signal watcher and publish current(). Idempotent while initialized.
    [[nodiscard]] core::Status initialize(const App_settings& settings) noexcept {
        if (initialized_) {
            return {};
        }
        if (shutdown_started_) {
            return std::unexpected(core::ErrorCode::NotInitialized);
        }

        sigset_t mask;
        sigemptyset(&mask);
        sigaddset(&mask, SIGINT);
        sigaddset(&mask, SIGTERM);
        if (::pthread_sigmask(SIG_BLOCK, &mask, &previous_mask_) != 0) {
            return std::unexpected(core::ErrorCode::SystemError);
        }

        const auto fail = [this](core::Status status) noexcept -> core::Status {
            rollback();
            return status;
        };

        if (const core::Status status = derived().emplace_logger(logger_, settings.logging);
            !status || !logger_) {
            return fail(status ? std::unexpected(core::ErrorCode::InvalidArgument) : status);
        }
        if (const core::Status status =
                derived().emplace_metrics_sink(metrics_sink_, settings.metrics);
            !status || !metrics_sink_) {
            return fail(status ? std::unexpected(core::ErrorCode::InvalidArgument) : status);
        }
        metrics_.emplace(settings.metrics.options, *metrics_sink_);

        if (const core::Status status =
                derived().emplace_trace_sink(trace_sink_, settings.tracing);
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
        current_.store(static_cast<Derived*>(this), std::memory_order_release);
        return {};
    }

    /// Run the caller's tick until a stop is requested; returns the recorded
    /// reason. A bool-returning tick requests a host stop when it yields false.
    template <class Tick>
    [[nodiscard]] App_stop_reason run(Tick&& tick) noexcept {
        while (!stop_requested()) {
            if constexpr (std::is_invocable_r_v<bool, Tick&>) {
                if (!tick()) {
                    request_stop(App_stop_reason::Requested);
                    break;
                }
            } else {
                tick();
            }
        }
        return stop_reason();
    }

    /// First stop cause wins; later requests leave the reason unchanged.
    void request_stop(App_stop_reason reason) noexcept {
        if (reason == App_stop_reason::None) {
            return;
        }
        App_stop_reason expected = App_stop_reason::None;
        static_cast<void>(stop_reason_.compare_exchange_strong(
            expected, reason, std::memory_order_acq_rel, std::memory_order_relaxed));
    }

    [[nodiscard]] bool stop_requested() const noexcept {
        return stop_reason_.load(std::memory_order_acquire) != App_stop_reason::None;
    }

    [[nodiscard]] App_stop_reason stop_reason() const noexcept {
        return stop_reason_.load(std::memory_order_acquire);
    }

    /// Stop the watcher, log the reason once, flush tracer → metrics → logger,
    /// restore the mask and clear current(). Sequential calls are idempotent.
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
                case App_stop_reason::Sigint:
                    logger_->warning("shutdown reason: sigint (2)");
                    break;
                case App_stop_reason::Sigterm:
                    logger_->warning("shutdown reason: sigterm (15)");
                    break;
                case App_stop_reason::Requested:
                    logger_->info("shutdown reason: requested");
                    break;
                case App_stop_reason::None:
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

        static_cast<void>(::pthread_sigmask(SIG_SETMASK, &previous_mask_, nullptr));
        current_.store(nullptr, std::memory_order_release);

        tracer_.reset();
        trace_sink_.reset();
        metrics_.reset();
        metrics_sink_.reset();
        logger_.reset();
        initialized_ = false;
        return first;
    }

    /// Precondition: initialized successfully.
    [[nodiscard]] Logger& logger() noexcept { return *logger_; }
    [[nodiscard]] metrics::MetricProvider& metrics() noexcept { return *metrics_; }
    [[nodiscard]] Tracer_provider& tracer() noexcept { return *tracer_; }

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

    /// Non-owning pointer to the most recently initialized instance.
    [[nodiscard]] static App* current() noexcept {
        return current_.load(std::memory_order_acquire);
    }

protected:
    App() noexcept = default;

    [[nodiscard]] Derived& derived() noexcept {
        return *static_cast<Derived*>(this);
    }

private:
    void rollback() noexcept {
        signals_.stop();
        tracer_.reset();
        trace_sink_.reset();
        metrics_.reset();
        metrics_sink_.reset();
        logger_.reset();
        static_cast<void>(::pthread_sigmask(SIG_SETMASK, &previous_mask_, nullptr));
        current_.store(nullptr, std::memory_order_release);
        initialized_ = false;
    }

    // Declaration order is the lifetime contract (design D2): providers are
    // destroyed before the sinks they reference; the watcher is destroyed
    // first.
    std::optional<Logger> logger_{};
    std::optional<Metrics_sink> metrics_sink_{};
    std::optional<metrics::MetricProvider> metrics_{};
    std::optional<Trace_sink> trace_sink_{};
    std::optional<Tracer_provider> tracer_{};
    detail::Signal_watcher signals_{};

    sigset_t previous_mask_{};
    bool initialized_{false};
    std::atomic<bool> shutdown_started_{false};
    alignas(64) std::atomic<App_stop_reason> stop_reason_{App_stop_reason::None};

    alignas(64) static inline std::atomic<App*> current_{nullptr};
};

} // namespace nexigen::app
