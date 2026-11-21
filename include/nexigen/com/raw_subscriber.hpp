// nexigen::com — type-erased subscriber for introspection tools (change
// record-telemetry-sessions; spec com/publish-subscribe "Raw introspection
// subscriber").
//
// Opens an existing publish-subscribe service by reading its payload and
// user-header type details from the transport, then receives raw bytes. The
// recorder uses it to record event channels without knowing their payload
// types.
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

#include <nexigen/com/error.hpp>
#include <nexigen/com/service_name.hpp>

namespace nexigen::com {

/// @brief Read-only subscriber that does not know the payload type at compile time.
///
/// Must be created after the service exists: the type details are queried from
/// the transport. Samples are borrowed until the next `receive()` or
/// destruction.
///
/// @note Spec `com/publish-subscribe`, requirement "Raw introspection
///       subscriber".
class RawSubscriber {
public:
    /// @brief One received sample as raw bytes.
    struct Sample {
        std::span<const std::byte> payload{};     ///< Payload bytes.
        std::span<const std::byte> user_header{}; ///< User-header bytes, empty when absent.
        std::string_view payload_type{};          ///< Payload type name from the service.
    };

    /// @brief Open an existing service and create a raw subscriber.
    ///
    /// @param name Service name; the service MUST already exist.
    /// @param buffer_size Requested subscriber queue depth (0 keeps the default).
    /// @return Subscriber, or `ServiceNotFound`/`CreationFailed`.
    [[nodiscard]] static Result<RawSubscriber> create(const ServiceName& name,
                                                      std::uint64_t buffer_size = 8U);

    RawSubscriber(const RawSubscriber&) = delete;
    RawSubscriber& operator=(const RawSubscriber&) = delete;
    /// @brief Move-construct a subscriber, transferring the borrowed handles.
    /// @param other Source subscriber; left without transport handles.
    RawSubscriber(RawSubscriber&& other) noexcept;
    /// @brief Move-assign a subscriber, transferring the borrowed handles.
    /// @param other Source subscriber; left without transport handles.
    /// @return This subscriber.
    RawSubscriber& operator=(RawSubscriber&& other) noexcept;
    ~RawSubscriber();

    /// @brief Receive the next sample.
    ///
    /// @return The raw sample, an empty optional when the queue is empty, or a
    ///         mapped error.
    [[nodiscard]] Result<std::optional<Sample>> receive() noexcept;

    /// @brief Payload type name reported by the service.
    /// @return Borrowed type name.
    [[nodiscard]] std::string_view payload_type() const noexcept { return payload_type_; }

    /// @brief Service name this subscriber opened.
    /// @return Borrowed service name.
    [[nodiscard]] std::string_view service_name() const noexcept { return service_name_text_; }

    /// @brief Payload size in bytes reported by the service.
    /// @return Payload size in bytes.
    [[nodiscard]] std::size_t payload_size() const noexcept { return payload_size_; }

private:
    RawSubscriber() = default;
    void release() noexcept;

    void* node_{nullptr};         ///< iox2_node_h
    void* service_name_{nullptr}; ///< iox2_service_name_h
    void* service_{nullptr};      ///< iox2_port_factory_pub_sub_h
    void* subscriber_{nullptr};   ///< iox2_subscriber_h
    void* sample_{nullptr};       ///< iox2_sample_h borrowed by the last receive
    std::size_t payload_size_{0};
    std::size_t user_header_size_{0};
    std::string payload_type_{};
    std::string service_name_text_{};
};

} // namespace nexigen::com
