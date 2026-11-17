// nexigen::com — empty event payload.
#pragma once

#include <format>
#include <string>
#include <string_view>

namespace nexigen::com {

/// @brief Payload placeholder for events that carry only an `EventHeader`.
///
/// @note Spec `com/event-system`, requirement "Event payload contract"; the
///       type is trivially copyable and satisfies `EventPayload`.
struct EmptyPayload {
    /// @brief Render the cold-path diagnostic string.
    /// @return `EmptyPayload()`.
    [[nodiscard]] std::string to_string() const { return "EmptyPayload()"; }
};

/// @brief Free helper mirroring `EmptyPayload::to_string`.
/// @param payload Payload to render.
/// @return The diagnostic string.
[[nodiscard]] inline std::string to_string(const EmptyPayload& payload) {
    return payload.to_string();
}

} // namespace nexigen::com

/// @brief Formats an empty payload as its diagnostic string.
template <> struct std::formatter<nexigen::com::EmptyPayload> : std::formatter<std::string_view> {
    /// @brief Write the payload diagnostic into the format context.
    /// @tparam Ctx Format context type.
    /// @param payload Payload to format.
    /// @param ctx Output context.
    /// @return Iterator past the last written character.
    template <class Ctx> auto format(const nexigen::com::EmptyPayload& payload, Ctx& ctx) const {
        return std::formatter<std::string_view>::format(payload.to_string(), ctx);
    }
};
