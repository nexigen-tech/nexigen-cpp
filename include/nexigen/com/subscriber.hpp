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
#include <nexigen/com/publish_subscribe_options.hpp>
#include <nexigen/com/sample.hpp>
#include <nexigen/com/service_name.hpp>

namespace nexigen::com {

/// @brief Zero-copy subscriber of payload type `T` for one service.
///
/// The consuming endpoint of the `com` publish-subscribe transport. It wraps
/// `iox2::Subscriber` so application code never names an `iox2::` type (spec
/// `com/service-management`, "Protocol-independent boundary"). `receive()`
/// returns a `Sample<T, H>` that is a read-only view of the same shared-memory
/// slot the publisher wrote: no serialization and no copy between buffers.
/// Dropping the sample releases the slot for reuse.
///
/// Discovery is daemon-less: `create()` opens the service when it already
/// exists and creates it otherwise, so a subscriber may be created before any
/// publisher appears and will connect once it does.
///
/// The subscriber owns the transport handle, so copying is disabled and
/// moving transfers ownership. `receive()` is a non-blocking, `noexcept` poll
/// that must be called from a single thread; the instance is not internally
/// synchronized.
///
/// @tparam T Payload type; constrained by `com::concepts::Message`, i.e.
///         `std::is_trivially_copyable_v<T>`.
/// @tparam H Optional custom header type attached by the publisher; `void`
///         (the default) means no header, otherwise `H` must also satisfy
///         `com::concepts::Message`.
/// @note Implements `com/publish-subscribe`: "Subscriber receive",
///       "Non-blocking overflow", and "Port diagnostics".
template <concepts::Message T, typename H = void> class Subscriber {
    static_assert(std::is_void_v<H> || concepts::Message<H>,
                  "the custom header must satisfy com::concepts::Message");

public:
    /// @brief Open or create the service with the transport-default buffer.
    ///
    /// @param node Node that owns this process's transport membership; must
    ///        outlive the subscriber.
    /// @param name Valid service name shared with the publisher.
    /// @return The constructed subscriber, or a mapped `com::Error`
    ///         (`ConnectionFailure`, `IncompatibleTypeLayout`, ...) when the
    ///         service cannot be opened or the layout does not match.
    /// @note Spec `com/publish-subscribe`, requirement "Subscriber receive";
    ///       spec `com/service-management`, requirement "Node and daemon-less
    ///       discovery". `IncompatibleTypeLayout` rejects a payload whose type
    ///       name, size, or alignment differs from the publisher's.
    [[nodiscard]] static Result<Subscriber> create(Node& node, const ServiceName& name) noexcept {
        return create(node, name, PublishSubscribeOptions{});
    }

    /// @brief Open or create the service with optional service settings.
    ///
    /// Applies the supplied service-level limits, history, safe-overflow policy,
    /// and optional per-subscriber buffer size. Zero numeric values keep the
    /// transport default.
    ///
    /// @param node Node that owns this process's transport membership.
    /// @param name Valid service name shared with the publisher.
    /// @param options Optional service-level and subscriber settings.
    /// @return The constructed subscriber, or a mapped `com::Error` on failure.
    /// @note Spec `com/publish-subscribe`, requirement "Publish-subscribe
    ///       service options".
    [[nodiscard]] static Result<Subscriber> create(Node& node, const ServiceName& name,
                                                   const PublishSubscribeOptions& options) noexcept {
        auto builder = node.handle().service_builder(name.handle()).publish_subscribe<T>();
        if (options.max_subscribers > 0U) {
            static_cast<void>(std::move(builder).max_subscribers(options.max_subscribers));
        }
        if (options.max_publishers > 0U) {
            static_cast<void>(std::move(builder).max_publishers(options.max_publishers));
        }
        if (options.max_nodes > 0U) {
            static_cast<void>(std::move(builder).max_nodes(options.max_nodes));
        }
        if (options.subscriber_max_buffer_size > 0U) {
            static_cast<void>(std::move(builder).subscriber_max_buffer_size(
                options.subscriber_max_buffer_size));
        }
        if (options.history_size > 0U) {
            static_cast<void>(std::move(builder).history_size(options.history_size));
        }
        auto service = [&]() {
            if constexpr (std::is_void_v<H>) {
                return std::move(builder).enable_safe_overflow(options.enable_safe_overflow)
                    .open_or_create();
            } else {
                return std::move(builder)
                    .template user_header<H>()
                    .enable_safe_overflow(options.enable_safe_overflow)
                    .open_or_create();
            }
        }();
        if (!service) {
            return std::unexpected(detail::map_error(service.error()));
        }
        auto subscriber_builder = service->subscriber_builder();
        if (options.subscriber_buffer_size > 0U) {
            static_cast<void>(
                std::move(subscriber_builder).buffer_size(options.subscriber_buffer_size));
        }
        if (options.subscriber_history_request > 0U) {
            static_cast<void>(std::move(subscriber_builder).history_request(
                options.subscriber_history_request));
        }
        auto subscriber = std::move(subscriber_builder).create();
        if (!subscriber) {
            return std::unexpected(detail::map_error(subscriber.error()));
        }
        return Subscriber{name, std::move(*subscriber)};
    }

    /// @brief Open or create the service with an explicit receiver buffer size.
    ///
    /// Compatibility overload: sets the per-subscriber buffer and leaves the
    /// service-level settings at their transport defaults.
    ///
    /// @param node Node that owns this process's transport membership.
    /// @param name Valid service name shared with the publisher.
    /// @param buffer_size Number of samples this subscriber may hold.
    /// @return The constructed subscriber, or a mapped `com::Error` on failure.
    /// @note Spec `com/publish-subscribe`, requirement "Configurable subscriber
    ///       buffer depth".
    [[nodiscard]] static Result<Subscriber> create(Node& node, const ServiceName& name,
                                                   std::uint64_t buffer_size) noexcept {
        PublishSubscribeOptions options{};
        options.subscriber_buffer_size = buffer_size;
        return create(node, name, options);
    }

    /// @brief Open or create the service with explicit service maximum and buffer.
    ///
    /// Compatibility overload: sets the service maximum and the per-subscriber
    /// buffer; other service settings stay at their transport defaults.
    ///
    /// @param node Node that owns this process's transport membership.
    /// @param name Valid service name shared with the publisher.
    /// @param subscriber_max_buffer_size Maximum number of samples one
    ///        subscriber may hold for this service.
    /// @param buffer_size Number of samples this subscriber requests to hold.
    /// @return The constructed subscriber, or a mapped `com::Error` on failure.
    /// @note Spec `com/publish-subscribe`, requirement "Configurable subscriber
    ///       buffer depth".
    [[nodiscard]] static Result<Subscriber> create(Node& node, const ServiceName& name,
                                                   std::uint64_t subscriber_max_buffer_size,
                                                   std::uint64_t buffer_size) noexcept {
        PublishSubscribeOptions options{};
        options.subscriber_max_buffer_size = subscriber_max_buffer_size;
        options.subscriber_buffer_size = buffer_size;
        return create(node, name, options);
    }

    /// @brief Copying is disabled: the subscriber owns the transport handle.
    Subscriber(const Subscriber&) = delete;

    /// @brief Copy assignment is disabled: the subscriber owns the transport
    ///        handle.
    Subscriber& operator=(const Subscriber&) = delete;

    /// @brief Move-construct a subscriber, transferring the transport handle.
    ///
    /// The moved-from object no longer owns a handle and must not receive.
    Subscriber(Subscriber&&) noexcept = default;

    /// @brief Move-assign a subscriber, transferring the transport handle.
    ///
    /// The destination's previous handle is released; self-assignment is a
    /// no-op.
    /// @return `*this`.
    Subscriber& operator=(Subscriber&&) noexcept = default;

    /// @brief Receive the next available sample.
    ///
    /// Non-blocking: returns an empty optional when the queue is momentarily
    /// empty, so the caller decides whether to retry or wait on an
    /// `EventListener`.
    ///
    /// @return The received `Sample<T, H>` view, an empty optional when no
    ///         sample is pending, or a mapped `com::Error` (for example
    ///         `ConnectionFailure`) when the transport cannot receive.
    /// @note Spec `com/publish-subscribe`, requirement "Subscriber receive":
    ///       the sample is the producer's shared-memory slot, and dropping it
    ///       returns that slot to the pool. Single consumer: call from one
    ///       thread.
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

    /// @brief Render the cold-path diagnostic string.
    ///
    /// @return `Subscriber(service=<name>)`.
    /// @note Mandate 9 (universal `to_string`); `com/publish-subscribe`,
    ///       requirement "Port diagnostics". Allocates, so cold path only.
    [[nodiscard]] std::string to_string() const {
        return std::format("Subscriber(service={})", service_name_.to_string());
    }

private:
    /// @brief Adopt an already-created transport subscriber.
    ///
    /// @param name Service name retained for `to_string()`.
    /// @param inner Transport subscriber returned by the iceoryx2 builder.
    /// @note Private by design: the only supported construction path is
    ///       `create()`.
    Subscriber(ServiceName name, iox2::Subscriber<iox2::ServiceType::Ipc, T, H> inner) noexcept
        : service_name_(std::move(name)), inner_(std::move(inner)) {}

    ServiceName service_name_; ///< Service identity rendered by `to_string()`.
    iox2::Subscriber<iox2::ServiceType::Ipc, T, H> inner_; ///< Owned transport handle.
};

/// @brief Free helper mirroring `Subscriber::to_string()`.
///
/// @tparam T Payload type satisfying `com::concepts::Message`.
/// @tparam H Optional custom header type; `void` means no header.
/// @param subscriber Subscriber to render.
/// @return The subscriber's diagnostic string.
/// @note Mandate 9 (universal `to_string`); allocates, so cold path only.
template <concepts::Message T, typename H = void>
[[nodiscard]] inline std::string to_string(const Subscriber<T, H>& subscriber) {
    return subscriber.to_string();
}

} // namespace nexigen::com

/// @brief Formats a `Subscriber` as its diagnostic string.
///
/// @tparam T Payload type satisfying `com::concepts::Message`.
/// @tparam H Optional custom header type; `void` means no header.
/// @note Mandate 11 (universal `std::formatter`); delegates to
///       `Subscriber::to_string()`, which allocates, so cold path only.
template <nexigen::com::concepts::Message T, typename H>
struct std::formatter<nexigen::com::Subscriber<T, H>> : std::formatter<std::string_view> {
    /// @brief Write the subscriber's diagnostic text into the format context.
    ///
    /// @tparam Ctx `std::format` context type.
    /// @param subscriber Subscriber to format.
    /// @param ctx Output context supplied by `std::format`.
    /// @return Iterator one past the last written character.
    template <class Ctx>
    auto format(const nexigen::com::Subscriber<T, H>& subscriber, Ctx& ctx) const {
        const std::string text = subscriber.to_string();
        return std::formatter<std::string_view>::format(text, ctx);
    }
};
