// nexigen::com — zero-copy subscriber (change add-com-module;
// spec com/publish-subscribe "Subscriber receive" / "Port diagnostics").
//
// Thin wrapper over iox2::Subscriber: receive() returns a read-only Sample<T>
// view of the same shared-memory slot the publisher wrote.
#pragma once

#include <format>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>

#include <iox2/iceoryx2.hpp>

#include <nexigen/com/concepts/message_concept.hpp>
#include <nexigen/com/detail/error_mapping.hpp>
#include <nexigen/com/error.hpp>
#include <nexigen/com/node.hpp>
#include <nexigen/com/sample.hpp>
#include <nexigen/com/service_name.hpp>

namespace nexigen::com {

/// Zero-copy subscriber of payload type T for one service (optional custom
/// header H, default void = no header).
template <concepts::Message T, typename H = void>
class Subscriber {
    static_assert(std::is_void_v<H> || concepts::Message<H>,
                  "the custom header must satisfy com::concepts::Message");
public:
    /// Open (or create) the service on the node and create the subscriber
    /// with the service-default buffer size.
    [[nodiscard]] static Result<Subscriber> create(Node& node, const ServiceName& name) noexcept {
        return create_impl(node, name, std::nullopt);
    }

    /// Open (or create) the service on the node and create the subscriber
    /// with an explicit receiver buffer size (minimum 1). Small buffers let
    /// tests and diagnostics exercise the overflow path deterministically.
    [[nodiscard]] static Result<Subscriber> create(Node& node, const ServiceName& name,
                                                   std::uint64_t buffer_size) noexcept {
        return create_impl(node, name, buffer_size);
    }

    Subscriber(const Subscriber&) = delete;
    Subscriber& operator=(const Subscriber&) = delete;
    Subscriber(Subscriber&&) noexcept = default;
    Subscriber& operator=(Subscriber&&) noexcept = default;

    /// Receive the next available sample; ok with no value when the queue is
    /// momentarily empty.
    [[nodiscard]] Result<std::optional<Sample<T, H>>> receive() noexcept {
        auto received = inner_.receive();
        if (!received) {
            return std::unexpected(detail::map_error(received.error()));
        }
        if (!received->has_value()) {
            return std::nullopt;
        }
        return Sample<T, H>{std::move(*(*received))};
    }

    /// Cold-path diagnostic (mandate 9): the service name.
    [[nodiscard]] std::string to_string() const {
        return std::format("Subscriber(service={})", service_name_.to_string());
    }

private:
    static Result<Subscriber> create_impl(Node& node, const ServiceName& name,
                                          std::optional<std::uint64_t> buffer_size) noexcept {
        auto service = [&]() {
            auto builder = node.handle().service_builder(name.handle()).publish_subscribe<T>();
            if constexpr (std::is_void_v<H>) {
                return std::move(builder).enable_safe_overflow(false).open_or_create();
            } else {
                return std::move(builder).template user_header<H>()
                    .enable_safe_overflow(false)
                    .open_or_create();
            }
        }();
        if (!service) {
            return std::unexpected(detail::map_error(service.error()));
        }
        if (buffer_size.has_value()) {
            auto subscriber = service->subscriber_builder().buffer_size(*buffer_size).create();
            if (!subscriber) {
                return std::unexpected(detail::map_error(subscriber.error()));
            }
            return Subscriber{name, std::move(*subscriber)};
        }
        auto subscriber = service->subscriber_builder().create();
        if (!subscriber) {
            return std::unexpected(detail::map_error(subscriber.error()));
        }
        return Subscriber{name, std::move(*subscriber)};
    }

    Subscriber(ServiceName name, iox2::Subscriber<iox2::ServiceType::Ipc, T, H> inner) noexcept
        : service_name_(std::move(name)), inner_(std::move(inner)) {}

    ServiceName service_name_;
    iox2::Subscriber<iox2::ServiceType::Ipc, T, H> inner_;
};

/// Free helper (mandate 9): mirrors the member.
template <concepts::Message T, typename H = void>
[[nodiscard]] inline std::string to_string(const Subscriber<T, H>& subscriber) {
    return subscriber.to_string();
}

} // namespace nexigen::com

/// The subscriber formats as its diagnostic string (mandate 11).
template <nexigen::com::concepts::Message T, typename H>
struct std::formatter<nexigen::com::Subscriber<T, H>> : std::formatter<std::string_view> {
    template <class Ctx>
    auto format(const nexigen::com::Subscriber<T, H>& subscriber, Ctx& ctx) const {
        const std::string text = subscriber.to_string();
        return std::formatter<std::string_view>::format(text, ctx);
    }
};
