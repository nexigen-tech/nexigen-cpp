#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include <nexigen/hw/can/can_message.hpp>

namespace nexigen::hw::can {

/// @brief Runtime filters applied by the CAN monitor before rendering.
struct CanMonitorFilter {
    std::string channel{};                              ///< Channel name ("" = all).
    std::uint32_t id{0};                                ///< CAN identifier.
    bool has_id{false};                                 ///< Whether `id` is active.
    CanMessageKind kind{CanMessageKind::ClassicData};   ///< Kind filter.
    bool has_kind{false};                               ///< Whether `kind` is active.
    CanMessageOrigin origin{CanMessageOrigin::Unknown}; ///< Origin filter.
    bool has_origin{false};                             ///< Whether `origin` is active.
    bool errors_only{false};                            ///< Show only error messages.
    bool json{false};                                   ///< Emit JSONL instead of text.
};

/// @brief Whether a message passes the monitor filters.
/// @param filter Active filters.
/// @param message Message to test.
/// @param channel Channel name that carried the message.
/// @return `true` when the message should be rendered.
[[nodiscard]] inline bool matches(const CanMonitorFilter& filter, const CanMessage& message,
                                  std::string_view channel) {
    if (!filter.channel.empty() && filter.channel != channel) {
        return false;
    }
    if (filter.has_id && message.can_id != filter.id) {
        return false;
    }
    if (filter.has_kind && message.kind != filter.kind) {
        return false;
    }
    if (filter.has_origin && message.origin != filter.origin) {
        return false;
    }
    if (filter.errors_only && message.kind != CanMessageKind::Error) {
        return false;
    }
    return true;
}

} // namespace nexigen::hw::can
