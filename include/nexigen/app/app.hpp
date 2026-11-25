/// @file
/// @brief Explicit application runtime facade for one Infrastructure policy.
///
/// `App<Policy>` owns the cold-path application boilerplate: command-line and
/// default settings, the optional embedded time base, the transport node,
/// `Infrastructure`, one meter with metric handles, up to sixteen event
/// subscriptions, and optional event outputs. The explicit `create` -> instruments/ports -> `run`
/// -> `shutdown` sequence keeps a minimal `main` readable without hiding
/// lifecycle decisions.
///
/// @note Spec `app/runtime`, purpose "App owns the application runtime
///       lifecycle".
#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <format>
#include <functional>
#include <memory>
#include <new>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include <nexigen/app/app_settings.hpp>
#include <nexigen/app/command_line.hpp>
#include <nexigen/app/detail/embedded_time_base.hpp>
#include <nexigen/app/detail/sigpipe_guard.hpp>
#include <nexigen/app/infrastructure.hpp>
#include <nexigen/app/infrastructure_stop_reason.hpp>
#include <nexigen/app/time_base_mode.hpp>
#include <nexigen/com/event_channel.hpp>
#include <nexigen/com/event_consumer.hpp>
#include <nexigen/com/event_consumer_set.hpp>
#include <nexigen/com/event_header.hpp>
#include <nexigen/com/event_producer.hpp>
#include <nexigen/com/i_event_consumer.hpp>
#include <nexigen/com/loss_info.hpp>
#include <nexigen/com/node.hpp>
#include <nexigen/com/service_name.hpp>
#include <nexigen/core/app_id.hpp>
#include <nexigen/core/result.hpp>
#include <nexigen/core/settings_diagnostic.hpp>
#include <nexigen/metrics/counter.hpp>
#include <nexigen/metrics/explicit_histogram.hpp>
#include <nexigen/metrics/gauge.hpp>
#include <nexigen/metrics/meter.hpp>
#include <nexigen/metrics/policy_enums.hpp>

namespace nexigen::app {

/// @brief Explicit application runtime around one Infrastructure policy.
///
/// `create` performs all cold-path setup: it parses the standard application
/// options, loads settings files or builds defaults, selects the time base,
/// installs the `SIGPIPE` guard, creates the transport node, initializes
/// `Infrastructure` and prepares the event consumer set. Instruments and ports
/// are then declared through `counter`, `gauge`, `histogram`, `subscribe` and
/// `publish`; any failure is retained as a diagnostic and blocks `run`.
///
/// `run` drives up to sixteen subscriptions of one payload type through a
/// bounded `EventConsumerSet` wait so an idle application still observes a stop
/// request, and `shutdown` drains the telemetry components and restores the
/// previous `SIGPIPE` disposition. The
/// object is heap-owned and intentionally non-copyable and non-movable because
/// its address backs the WaitSet attachments and the borrowed producer handles
/// returned by `publish`.
///
/// @tparam Policy Stateless `Infrastructure` policy that supplies the logger,
///         metric sink and trace sink types plus their emplacement hooks.
///
/// @note Spec `app/runtime`, requirements "App owns the application runtime
///       lifecycle", "Declared instruments and ports must be valid before run",
///       and "One event input and an optional event output".
/// @note Create the `App` on the thread that will call `run()` and `shutdown()`:
///       `Infrastructure` blocks termination signals on the calling thread and
///       only one live `App` may own the process-global signal state.
/// @note The destructor calls `shutdown()` as a safety net; callers should still
///       shut down explicitly so failures can be reported.
template <class Policy> class App {
public:
    /// @brief Maximum subscriptions one `App` can attach.
    static constexpr std::size_t max_subscriptions = 16U;

    /// @brief Heap-only application factory.
    ///
    /// When `argv` names a settings directory, the strict application parser and
    /// file loader run unchanged. Otherwise a small identity-only parser accepts
    /// `--app-name`, `--app-id`, `--log-level` and `--help`, and the remaining
    /// settings come from `Settings::create(app_name)`. A non-empty canonical
    /// `app_id` is mandatory; the factory never falls back to the application
    /// name or a compiled tag.
    ///
    /// `Embedded` publishes a local time base before `Infrastructure::initialize`
    /// and rewrites the chrono segment for the rest of the setup. `External`
    /// leaves the configured segment untouched and fails when no usable base is
    /// published. `SIGPIPE` is set to `SIG_IGN` before the node, logger or ports
    /// are created, and the previous disposition is restored even on failure.
    ///
    /// @param argc Argument count from `main`.
    /// @param argv Argument vector from `main`; may be null-terminated entries.
    /// @param app_name Fallback application name when no `--app-name` is given.
    /// @param mode External (default) or explicit Embedded time base.
    /// @return The application, `InvalidArgument` for bad options or a missing
    ///         identifier, `SystemError` for allocation/setup failures, the
    ///         time-base attach error for `External`, or a successful null
    ///         pointer when `--help` was printed.
    ///
    /// @note Spec `app/runtime`, requirements "App bootstrap loads files or uses
    ///       defaults", "Application identity is required", "Time base is
    ///       external by default and embeddable explicitly", and "Graceful
    ///       signals and exit codes".
    /// @note A second live `App` returns `Conflict` when `Infrastructure`
    ///       claims the process-global signal ownership.
    /// @note The caller owns the returned `unique_ptr`; a null success value is
    ///       the documented `--help` outcome and maps to exit code 0.

    [[nodiscard]] static core::Result<std::unique_ptr<App>>
    create(int argc, const char* const argv[], std::string_view app_name,
           TimeBaseMode mode = TimeBaseMode::External) noexcept {
        std::unique_ptr<App> app{new (std::nothrow) App{}};
        if (!app) {
            return std::unexpected(core::ErrorCode::SystemError);
        }
        app->mode_ = mode;
        app->app_name_ = std::string(app_name);

        app::Settings settings{};
        const core::Status parsed = app->load_or_default_settings(argc, argv, settings);
        if (!parsed) {
            return std::unexpected(parsed.error());
        }
        if (app->help_requested_) {
            return std::unique_ptr<App>{}; // help is a successful, null outcome
        }
        if (settings.app_id.empty()) {
            std::fprintf(stderr,
                         "app: --app-id is required (or launch through the suite manifest)\n");
            return std::unexpected(core::ErrorCode::InvalidArgument);
        }
        app->app_id_ = settings.app_id;
        if (settings.app_name.empty()) {
            settings.app_name = app->app_name_;
        } else {
            app->app_name_ = settings.app_name;
        }

        if (mode == TimeBaseMode::Embedded) {
            app->embedded_ = std::make_unique<detail::EmbeddedTimeBase>(app->app_name_);
            if (!app->embedded_->is_open()) {
                std::fprintf(stderr, "app: embedded time base could not be created\n");
                return std::unexpected(core::ErrorCode::SystemError);
            }
            settings.chrono.segment_name = app->embedded_->name();
        }

        app->sigpipe_ = std::make_unique<detail::SigpipeGuard>();
        if (!app->sigpipe_->armed()) {
            std::fprintf(stderr, "app: SIGPIPE disposition could not be installed\n");
            return std::unexpected(core::ErrorCode::SystemError);
        }

        auto node = com::Node::create();
        if (!node) {
            std::fprintf(stderr, "app: com::Node::create failed\n");
            return std::unexpected(core::ErrorCode::SystemError);
        }
        app->node_ = std::make_unique<com::Node>(std::move(*node));

        auto infra = make_infrastructure<Policy, 256, 8>();
        if (!infra) {
            std::fprintf(stderr, "app: make_infrastructure failed\n");
            return std::unexpected(core::ErrorCode::SystemError);
        }
        app->infra_ = std::move(*infra);
        if (auto status = app->infra_->initialize(settings); !status) {
            std::fprintf(stderr, "app: Infrastructure::initialize failed: %s\n",
                         std::string(core::to_string(status.error())).c_str());
            return std::unexpected(status.error());
        }

        app->consumer_set_.emplace(*app->node_);
        if (!*app->consumer_set_) {
            std::fprintf(stderr, "app: event consumer set could not be created\n");
            return std::unexpected(core::ErrorCode::SystemError);
        }
        return app;
    }

    /// @brief Deleted: the WaitSet and borrowed handles depend on a stable address.
    App(const App&) = delete;
    /// @brief Deleted: copies would duplicate process-global ownership.
    App& operator=(const App&) = delete;
    /// @brief Deleted: moving would invalidate WaitSet attachments and handles.
    App(App&&) = delete;
    /// @brief Deleted: moving would invalidate WaitSet attachments and handles.
    App& operator=(App&&) = delete;

    /// @brief Destructor fallback: drain telemetry if the caller did not.
    ///
    /// Equivalent to calling `shutdown()` and ignoring its status; the explicit
    /// call remains the way to observe shutdown failures.
    ~App() { static_cast<void>(shutdown()); }

    /// @brief Run the input loop until a stop is requested.
    ///
    /// Returns immediately with `InvalidArgument` when any instrument or port
    /// declaration recorded a diagnostic, so a partially constructed
    /// application never enters the loop. Otherwise it delegates to
    /// `Infrastructure::run` and waits on all subscriptions through
    /// `EventConsumerSet<max_subscriptions>` with a 100 ms timeout; every
    /// wake-up dispatches the registered callbacks on this thread, and every
    /// timeout re-checks the stop request and time-base state.
    ///
    /// @return The `Infrastructure` stop reason (`Requested`, `Sigint`,
    ///         `Sigterm`, `Sighup`, `TimeBaseLost`, or `None`), `InvalidArgument`
    ///         when the diagnostics gate rejected the application,
    ///         `NotInitialized` before `create` or after `shutdown`, or
    ///         `SystemError` when the bounded wait itself failed.
    ///
    /// @note Spec `app/runtime`, requirements "Declared instruments and ports
    ///       must be valid before run" and "One event input and an optional
    ///       event output".
    /// @note Spec `app/application`, requirement "Run loop observes the stop
    ///       request".
    /// @note Must be called on the same thread that created the `App`.
    [[nodiscard]] core::Result<InfrastructureStopReason> run() noexcept {
        if (!infra_ || shutdown_done_) {
            return std::unexpected(core::ErrorCode::NotInitialized);
        }
        if (!diagnostics_.empty()) {
            return std::unexpected(core::ErrorCode::InvalidArgument);
        }
        bool wait_failed = false;
        const InfrastructureStopReason reason = infra_->run([&]() -> bool {
            if (auto drained = consumer_set_->drain_all(); !drained) {
                wait_failed = true;
                return false;
            }
            auto waited = consumer_set_->wait_for(std::chrono::milliseconds{100});
            if (!waited) {
                wait_failed = true;
                return false;
            }
            return true;
        });
        if (wait_failed) {
            return std::unexpected(core::ErrorCode::SystemError);
        }
        return reason;
    }

    /// @brief Drain telemetry and restore process-global state.
    ///
    /// Stops and joins the signal watcher, logs the stop reason, quiesces the
    /// tracer and metric providers, flushes and quiesces the logger, and only
    /// then restores the previous `SIGPIPE` disposition. Component storage is
    /// released by the destructor, not here. The first call performs the work;
    /// every later call is a no-op that returns success, so the destructor can
    /// safely repeat it.
    ///
    /// @return The first failure from `Infrastructure::shutdown()`, or success
    ///         on the first successful call and on every repeated call.
    ///
    /// @note Spec `app/runtime`, requirement "App owns the application runtime
    ///       lifecycle".
    /// @note Spec `app/application`, requirement "Shutdown is idempotent and
    ///       flushes in order".
    /// @note Must be called on the same thread that called `run()` and before
    ///       the `App` is destroyed when shutdown status matters.
    [[nodiscard]] core::Status shutdown() noexcept {
        if (shutdown_done_) {
            return {};
        }
        shutdown_done_ = true;
        core::Status status{};
        if (infra_) {
            status = infra_->shutdown();
        }
        if (sigpipe_) {
            sigpipe_->restore();
        }
        return status;
    }

    /// @brief Provider-owned counter handle returned by `counter()`.
    ///
    /// Multi-threaded, readable and reset-on-read: safe to increment from the
    /// `run()` callback and from worker threads, and harvested by the provider.
    using Counter =
        metrics::Counter<metrics::ConcurrencyPolicy::MultiThreaded, metrics::AccessPolicy::Readable,
                         metrics::ResetPolicy::ResetOnRead>;

    /// @brief Create a counter under the app meter.
    ///
    /// The meter is created lazily on the first instrument request. A failed
    /// meter or counter creation appends a `core::SettingsDiagnostic` naming
    /// @p name and returns `nullptr`; `run()` will then refuse to start.
    ///
    /// @param name Unique metric name within the app meter.
    /// @param unit Optional unit string, empty when unitless.
    /// @return The provider-owned counter, or `nullptr` after recording a
    ///         diagnostic.
    ///
    /// @note Spec `app/runtime`, requirements "App owns the application runtime
    ///       lifecycle" and "Declared instruments and ports must be valid before
    ///       run".
    /// @note The returned handle is borrowed; it stays valid until the provider
    ///       is destroyed by the `App` destructor.
    [[nodiscard]] Counter* counter(std::string_view name, std::string_view unit = {}) noexcept {
        if (!ensure_meter()) {
            return nullptr;
        }
        auto created =
            meter_->template create_counter<metrics::ConcurrencyPolicy::MultiThreaded,
                                            metrics::AccessPolicy::Readable,
                                            metrics::ResetPolicy::ResetOnRead>(name, unit);
        if (!created) {
            record_failure(name, core::to_string(created.error()));
            return nullptr;
        }
        return *created;
    }

    /// @brief Provider-owned integer gauge handle returned by `gauge()`.
    ///
    /// Multi-threaded, readable and reset-on-read, matching the counter policy
    /// so callbacks and workers can update it without external locking.
    using Gauge =
        metrics::Gauge<std::int64_t, metrics::ConcurrencyPolicy::MultiThreaded,
                       metrics::AccessPolicy::Readable, metrics::ResetPolicy::ResetOnRead>;

    /// @brief Create an integer gauge under the app meter.
    ///
    /// The meter is created lazily on the first instrument request. A failed
    /// meter or gauge creation appends a `core::SettingsDiagnostic` naming
    /// @p name and returns `nullptr`; `run()` will then refuse to start.
    ///
    /// @param name Unique metric name within the app meter.
    /// @param unit Optional unit string, empty when unitless.
    /// @return The provider-owned gauge, or `nullptr` after recording a
    ///         diagnostic.
    ///
    /// @note Spec `app/runtime`, requirements "App owns the application runtime
    ///       lifecycle" and "Declared instruments and ports must be valid before
    ///       run".
    /// @note The returned handle is borrowed; it stays valid until the provider
    ///       is destroyed by the `App` destructor.
    [[nodiscard]] Gauge* gauge(std::string_view name, std::string_view unit = {}) noexcept {
        if (!ensure_meter()) {
            return nullptr;
        }
        auto created =
            meter_->template create_gauge<std::int64_t, metrics::ConcurrencyPolicy::MultiThreaded,
                                          metrics::AccessPolicy::Readable,
                                          metrics::ResetPolicy::ResetOnRead>(name, unit);
        if (!created) {
            record_failure(name, core::to_string(created.error()));
            return nullptr;
        }
        return *created;
    }

    /// @brief Provider-owned histogram handle returned by `histogram()`.
    ///
    /// Explicit-bucket, multi-threaded, readable and reset-on-read; suitable for
    /// recording callback latencies without allocating in the hot path.
    using Histogram = metrics::ExplicitBucketHistogram<metrics::ConcurrencyPolicy::MultiThreaded,
                                                       metrics::AccessPolicy::Readable,
                                                       metrics::ResetPolicy::ResetOnRead>;

    /// @brief Create an explicit-bucket histogram under the app meter.
    ///
    /// The meter is created lazily on the first instrument request. A failed
    /// meter or histogram creation appends a `core::SettingsDiagnostic` naming
    /// @p name and returns `nullptr`; `run()` will then refuse to start.
    ///
    /// @param name Unique metric name within the app meter.
    /// @param bounds Bucket upper bounds in ascending order; the span must stay
    ///        valid for the duration of the call.
    /// @param unit Optional unit string, empty when unitless.
    /// @return The provider-owned histogram, or `nullptr` after recording a
    ///         diagnostic.
    ///
    /// @note Spec `app/runtime`, requirements "App owns the application runtime
    ///       lifecycle" and "Declared instruments and ports must be valid before
    ///       run".
    /// @note The returned handle is borrowed; it stays valid until the provider
    ///       is destroyed by the `App` destructor.
    [[nodiscard]] Histogram* histogram(std::string_view name, std::span<const double> bounds,
                                       std::string_view unit = {}) noexcept {
        if (!ensure_meter()) {
            return nullptr;
        }
        auto created =
            meter_->template create_explicit_histogram<metrics::ConcurrencyPolicy::MultiThreaded,
                                                       metrics::AccessPolicy::Readable,
                                                       metrics::ResetPolicy::ResetOnRead>(
                name, bounds, unit);
        if (!created) {
            record_failure(name, core::to_string(created.error()));
            return nullptr;
        }
        return *created;
    }

    /// @brief Subscribe to one input event service.
    ///
    /// Creates one `com::EventChannel<T>` and one `com::EventConsumer<T>` and
    /// stores them behind the type-erased `SubscriptionBase` owned by this
    /// `App`. The callback is invoked on the `run()` thread and may take either
    /// `(const com::EventHeader&, const T&)` or `(const T&)`. Event loss is
    /// reported through the infrastructure logger when one is available.
    ///
    /// Up to sixteen subscriptions are accepted; each subscription may use its
    /// own trivially copyable payload type. A seventeenth subscription is
    /// rejected as a diagnostic error. Any service/channel/consumer/attach
    /// failure is retained the same way, so `run()` refuses to start instead of
    /// silently dropping events.
    ///
    /// @tparam T Trivially copyable event payload satisfying
    ///         `com::concepts::EventPayload`.
    /// @tparam Callback Callable invocable with `(const T&)` or
    ///         `(const com::EventHeader&, const T&)`.
    /// @param service Event service name forwarded to `com::ServiceName`.
    /// @param callback Callback to invoke on the `run()` thread; stored by value.
    /// @return `true` when the subscription is registered and attached,
    ///         otherwise `false` after recording a diagnostic.
    ///
    /// @note Spec `app/runtime`, requirements "One or more event inputs and an
    ///       optional event output" and "Declared instruments and ports must be
    ///       valid before run".
    /// @note Register subscriptions before `run()`; callbacks must be short
    ///       because they execute serially on the stop-observation thread.
    template <class T, class Callback>
        requires com::concepts::EventPayload<T>
    bool subscribe(std::string_view service, Callback&& callback) noexcept {
        if (subscriptions_.size() >= max_subscriptions) {
            record_failure(service, "subscription limit of 16 reached");
            return false;
        }
        auto name = com::ServiceName::create(std::string(service).c_str());
        if (!name) {
            record_failure(service, "invalid service name");
            return false;
        }
        auto channel = com::EventChannel<T>::create(*node_, *name, config_);
        if (!channel) {
            record_failure(service, "event channel could not be created");
            return false;
        }

        using EventCb = std::function<void(const com::EventHeader&, const T&)>;
        using LossCb = std::function<void(const com::LossInfo&)>;
        EventCb on_event = [cb = std::forward<Callback>(callback)](const com::EventHeader& header,
                                                                   const T& payload) mutable {
            if constexpr (std::is_invocable_v<Callback&, const com::EventHeader&, const T&>) {
                cb(header, payload);
            } else {
                cb(payload);
            }
        };
        LossCb on_loss = [this](const com::LossInfo& loss) {
            if (infra_) {
                if (auto logger = infra_->logger(); logger) {
                    (*logger)->warning("input loss count={}", loss.lost_count);
                }
            }
        };

        auto consumer = channel->consumer(std::move(on_event), std::move(on_loss));
        if (!consumer) {
            record_failure(service, "event consumer could not be created");
            return false;
        }
        auto subscription = std::make_unique<Subscription<T, EventCb, LossCb>>(
            std::move(*channel), std::move(*consumer));
        if (auto attached = consumer_set_->attach(subscription->consumer()); !attached) {
            record_failure(service, "event consumer could not be attached");
            return false;
        }
        subscriptions_.push_back(std::move(subscription));
        return true;
    }

    /// @brief Subscribe to one input event service with explicit channel limits.
    ///
    /// @tparam T Trivially copyable event payload.
    /// @tparam Callback Callable invocable with `(const T&)` or
    ///         `(const com::EventHeader&, const T&)`.
    /// @param service Event service name.
    /// @param callback Callback invoked on the `run()` thread.
    /// @param config Channel limits for the created service.
    /// @return `true` when the subscription is registered and attached.
    template <class T, class Callback>
        requires com::concepts::EventPayload<T>
    bool subscribe_with_config(std::string_view service, Callback&& callback,
                               const com::ChannelConfig& config) noexcept {
        if (subscriptions_.size() >= max_subscriptions) {
            record_failure(service, "subscription limit of 16 reached");
            return false;
        }
        auto name = com::ServiceName::create(std::string(service).c_str());
        if (!name) {
            record_failure(service, "invalid service name");
            return false;
        }
        auto channel = com::EventChannel<T>::create(*node_, *name, config);
        if (!channel) {
            record_failure(service, "event channel could not be created");
            return false;
        }
        using EventCb = std::function<void(const com::EventHeader&, const T&)>;
        using LossCb = std::function<void(const com::LossInfo&)>;
        EventCb on_event = [cb = std::forward<Callback>(callback)](const com::EventHeader& header,
                                                                   const T& payload) mutable {
            if constexpr (std::is_invocable_v<Callback&, const com::EventHeader&, const T&>) {
                cb(header, payload);
            } else {
                cb(payload);
            }
        };
        LossCb on_loss = [this](const com::LossInfo& loss) {
            if (infra_) {
                if (auto logger = infra_->logger(); logger) {
                    (*logger)->warning("input loss count={}", loss.lost_count);
                }
            }
        };
        auto consumer = channel->consumer(std::move(on_event), std::move(on_loss));
        if (!consumer) {
            record_failure(service, "event consumer could not be created");
            return false;
        }
        auto subscription = std::make_unique<Subscription<T, EventCb, LossCb>>(
            std::move(*channel), std::move(*consumer));
        if (auto attached = consumer_set_->attach(subscription->consumer()); !attached) {
            record_failure(service, "event consumer could not be attached");
            return false;
        }
        subscriptions_.push_back(std::move(subscription));
        return true;
    }

    /// @brief Subscribe with explicit channel limits and a loss callback.
    ///
    /// @tparam T Trivially copyable event payload.
    /// @tparam Callback Callable with `(const T&)` or
    ///         `(const com::EventHeader&, const T&)`.
    /// @tparam OnLoss Callable with `(const com::LossInfo&)`.
    /// @param service Event service name.
    /// @param callback Event callback.
    /// @param on_loss Transport-loss callback.
    /// @param config Channel limits for the created service.
    /// @return `true` when the subscription is registered and attached.
    template <class T, class Callback, class OnLoss>
        requires com::concepts::EventPayload<T>
    bool subscribe_with_config(std::string_view service, Callback&& callback, OnLoss&& on_loss,
                               const com::ChannelConfig& config) noexcept {
        if (subscriptions_.size() >= max_subscriptions) {
            record_failure(service, "subscription limit of 16 reached");
            return false;
        }
        auto name = com::ServiceName::create(std::string(service).c_str());
        if (!name) {
            record_failure(service, "invalid service name");
            return false;
        }
        auto channel = com::EventChannel<T>::create(*node_, *name, config);
        if (!channel) {
            record_failure(service, "event channel could not be created");
            return false;
        }
        using EventCb = std::function<void(const com::EventHeader&, const T&)>;
        using LossCb = std::function<void(const com::LossInfo&)>;
        EventCb on_event = [cb = std::forward<Callback>(callback)](const com::EventHeader& header,
                                                                   const T& payload) mutable {
            if constexpr (std::is_invocable_v<Callback&, const com::EventHeader&, const T&>) {
                cb(header, payload);
            } else {
                cb(payload);
            }
        };
        LossCb loss = [cb = std::forward<OnLoss>(on_loss)](const com::LossInfo& info) mutable {
            cb(info);
        };
        auto consumer = channel->consumer(std::move(on_event), std::move(loss));
        if (!consumer) {
            record_failure(service, "event consumer could not be created");
            return false;
        }
        auto subscription = std::make_unique<Subscription<T, EventCb, LossCb>>(
            std::move(*channel), std::move(*consumer));
        if (auto attached = consumer_set_->attach(subscription->consumer()); !attached) {
            record_failure(service, "event consumer could not be attached");
            return false;
        }
        subscriptions_.push_back(std::move(subscription));
        return true;
    }

    /// @brief Subscribe with an explicit transport-loss callback.
    ///
    /// Same as the two-argument overload, but replaces the default logger
    /// warning with @p on_loss. The callback runs on the `run()` thread.
    ///
    /// @tparam T Trivially copyable event payload.
    /// @tparam Callback Callable with `(const T&)` or
    ///         `(const com::EventHeader&, const T&)`.
    /// @tparam OnLoss Callable with `(const com::LossInfo&)`.
    /// @param service Event service name.
    /// @param callback Event callback.
    /// @param on_loss Transport-loss callback.
    /// @return `true` when the subscription is registered and attached.
    template <class T, class Callback, class OnLoss>
        requires com::concepts::EventPayload<T>
    bool subscribe(std::string_view service, Callback&& callback, OnLoss&& on_loss) noexcept {
        if (subscriptions_.size() >= max_subscriptions) {
            record_failure(service, "subscription limit of 16 reached");
            return false;
        }
        auto name = com::ServiceName::create(std::string(service).c_str());
        if (!name) {
            record_failure(service, "invalid service name");
            return false;
        }
        auto channel = com::EventChannel<T>::create(*node_, *name, config_);
        if (!channel) {
            record_failure(service, "event channel could not be created");
            return false;
        }
        using EventCb = std::function<void(const com::EventHeader&, const T&)>;
        using LossCb = std::function<void(const com::LossInfo&)>;
        EventCb on_event = [cb = std::forward<Callback>(callback)](const com::EventHeader& header,
                                                                   const T& payload) mutable {
            if constexpr (std::is_invocable_v<Callback&, const com::EventHeader&, const T&>) {
                cb(header, payload);
            } else {
                cb(payload);
            }
        };
        LossCb loss = [cb = std::forward<OnLoss>(on_loss)](const com::LossInfo& info) mutable {
            cb(info);
        };
        auto consumer = channel->consumer(std::move(on_event), std::move(loss));
        if (!consumer) {
            record_failure(service, "event consumer could not be created");
            return false;
        }
        auto subscription = std::make_unique<Subscription<T, EventCb, LossCb>>(
            std::move(*channel), std::move(*consumer));
        if (auto attached = consumer_set_->attach(subscription->consumer()); !attached) {
            record_failure(service, "event consumer could not be attached");
            return false;
        }
        subscriptions_.push_back(std::move(subscription));
        return true;
    }

    /// @brief Create the optional output event producer.
    ///
    /// Creates an `com::EventChannel<T>` and an `com::EventProducer<T>` carrying
    /// this application's `app_id`, stores both in a type-erased output slot, and
    /// returns a borrowed producer pointer. All output headers therefore carry
    /// the same identity as the telemetry frames. A failure to create the
    /// service, channel or producer is retained as a diagnostic and blocks
    /// `run()`.
    ///
    /// @tparam T Trivially copyable event payload satisfying
    ///         `com::concepts::EventPayload`.
    /// @param service Event service name forwarded to `com::ServiceName`.
    /// @return Borrowed producer owned by this `App`, or `nullptr` after
    ///         recording a diagnostic.
    ///
    /// @note Spec `app/runtime`, requirements "One event input and an optional
    ///       event output", "Application identity is required", and "Declared
    ///       instruments and ports must be valid before run".
    /// @note The pointer is valid until the `App` is destroyed; do not delete it.
    /// @note `publish_now` is called from the callback on the `run()` thread.
    template <class T>
        requires com::concepts::EventPayload<T>
    [[nodiscard]] com::EventProducer<T>* publish(std::string_view service) noexcept {
        return publish<T>(service, config_);
    }

    /// @brief Create an optional output producer with explicit channel limits.
    /// @tparam T Event payload type.
    /// @param service Event service name.
    /// @param config Channel configuration.
    /// @return Borrowed producer, or `nullptr` after a diagnostic.
    template <class T>
        requires com::concepts::EventPayload<T>
    [[nodiscard]] com::EventProducer<T>* publish(std::string_view service,
                                                 const com::ChannelConfig& config) noexcept {
        auto name = com::ServiceName::create(std::string(service).c_str());
        if (!name) {
            record_failure(service, "invalid service name");
            return nullptr;
        }
        auto channel = com::EventChannel<T>::create(*node_, *name, config);
        if (!channel) {
            record_failure(service, "event channel could not be created");
            return nullptr;
        }
        auto producer = channel->producer(app_id_.text());
        if (!producer) {
            record_failure(service, std::string("event producer: ") +
                                        std::string(com::to_string(producer.error())));
            return nullptr;
        }
        auto port = std::make_unique<OutputPort<T>>(std::move(*channel), std::move(*producer));
        auto* borrowed = &port->producer();
        outputs_.push_back(std::move(port));
        return borrowed;
    }

    /// @brief Application logger handle.
    ///
    /// @return The policy-created logger, or the failure returned by
    ///         `Infrastructure::logger()`.
    ///
    /// @note Spec `app/application`, requirement "Infrastructure exposes logger,
    ///       metric and tracer factories".
    /// @note The pointer is borrowed and valid while the `App` is alive.
    [[nodiscard]] core::Result<typename Infrastructure<Policy, 256, 8>::Logger*> logger() noexcept {
        return infra_->logger();
    }

    /// @brief Application tracer handle for one scope.
    ///
    /// @param scope Logical scope name attached to the returned tracer.
    /// @return The policy-created tracer, or the failure returned by
    ///         `Infrastructure::tracer_for()`.
    ///
    /// @note Spec `app/application`, requirement "Infrastructure exposes logger,
    ///       metric and tracer factories".
    /// @note The pointer is borrowed and valid while the `App` is alive.
    [[nodiscard]] core::Result<typename Infrastructure<Policy, 256, 8>::TracerType*>
    tracer_for(std::string_view scope) noexcept {
        return infra_->tracer_for(scope);
    }

    /// @brief Collected cold-path failures from instrument/port declarations.
    ///
    /// @return A non-owning view over the diagnostics retained since `create`.
    ///         The span is empty when every declaration succeeded and stays
    ///         valid until the next declaration or `App` destruction.
    ///
    /// @note Spec `app/runtime`, requirement "App exposes cold-path diagnostics".
    /// @note `run()` rejects a non-empty set with `InvalidArgument`; callers can
    ///       print the entries before shutdown for a precise failure report.
    [[nodiscard]] std::span<const core::SettingsDiagnostic> diagnostics() const noexcept {
        return diagnostics_;
    }

    /// @brief Render the application runtime for diagnostics.
    ///
    /// @return Text naming the application, time-base mode, initialized state and
    ///         diagnostic count. Cold path only.
    ///
    /// @note Spec `app/runtime`, requirement "App exposes cold-path diagnostics".
    /// @note The `std::formatter` specialization delegates to this member, so
    ///       `std::format("{}", app)` and `to_string()` agree.
    [[nodiscard]] std::string to_string() const {
        return std::format("App(name={}, mode={}, initialized={}, diagnostics={})", app_name_,
                           app::to_string(mode_), infra_ != nullptr, diagnostics_.size());
    }

private:
    /// @brief Opaque base for the one type-erased input subscription.
    ///
    /// Exposes only the `com::IEventConsumer` reference needed by
    /// `EventConsumerSet<1>`, keeping the callback and loss-handler types out of
    /// the `App` layout.
    class SubscriptionBase {
    public:
        /// @brief Virtual destructor for the type-erased owner.
        virtual ~SubscriptionBase() = default;

        /// @brief Consumer attachment used by the WaitSet.
        /// @return The stored consumer as the non-template interface.
        [[nodiscard]] virtual com::IEventConsumer& consumer() noexcept = 0;
    };

    /// @brief Concrete owner of one event channel and consumer.
    ///
    /// @tparam T Trivially copyable payload type.
    /// @tparam OnEvent Callback type invoked with the header and payload.
    /// @tparam OnLoss Callback type invoked when the consumer reports loss.
    template <class T, class OnEvent, class OnLoss>
    class Subscription final : public SubscriptionBase {
    public:
        /// @brief Store the channel and consumer that must share a lifetime.
        ///
        /// @param channel Event channel owning the service identity.
        /// @param consumer Consumer attached to the App WaitSet.
        Subscription(com::EventChannel<T> channel, com::EventConsumer<T, OnEvent, OnLoss> consumer)
            : channel_(std::move(channel)), consumer_(std::move(consumer)) {}

        /// @brief Consumer attachment used by the WaitSet.
        /// @return The stored consumer as the non-template interface.
        [[nodiscard]] com::IEventConsumer& consumer() noexcept override { return consumer_; }

    private:
        com::EventChannel<T> channel_; ///< Keeps the service identity alive with the consumer.
        com::EventConsumer<T, OnEvent, OnLoss> consumer_; ///< Owns the callbacks.
    };

    /// @brief Opaque base for one optional output port.
    ///
    /// Keeps the payload type out of `App` while retaining the channel and
    /// producer for the object lifetime.
    class OutputPortBase {
    public:
        /// @brief Virtual destructor for the type-erased owner.
        virtual ~OutputPortBase() = default;
    };

    /// @brief Concrete owner of one output channel and producer.
    ///
    /// @tparam T Trivially copyable payload type.
    template <class T> class OutputPort final : public OutputPortBase {
    public:
        /// @brief Store the channel and producer that must share a lifetime.
        ///
        /// @param channel Event channel owning the service identity.
        /// @param producer Producer borrowed by the caller through `publish()`.
        OutputPort(com::EventChannel<T> channel, com::EventProducer<T> producer)
            : channel_(std::move(channel)), producer_(std::move(producer)) {}

        /// @brief Producer handle returned to the application.
        /// @return The stored producer; the reference stays valid with this port.
        [[nodiscard]] com::EventProducer<T>& producer() noexcept { return producer_; }

    private:
        com::EventChannel<T> channel_;   ///< Keeps the service alive with the producer.
        com::EventProducer<T> producer_; ///< Borrowed producer returned to the caller.
    };

    /// @brief Construct an empty shell; callers must use `create()`.
    ///
    /// @note Private so a partially initialized `App` can never escape without
    ///       the diagnostics, `SIGPIPE` and Infrastructure state.
    App() noexcept = default;

    /// @brief Load settings from the supplied directories or build defaults.
    ///
    /// Scans for `-gs/--global-settings` or `-ls/--local-settings` first. With a
    /// directory, the strict parser and `load_app_settings` run unchanged; on
    /// `--help` the usage text is printed and `help_requested_` is set. Without
    /// a directory, `Settings::create(app_name_)` is used, the one-second
    /// standalone metric harvest cadence is enabled, and only identity/log-level
    /// options are parsed.
    ///
    /// @param argc Argument count from `main`.
    /// @param argv Argument vector from `main`.
    /// @param out Receives the merged settings on success.
    /// @return Success, `InvalidArgument` for malformed options, or the settings
    ///         loader failure unchanged.
    ///
    /// @note Spec `app/runtime`, requirement "App bootstrap loads files or uses
    ///       defaults".
    /// @note Supplying a settings directory keeps that file's harvest interval;
    ///       the one-second default applies only to the no-settings path.
    [[nodiscard]] core::Status load_or_default_settings(int argc, const char* const argv[],
                                                        app::Settings& out) noexcept {
        bool has_directory = false;
        for (int i = 1; i < argc; ++i) {
            const std::string_view token = argv[i] == nullptr ? std::string_view{} : argv[i];
            if (token == "--") {
                break;
            }
            if (token.starts_with("-gs") || token.starts_with("--global-settings") ||
                token.starts_with("-ls") || token.starts_with("--local-settings")) {
                has_directory = true;
                break;
            }
        }
        if (has_directory) {
            auto parsed = app::parse_command_line(argc, argv);
            if (!parsed) {
                std::fprintf(stderr, "app: %s\n", parsed.error().to_string().c_str());
                return std::unexpected(core::ErrorCode::InvalidArgument);
            }
            if (parsed->help) {
                print_usage();
                help_requested_ = true;
                return {};
            }
            auto loaded = app::load_app_settings(*parsed, out);
            if (!loaded) {
                std::fprintf(stderr, "app: settings failed: %s\n",
                             std::string(core::to_string(loaded.error())).c_str());
                return loaded;
            }
            return {};
        }

        out = app::Settings::create(app_name_);
        // The aggregate default has metric harvesting disabled (interval 0).
        // A standalone quickstart still expects its counters to reach the
        // recorder, so the no-settings path enables the normal one-second
        // harvest cadence. A supplied settings file keeps whatever it says.
        out.metrics.options.collect_interval = std::chrono::seconds{1};
        auto parsed = parse_identity_options(argc, argv, out);
        if (!parsed) {
            return parsed;
        }
        return {};
    }

    /// @brief Apply one identity option value to the parsed options.
    ///
    /// @param name Canonical long or accepted short option name.
    /// @param value Option value to store or parse as a log level.
    /// @param options Accumulator updated in place.
    /// @return Success, or `InvalidArgument` when a log level is unknown.
    [[nodiscard]] static core::Status
    apply_identity_value(std::string_view name, std::string_view value,
                         app::CommandLineOptions& options) noexcept {
        if (name == "--app-name" || name == "-an") {
            options.app_name = std::string(value);
            return {};
        }
        if (name == "--app-id" || name == "-ai") {
            options.app_id = std::string(value);
            return {};
        }
        core::LogLevel level = core::LogLevel::Info;
        if (!core::from_string(value, level)) {
            std::fprintf(stderr, "app: invalid --log-level\n");
            return std::unexpected(core::ErrorCode::InvalidArgument);
        }
        options.log_level = level;
        return {};
    }

    /// @brief Whether a long/short name is accepted by the identity-only parser.
    ///
    /// @param name Token name without any `=value` suffix.
    /// @return `true` for `--app-name`, `--app-id`, and `--log-level` plus their
    ///         short aliases.
    [[nodiscard]] static bool is_identity_option(std::string_view name) noexcept {
        return name == "--app-name" || name == "-an" || name == "--app-id" || name == "-ai" ||
               name == "--log-level" || name == "-lgl";
    }

    /// @brief Consume the adjacent short form (`-aiVALUE`), if present.
    ///
    /// @param token One raw command-line token.
    /// @param options Accumulator updated when the token is recognized.
    /// @param handled Set to `true` only when the token matched the adjacent form.
    /// @return Success, or the log-level parse failure for `-lglVALUE`.
    [[nodiscard]] static core::Status apply_adjacent_short(std::string_view token,
                                                           app::CommandLineOptions& options,
                                                           bool& handled) noexcept {
        handled = false;
        if (token.size() > 3U && token.starts_with("-ai") && token[3] != '=') {
            options.app_id = std::string(token.substr(3));
            handled = true;
            return {};
        }
        if (token.size() > 3U && token.starts_with("-an") && token[3] != '=') {
            options.app_name = std::string(token.substr(3));
            handled = true;
            return {};
        }
        if (token.size() > 4U && token.starts_with("-lgl") && token[4] != '=') {
            handled = true;
            return apply_identity_value("--log-level", token.substr(4), options);
        }
        return {};
    }

    /// @brief Consume one identity/named option token.
    ///
    /// Handles the adjacent short form first, then the separated and `=value`
    /// forms. Advances @p i over a separated value when the token carries no
    /// inline value.
    ///
    /// @param argc Argument count from `main`.
    /// @param argv Argument vector from `main`.
    /// @param i Index of the token under inspection; advanced on a separated value.
    /// @param options Accumulator updated in place.
    /// @return Success, or `InvalidArgument` for an unknown option, a missing
    ///         value, or an unknown log level.
    [[nodiscard]] static core::Status
    consume_identity_token(int argc, const char* const argv[], int& i,
                           app::CommandLineOptions& options) noexcept {
        const std::string_view token = argv[i] == nullptr ? std::string_view{} : argv[i];
        bool adjacent = false;
        auto adjacent_status = apply_adjacent_short(token, options, adjacent);
        if (!adjacent_status) {
            return adjacent_status;
        }
        if (adjacent) {
            return {};
        }
        std::string_view name = token;
        std::string_view value{};
        const std::size_t equals = token.find('=');
        if (equals != std::string_view::npos) {
            name = token.substr(0, equals);
            value = token.substr(equals + 1);
        }
        if (!is_identity_option(name)) {
            std::fprintf(stderr, "app: unknown option %.*s\n", static_cast<int>(name.size()),
                         name.data());
            return std::unexpected(core::ErrorCode::InvalidArgument);
        }
        if (value.empty()) {
            if (i + 1 >= argc || argv[i + 1] == nullptr) {
                std::fprintf(stderr, "app: %.*s requires a value\n", static_cast<int>(name.size()),
                             name.data());
                return std::unexpected(core::ErrorCode::InvalidArgument);
            }
            value = argv[++i];
        }
        return apply_identity_value(name, value, options);
    }

    /// @brief Parse the identity-only fallback options and apply them.
    ///
    /// Accepts `-h/--help` (prints usage and sets `help_requested_`),
    /// `-an/--app-name`, `-ai/--app-id`, and `-lgl/--log-level` in adjacent,
    /// separated, and `=` forms. A bare `--` is only accepted at the end of the
    /// argument list.
    ///
    /// @param argc Argument count from `main`.
    /// @param argv Argument vector from `main`.
    /// @param out Settings updated through `app::apply_to`.
    /// @return Success, or `InvalidArgument` for malformed or unknown options.
    ///
    /// @note Spec `app/runtime`, requirements "App bootstrap loads files or uses
    ///       defaults" and "Application identity is required".
    [[nodiscard]] core::Status parse_identity_options(int argc, const char* const argv[],
                                                      app::Settings& out) noexcept {
        app::CommandLineOptions options{};
        for (int i = 1; i < argc; ++i) {
            const std::string_view token = argv[i] == nullptr ? std::string_view{} : argv[i];
            if (token == "--") {
                if (i + 1 < argc) {
                    std::fprintf(stderr, "app: unexpected positional argument\n");
                    return std::unexpected(core::ErrorCode::InvalidArgument);
                }
                break;
            }
            if (token == "-h" || token == "--help") {
                print_usage();
                help_requested_ = true;
                return {};
            }
            auto consumed = consume_identity_token(argc, argv, i, options);
            if (!consumed) {
                return consumed;
            }
        }
        app::apply_to(options, out);
        if (!options.app_id.empty() && out.app_id.empty()) {
            std::fprintf(stderr, "app: invalid --app-id\n");
            return std::unexpected(core::ErrorCode::InvalidArgument);
        }
        return {};
    }

    /// @brief Print the identity-only usage text to stdout.
    ///
    /// @note Cold path only; used by both `--help` paths.
    static void print_usage() noexcept {
        constexpr std::string_view usage =
            "Usage: <program> --app-id <id> [--app-name <name>] [--log-level <level>]\n"
            "       [--global-settings <absolute-dir> | --local-settings <relative-dir>]\n"
            "Defaults are used when neither settings directory is supplied.\n";
        static_cast<void>(std::fwrite(usage.data(), 1U, usage.size(), stdout));
    }

    /// @brief Create the app meter lazily on the first instrument request.
    ///
    /// @return `true` when the meter exists, otherwise `false` after recording a
    ///         diagnostic naming `meter`.
    ///
    /// @note Spec `app/runtime`, requirement "Declared instruments and ports must
    ///       be valid before run".
    [[nodiscard]] bool ensure_meter() noexcept {
        if (meter_ != nullptr) {
            return true;
        }
        auto meter = infra_->meter(app_name_, "1.0");
        if (!meter) {
            record_failure("meter", core::to_string(meter.error()));
            return false;
        }
        meter_ = *meter;
        return true;
    }

    /// @brief Retain one cold-path declaration failure.
    ///
    /// @param field Declaration name reported to the caller.
    /// @param message Human-readable failure detail.
    ///
    /// @note Spec `app/runtime`, requirement "App exposes cold-path diagnostics".
    void record_failure(std::string_view field, std::string_view message) {
        diagnostics_.push_back(core::SettingsDiagnostic{core::SettingsSource::Computed,
                                                        std::string(field), std::string(message)});
    }

    std::string app_name_{}; ///< Effective long application name.
    core::AppId app_id_{};   ///< Required canonical identity for headers and telemetry.
    TimeBaseMode mode_{TimeBaseMode::External};            ///< Selected time-base ownership.
    std::unique_ptr<detail::EmbeddedTimeBase> embedded_{}; ///< Local base in Embedded mode.
    std::unique_ptr<com::Node> node_{}; ///< One transport node for input and outputs.
    std::unique_ptr<Infrastructure<Policy, 256, 8>> infra_{};        ///< Telemetry/runtime owner.
    metrics::Meter* meter_{nullptr};                                 ///< Lazily created app meter.
    std::vector<std::unique_ptr<SubscriptionBase>> subscriptions_{}; ///< Type-erased inputs.
    std::optional<com::EventConsumerSet<max_subscriptions>> consumer_set_{}; ///< Input WaitSet.
    std::vector<std::unique_ptr<OutputPortBase>> outputs_{}; ///< Type-erased output ports.
    std::vector<core::SettingsDiagnostic> diagnostics_{};    ///< Retained cold-path failures.
    std::unique_ptr<detail::SigpipeGuard> sigpipe_{};        ///< Process-global SIGPIPE guard.
    bool help_requested_{false};                             ///< `--help` short-circuited `create`.
    bool shutdown_done_{false};                              ///< Idempotence bit for `shutdown()`.
    com::ChannelConfig config_{}; ///< Channel limits shared by input and outputs.
};

} // namespace nexigen::app

/// @brief `std::formatter` for the application runtime.
///
/// @tparam Policy Stateless `Infrastructure` policy of the formatted `App`.
template <class Policy>
struct std::formatter<nexigen::app::App<Policy>> : std::formatter<std::string_view> {
    /// @brief Format the runtime via its `to_string` diagnostic.
    ///
    /// @tparam Ctx Format context type.
    /// @param app Runtime to render.
    /// @param ctx Format context receiving the text.
    /// @return Iterator past the last emitted character.
    ///
    /// @note Spec `app/runtime`, requirement "App exposes cold-path diagnostics".
    template <class Ctx> auto format(const nexigen::app::App<Policy>& app, Ctx& ctx) const {
        const std::string text = app.to_string();
        return std::formatter<std::string_view>::format(text, ctx);
    }
};
