// nexigen::logging — read-only view of the log service's live capacity shape
// (change fix-log-subscriber-capacity; spec logging/iceoryx2-logger "Log
// service capacity is a shared contract").
//
// The log service is created once and keeps its shape while any participant is
// alive. This detail helper lets the logger and the daemon compare the live
// shape with the shared constants and produce the same human-readable
// diagnostic instead of a generic transport error.
//
// COM-GATED by construction: it includes nexigen::com and iceoryx2, so only
// nexigen_logging_com consumers may include it.
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <string>
#include <string_view>

#include <iox2/iceoryx2.hpp>

#include <nexigen/com/node.hpp>
#include <nexigen/com/service_name.hpp>
#include <nexigen/logging/log_service_capacity.hpp>

namespace nexigen::logging::detail {

/// @brief Capacity fields read from a live publish-subscribe service.
///
/// Trivially copyable snapshot of the transport's static configuration. It
/// carries no ownership and is produced only on the cold startup path.
///
/// @note Spec `logging/iceoryx2-logger`, requirement "Log service capacity is a
///       shared contract".
struct LogServiceShape {
    std::uint64_t max_subscribers{0}; ///< Service subscriber limit.
    std::uint64_t max_publishers{0};  ///< Service publisher limit.
    std::uint64_t max_nodes{0};       ///< Service node limit.
    std::uint64_t buffer_depth{0};    ///< Per-subscriber sample depth.
    std::uint64_t history{0};         ///< Publisher history size.
    bool safe_overflow{false};        ///< Oldest-sample eviction flag.

    /// @brief Compare this shape with the shared log-service constants.
    ///
    /// @return `true` when every field equals the shared contract.
    /// @note Cold path only; no allocation and no transport access.
    [[nodiscard]] bool compatible() const noexcept {
        return max_subscribers == kLogServiceMaxSubscribers &&
               max_publishers == kLogServiceMaxPublishers && max_nodes == kLogServiceMaxNodes &&
               buffer_depth == kLogServiceBufferDepth && history == kLogServiceHistoryDepth &&
               safe_overflow == kLogServiceSafeOverflow;
    }

    /// @brief Render the shape for cold-path diagnostics.
    ///
    /// @return Text naming every capacity field.
    /// @note Allocates; cold path only.
    [[nodiscard]] std::string to_string() const {
        std::string out = "LogServiceShape(max_subscribers=" + std::to_string(max_subscribers);
        out += ", max_publishers=" + std::to_string(max_publishers);
        out += ", max_nodes=" + std::to_string(max_nodes);
        out += ", buffer_depth=" + std::to_string(buffer_depth);
        out += ", history=" + std::to_string(history);
        out += ", safe_overflow=" + std::string(safe_overflow ? "true" : "false") + ")";
        return out;
    }
};

/// @brief Free `to_string` mirror for the shape (mandate 9).
///
/// @param shape Shape to render.
/// @return Exactly `shape.to_string()`.
[[nodiscard]] inline std::string to_string(const LogServiceShape& shape) {
    return shape.to_string();
}

/// @brief Read the live log service's capacity shape.
///
/// @param node Node used for the transport query.
/// @param service_name NUL-terminated service name.
/// @return The live shape, or `std::nullopt` when the service is absent, the
///         name is invalid, or the transport cannot read the configuration.
/// @note Cold path only: called during startup before any worker exists.
/// @note Spec `logging/iceoryx2-logger`, requirement "Log service capacity is a
///       shared contract", scenario "Existing incompatible service is
///       rejected".
[[nodiscard]] inline std::optional<LogServiceShape>
query_log_service_shape(com::Node& node, const char* service_name) noexcept {
    if (service_name == nullptr) {
        return std::nullopt;
    }
    const auto name = com::ServiceName::create(service_name);
    if (!name.has_value()) {
        return std::nullopt;
    }
    auto details = iox2::Service<iox2::ServiceType::Ipc>::details(
        name->handle(), node.handle().config(), iox2::MessagingPattern::PublishSubscribe);
    if (!details.has_value() || !details->has_value()) {
        return std::nullopt;
    }
    const auto shape = (*details)->static_details.publish_subscribe();
    return LogServiceShape{shape.max_subscribers(), shape.max_publishers(),
                           shape.max_nodes(),       shape.subscriber_max_buffer_size(),
                           shape.history_size(),    shape.has_safe_overflow()};
}

/// @brief Format the mismatch between the live shape and the shared contract.
///
/// Writes one stable sentence naming both the existing and the required
/// capacities, suitable for a startup error line.
///
/// @param service_name Service name to include (length-bounded in the output).
/// @param shape Existing live shape.
/// @param out Destination buffer.
/// @param cap Capacity of `out` in bytes.
/// @return Bytes written (excluding the NUL), or `0` when the buffer is too
///         small or `out` is null.
/// @note Cold path only; never allocates.
[[nodiscard]] inline std::size_t format_shape_mismatch(std::string_view service_name,
                                                       const LogServiceShape& shape, char* out,
                                                       std::size_t cap) noexcept {
    if (out == nullptr || cap == 0U) {
        return 0U;
    }
    const int name_len = service_name.size() < 511U ? static_cast<int>(service_name.size()) : 511;
    const int written = std::snprintf(
        out, cap,
        "log service '%.*s' exists with max_subscribers=%llu max_publishers=%llu max_nodes=%llu "
        "buffer_depth=%llu history=%llu safe_overflow=%s; required max_subscribers=%llu "
        "max_publishers=%llu max_nodes=%llu buffer_depth=%llu history=%llu safe_overflow=%s",
        name_len, service_name.data(), static_cast<unsigned long long>(shape.max_subscribers),
        static_cast<unsigned long long>(shape.max_publishers),
        static_cast<unsigned long long>(shape.max_nodes),
        static_cast<unsigned long long>(shape.buffer_depth),
        static_cast<unsigned long long>(shape.history), shape.safe_overflow ? "true" : "false",
        static_cast<unsigned long long>(kLogServiceMaxSubscribers),
        static_cast<unsigned long long>(kLogServiceMaxPublishers),
        static_cast<unsigned long long>(kLogServiceMaxNodes),
        static_cast<unsigned long long>(kLogServiceBufferDepth),
        static_cast<unsigned long long>(kLogServiceHistoryDepth),
        kLogServiceSafeOverflow ? "true" : "false");
    if (written <= 0 || static_cast<std::size_t>(written) >= cap) {
        return 0U;
    }
    return static_cast<std::size_t>(written);
}

} // namespace nexigen::logging::detail
