// nexigen::com — event notification (change add-com-module;
// spec com/publish-subscribe "Event notification on publish").
//
// Thin wrappers over iox2::Notifier / iox2::Listener: the notifier emits a
// lightweight wake-up (optionally carrying an EventId); the listener blocks
// until a notification arrives — no busy polling.
#pragma once

#include <format>
#include <string>
#include <utility>

#include <iox2/iceoryx2.hpp>

#include <nexigen/com/detail/error_mapping.hpp>
#include <nexigen/com/error.hpp>
#include <nexigen/com/event_id.hpp>
#include <nexigen/com/node.hpp>
#include <nexigen/com/service_name.hpp>

namespace nexigen::com {

/// @brief Emits event notifications for one service.
///
/// Pairs with `EventListener`: the notifier emits a lightweight wake-up (with
/// the default event id or an application-supplied `EventId`) and the listener
/// thread wakes in the kernel, without busy polling. Publishing a sample may
/// be paired with a notification so consumers do not have to poll the queue.
///
/// The notifier owns the transport handle, so copying is disabled and moving
/// transfers ownership. `notify()` and `notify_with()` are non-blocking and
/// must be called from a single thread; the instance is not internally
/// synchronized.
///
/// @note Spec `com/publish-subscribe`, requirement "Event notification on
///       publish"; `com/service-management`, requirement
///       "Protocol-independent boundary".
class EventNotifier {
public:
    /// @brief Open or create the event service on `node` and create the notifier.
    ///
    /// @param node Node that owns this process's transport membership; must
    ///        outlive the notifier.
    /// @param name Valid service name shared with the listener.
    /// @return The constructed notifier, or a mapped `com::Error`
    ///         (`ConnectionFailure`, `CreationFailed`, ...) when the service or
    ///         notifier cannot be created.
    /// @note Spec `com/publish-subscribe`, requirement "Event notification on
    ///       publish".
    [[nodiscard]] static Result<EventNotifier> create(Node& node,
                                                      const ServiceName& name) noexcept {
        auto service = node.handle().service_builder(name.handle()).event().open_or_create();
        if (!service) {
            return std::unexpected(detail::map_error(service.error()));
        }
        auto notifier = service->notifier_builder().create();
        if (!notifier) {
            return std::unexpected(detail::map_error(notifier.error()));
        }
        return EventNotifier{name, std::move(*notifier)};
    }

    /// @brief Copying is disabled: the notifier owns the transport handle.
    EventNotifier(const EventNotifier&) = delete;

    /// @brief Copy assignment is disabled: the notifier owns the transport
    ///        handle.
    EventNotifier& operator=(const EventNotifier&) = delete;

    /// @brief Move-construct a notifier, transferring the transport handle.
    ///
    /// The moved-from object no longer owns a handle and must not notify.
    EventNotifier(EventNotifier&&) noexcept = default;

    /// @brief Move-assign a notifier, transferring the transport handle.
    ///
    /// The destination's previous handle is released; self-assignment is a
    /// no-op.
    /// @return `*this`.
    EventNotifier& operator=(EventNotifier&&) noexcept = default;

    /// @brief Notify all listeners with the default event id.
    ///
    /// @return Empty `Status` on success, or a mapped `com::Error` (for
    ///         example `ConnectionFailure`) when no listener could be
    ///         notified.
    /// @note Spec `com/publish-subscribe`, requirement "Event notification on
    ///       publish": non-blocking, so a slow listener never stalls the
    ///       notifier thread.
    [[nodiscard]] Status notify() noexcept {
        auto notified = inner_.notify();
        if (!notified) {
            return std::unexpected(detail::map_error(notified.error()));
        }
        return {};
    }

    /// @brief Notify all listeners with an application-defined event id.
    ///
    /// @param id Event tag delivered to every waiting listener, for example a
    ///        checkpoint id or a trigger code.
    /// @return Empty `Status` on success, or a mapped `com::Error` when the
    ///         notification cannot be delivered.
    /// @note Spec `com/publish-subscribe`, requirement "Event notification on
    ///       publish"; spec `com/waitset`, requirement "Event notification
    ///       attachment". Non-blocking.
    [[nodiscard]] Status notify_with(EventId id) noexcept {
        auto notified = inner_.notify_with_custom_event_id(id.handle());
        if (!notified) {
            return std::unexpected(detail::map_error(notified.error()));
        }
        return {};
    }

    /// @brief Render the cold-path diagnostic string.
    ///
    /// @return `EventNotifier(service=<name>)`.
    /// @note Mandate 9 (universal `to_string`); `com/publish-subscribe`,
    ///       requirement "Port diagnostics". Allocates, so cold path only.
    [[nodiscard]] std::string to_string() const {
        return std::format("EventNotifier(service={})", service_name_.to_string());
    }

private:
    /// @brief Adopt an already-created transport notifier.
    ///
    /// @param name Service name retained for `to_string()`.
    /// @param inner Transport notifier returned by the iceoryx2 builder.
    /// @note Private by design: the only supported construction path is
    ///       `create()`.
    EventNotifier(ServiceName name, iox2::Notifier<iox2::ServiceType::Ipc> inner) noexcept
        : service_name_(std::move(name)), inner_(std::move(inner)) {}

    ServiceName service_name_;                     ///< Service identity rendered by `to_string()`.
    iox2::Notifier<iox2::ServiceType::Ipc> inner_; ///< Owned transport handle.
};

/// @brief Free helper mirroring `EventNotifier::to_string()`.
///
/// @param notifier Notifier to render.
/// @return The notifier's diagnostic string.
/// @note Mandate 9 (universal `to_string`); allocates, so cold path only.
[[nodiscard]] inline std::string to_string(const EventNotifier& notifier) {
    return notifier.to_string();
}

/// @brief Receives event notifications for one service.
///
/// Pairs with `EventNotifier`. The listener blocks the calling thread in the
/// kernel until a notification batch arrives, or drains already-pending
/// activations without blocking; in both cases the caller's callback receives
/// a `com::EventId` per activation. No polling loop is needed.
///
/// The listener owns the transport handle, so copying is disabled and moving
/// transfers ownership. Waiting must happen on a single thread; it is also the
/// source a `WaitSet` attaches through `handle()`.
///
/// @note Spec `com/publish-subscribe`, requirement "Event notification on
///       publish"; spec `com/waitset`, requirement "Event notification
///       attachment".
class EventListener {
public:
    /// @brief Open or create the event service on `node` and create the listener.
    ///
    /// @param node Node that owns this process's transport membership; must
    ///        outlive the listener.
    /// @param name Valid service name shared with the notifier.
    /// @return The constructed listener, or a mapped `com::Error`
    ///         (`ConnectionFailure`, `CreationFailed`, ...) when the service or
    ///         listener cannot be created.
    /// @note Spec `com/publish-subscribe`, requirement "Event notification on
    ///       publish".
    [[nodiscard]] static Result<EventListener> create(Node& node,
                                                      const ServiceName& name) noexcept {
        auto service = node.handle().service_builder(name.handle()).event().open_or_create();
        if (!service) {
            return std::unexpected(detail::map_error(service.error()));
        }
        auto listener = service->listener_builder().create();
        if (!listener) {
            return std::unexpected(detail::map_error(listener.error()));
        }
        return EventListener{name, std::move(*listener)};
    }

    /// @brief Copying is disabled: the listener owns the transport handle.
    EventListener(const EventListener&) = delete;

    /// @brief Copy assignment is disabled: the listener owns the transport
    ///        handle.
    EventListener& operator=(const EventListener&) = delete;

    /// @brief Move-construct a listener, transferring the transport handle.
    ///
    /// The moved-from object no longer owns a handle and must not wait.
    EventListener(EventListener&&) noexcept = default;

    /// @brief Move-assign a listener, transferring the transport handle.
    ///
    /// The destination's previous handle is released; self-assignment is a
    /// no-op.
    /// @return `*this`.
    EventListener& operator=(EventListener&&) noexcept = default;

    /// @brief Block until one notification batch arrives.
    ///
    /// The calling thread sleeps in the kernel; the callback runs once per
    /// activation with that activation's `com::EventId`, so no polling is
    /// involved.
    ///
    /// @tparam Callback Callable invocable as `void(EventId)`.
    /// @param callback Callback invoked for each activation of the batch.
    /// @return Empty `Status` on success, or a mapped `com::Error` (for
    ///         example `Timeout` or `ConnectionFailure`) when the wait fails.
    /// @note Spec `com/publish-subscribe`, requirement "Event notification on
    ///       publish". Blocking, so do not call it on a real-time hot path.
    template <class Callback> [[nodiscard]] Status blocking_wait(Callback&& callback) noexcept {
        auto adapter = [&callback](iox2::EventActivation activation) {
            callback(EventId{activation.id().as_value()});
        };
        auto waited = inner_.blocking_wait(
            iox2::bb::StaticFunction<void(iox2::EventActivation)>{std::move(adapter)});
        if (!waited) {
            return std::unexpected(detail::map_error(waited.error()));
        }
        return {};
    }

    /// @brief Drain activations that are already pending.
    ///
    /// Non-blocking; the callback runs once per pending activation with its
    /// `com::EventId`. This is the polling-free way for a WaitSet-driven loop
    /// to collect checkpoint/state ids.
    ///
    /// @tparam Callback Callable invocable as `void(EventId)`.
    /// @param callback Callback invoked for each drained activation.
    /// @return The number of activations delivered, or a mapped `com::Error`
    ///         when the drain fails.
    /// @note Spec `com/publish-subscribe`, requirement "Event notification on
    ///       publish".
    template <class Callback>
    [[nodiscard]] Result<std::uint64_t> try_wait(Callback&& callback) noexcept {
        auto adapter = [&callback](iox2::EventActivation activation) {
            callback(EventId{activation.id().as_value()});
        };
        auto collected = inner_.try_wait(
            iox2::bb::StaticFunction<void(iox2::EventActivation)>{std::move(adapter)});
        if (!collected) {
            return std::unexpected(detail::map_error(collected.error()));
        }
        return *collected;
    }

    /// @brief Render the cold-path diagnostic string.
    ///
    /// @return `EventListener(service=<name>)`.
    /// @note Mandate 9 (universal `to_string`); `com/publish-subscribe`,
    ///       requirement "Port diagnostics". Allocates, so cold path only.
    [[nodiscard]] std::string to_string() const {
        return std::format("EventListener(service={})", service_name_.to_string());
    }

    /// @brief The underlying transport handle.
    ///
    /// @return Const reference to the wrapped `iox2::Listener`.
    /// @note Module-internal surface only: the `WaitSet` attaches listeners
    ///       through it; business code must stay transport-agnostic (spec
    ///       `com/service-management`, requirement "Protocol-independent
    ///       boundary").
    [[nodiscard]] const iox2::Listener<iox2::ServiceType::Ipc>& handle() const noexcept {
        return inner_;
    }

private:
    /// @brief Adopt an already-created transport listener.
    ///
    /// @param name Service name retained for `to_string()`.
    /// @param inner Transport listener returned by the iceoryx2 builder.
    /// @note Private by design: the only supported construction path is
    ///       `create()`.
    EventListener(ServiceName name, iox2::Listener<iox2::ServiceType::Ipc> inner) noexcept
        : service_name_(std::move(name)), inner_(std::move(inner)) {}

    ServiceName service_name_;                     ///< Service identity rendered by `to_string()`.
    iox2::Listener<iox2::ServiceType::Ipc> inner_; ///< Owned transport handle.
};

/// @brief Free helper mirroring `EventListener::to_string()`.
///
/// @param listener Listener to render.
/// @return The listener's diagnostic string.
/// @note Mandate 9 (universal `to_string`); allocates, so cold path only.
[[nodiscard]] inline std::string to_string(const EventListener& listener) {
    return listener.to_string();
}

} // namespace nexigen::com

/// @brief Formats an `EventNotifier` as its diagnostic string.
///
/// @note Mandate 11 (universal `std::formatter`); delegates to
///       `EventNotifier::to_string()`, which allocates, so cold path only.
template <> struct std::formatter<nexigen::com::EventNotifier> : std::formatter<std::string_view> {
    /// @brief Write the notifier's diagnostic text into the format context.
    ///
    /// @tparam Ctx `std::format` context type.
    /// @param notifier Notifier to format.
    /// @param ctx Output context supplied by `std::format`.
    /// @return Iterator one past the last written character.
    template <class Ctx> auto format(const nexigen::com::EventNotifier& notifier, Ctx& ctx) const {
        const std::string text = notifier.to_string();
        return std::formatter<std::string_view>::format(text, ctx);
    }
};

/// @brief Formats an `EventListener` as its diagnostic string.
///
/// @note Mandate 11 (universal `std::formatter`); delegates to
///       `EventListener::to_string()`, which allocates, so cold path only.
template <> struct std::formatter<nexigen::com::EventListener> : std::formatter<std::string_view> {
    /// @brief Write the listener's diagnostic text into the format context.
    ///
    /// @tparam Ctx `std::format` context type.
    /// @param listener Listener to format.
    /// @param ctx Output context supplied by `std::format`.
    /// @return Iterator one past the last written character.
    template <class Ctx> auto format(const nexigen::com::EventListener& listener, Ctx& ctx) const {
        const std::string text = listener.to_string();
        return std::formatter<std::string_view>::format(text, ctx);
    }
};
