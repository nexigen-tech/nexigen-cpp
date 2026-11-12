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

/// @brief Maximum length of the function-group name in a fault notification.
///
/// The buffer includes its terminating NUL, so a group name is at most 31
/// bytes.
inline constexpr std::size_t k_recovery_name_length = 32;

/// @brief One PHM fault notification identifying the failed Function Group.
///
/// A plain fixed `char` buffer and nothing else, so the payload is trivially
/// copyable and satisfies the com `Message` concept. PHM publishes it on the
/// recovery channel while raising the paired event; the SM reads it only after
/// its WaitSet woke on that event.
///
/// @note Spec `sm/recovery-handler`, requirement "PHM fault-event interface".
struct RecoveryNotification {
    char function_group[k_recovery_name_length]{}; ///< NUL-terminated group name.

    /// @brief Render the notification as a diagnostic string.
    ///
    /// @return Text naming the failed group; allocates through `std::string`
    ///         and `std::format`.
    /// @note Spec `sm/recovery-handler`, requirement "Recovery diagnostics".
    /// @note Cold-path diagnostic only (project mandate 9).
    [[nodiscard]] std::string to_string() const {
        return std::format("RecoveryNotification(function_group={})", std::string(function_group));
    }
};

/// @brief Compile-time proof that the notification satisfies `Message`.
///
/// The concept requires plain bytes and trivially copyable storage, so the
/// payload can travel zero-copy between PHM and the SM.
static_assert(com::concepts::Message<RecoveryNotification>);

/// @brief Free diagnostic helper mirroring `RecoveryNotification::to_string`.
///
/// @param notification Notification to render.
/// @return The member `to_string()` output.
/// @note Spec `sm/recovery-handler`, requirement "Recovery diagnostics".
/// @note Cold-path diagnostic only (project mandate 9).
[[nodiscard]] inline std::string to_string(const RecoveryNotification& notification) {
    return notification.to_string();
}

} // namespace nexigen::sm

/// @brief `std::formatter` for `RecoveryNotification` (project mandate 11).
///
/// Delegates to `std::formatter<std::string_view>` after rendering the member
/// diagnostic, so `std::format("{}", notification)` equals `to_string()`.
///
/// @note Spec `sm/recovery-handler`, requirement "Recovery diagnostics".
template <>
struct std::formatter<nexigen::sm::RecoveryNotification> : std::formatter<std::string_view> {
    /// @brief Format the notification as its diagnostic string.
    ///
    /// @tparam Ctx Format context type supplied by `std::format`.
    /// @param notification Notification to render.
    /// @param ctx Output iterator context receiving the text.
    /// @return Iterator past the written characters.
    template <class Ctx>
    auto format(const nexigen::sm::RecoveryNotification& notification, Ctx& ctx) const {
        const std::string text = notification.to_string();
        return std::formatter<std::string_view>::format(text, ctx);
    }
};
