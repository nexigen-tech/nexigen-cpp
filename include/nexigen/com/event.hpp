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

/// Emits event notifications for one service.
class EventNotifier {
public:
    /// Open (or create) the event service on the node and create the notifier.
    [[nodiscard]] static Result<EventNotifier> create(Node& node, const ServiceName& name) noexcept {
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

    EventNotifier(const EventNotifier&) = delete;
    EventNotifier& operator=(const EventNotifier&) = delete;
    EventNotifier(EventNotifier&&) noexcept = default;
    EventNotifier& operator=(EventNotifier&&) noexcept = default;

    /// Notify all listeners with the default event id.
    [[nodiscard]] Status notify() noexcept {
        auto notified = inner_.notify();
        if (!notified) {
            return std::unexpected(detail::map_error(notified.error()));
        }
        return {};
    }

    /// Notify all listeners with an application-defined event id.
    [[nodiscard]] Status notify_with(EventId id) noexcept {
        auto notified = inner_.notify_with_custom_event_id(id.handle());
        if (!notified) {
            return std::unexpected(detail::map_error(notified.error()));
        }
        return {};
    }

    /// Cold-path diagnostic (mandate 9).
    [[nodiscard]] std::string to_string() const {
        return std::format("EventNotifier(service={})", service_name_.to_string());
    }

private:
    EventNotifier(ServiceName name, iox2::Notifier<iox2::ServiceType::Ipc> inner) noexcept
        : service_name_(std::move(name)), inner_(std::move(inner)) {}

    ServiceName service_name_;
    iox2::Notifier<iox2::ServiceType::Ipc> inner_;
};

/// Free helper (mandate 9): mirrors the member.
[[nodiscard]] inline std::string to_string(const EventNotifier& notifier) {
    return notifier.to_string();
}

/// Receives event notifications for one service.
class EventListener {
public:
    /// Open (or create) the event service on the node and create the listener.
    [[nodiscard]] static Result<EventListener> create(Node& node, const ServiceName& name) noexcept {
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

    EventListener(const EventListener&) = delete;
    EventListener& operator=(const EventListener&) = delete;
    EventListener(EventListener&&) noexcept = default;
    EventListener& operator=(EventListener&&) noexcept = default;

    /// Block until one notification batch arrives; the callback runs for each
    /// activation. No polling: the listener sleeps in the kernel.
    template <class Callback>
    [[nodiscard]] Status blocking_wait(Callback&& callback) noexcept {
        auto waited = inner_.blocking_wait(
            iox2::bb::StaticFunction<void(iox2::EventActivation)>{std::forward<Callback>(callback)});
        if (!waited) {
            return std::unexpected(detail::map_error(waited.error()));
        }
        return {};
    }

    /// Cold-path diagnostic (mandate 9).
    [[nodiscard]] std::string to_string() const {
        return std::format("EventListener(service={})", service_name_.to_string());
    }

private:
    EventListener(ServiceName name, iox2::Listener<iox2::ServiceType::Ipc> inner) noexcept
        : service_name_(std::move(name)), inner_(std::move(inner)) {}

    ServiceName service_name_;
    iox2::Listener<iox2::ServiceType::Ipc> inner_;
};

/// Free helper (mandate 9): mirrors the member.
[[nodiscard]] inline std::string to_string(const EventListener& listener) {
    return listener.to_string();
}

} // namespace nexigen::com

/// The notifier formats as its diagnostic string (mandate 11).
template <> struct std::formatter<nexigen::com::EventNotifier> : std::formatter<std::string_view> {
    template <class Ctx>
    auto format(const nexigen::com::EventNotifier& notifier, Ctx& ctx) const {
        const std::string text = notifier.to_string();
        return std::formatter<std::string_view>::format(text, ctx);
    }
};

/// The listener formats as its diagnostic string (mandate 11).
template <> struct std::formatter<nexigen::com::EventListener> : std::formatter<std::string_view> {
    template <class Ctx>
    auto format(const nexigen::com::EventListener& listener, Ctx& ctx) const {
        const std::string text = listener.to_string();
        return std::formatter<std::string_view>::format(text, ctx);
    }
};
