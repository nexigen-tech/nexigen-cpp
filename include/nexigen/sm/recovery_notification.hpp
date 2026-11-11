// nexigen::sm — PHM recovery notification payload (change add-sm;
// spec sm/recovery-handler "PHM fault-event interface"; design D5).
//
// The shared payload the PHM daemon publishes to the SM when a
// safety-critical process expires: the failed Function Group name in a
// trivially copyable fixed buffer. Shared between nexigen::phm (publisher)
// and nexigen::sm (receiver); header-only, no com transport dependencies.
#pragma once

#include <cstddef>
#include <format>
#include <string>

#include <nexigen/com/concepts/message_concept.hpp>

namespace nexigen::sm {

/// Maximum length of the function-group name in a fault notification.
inline constexpr std::size_t k_recovery_name_length = 32;

/// One PHM fault notification (trivially copyable).
struct RecoveryNotification {
    char function_group[k_recovery_name_length]{};

    /// Cold-path diagnostic (mandate 9).
    [[nodiscard]] std::string to_string() const {
        return std::format("RecoveryNotification(function_group={})", std::string(function_group));
    }
};

/// The payload contract: plain bytes, safe for zero-copy transport.
static_assert(com::concepts::Message<RecoveryNotification>);

/// Free helper (mandate 9): mirrors the member.
[[nodiscard]] inline std::string to_string(const RecoveryNotification& notification) {
    return notification.to_string();
}

} // namespace nexigen::sm

/// The notification formats as its diagnostic string (mandate 11).
template <>
struct std::formatter<nexigen::sm::RecoveryNotification> : std::formatter<std::string_view> {
    template <class Ctx>
    auto format(const nexigen::sm::RecoveryNotification& notification, Ctx& ctx) const {
        const std::string text = notification.to_string();
        return std::formatter<std::string_view>::format(text, ctx);
    }
};
