#pragma once

#include <cstdint>
#include <format>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

namespace nexigen::hw::can {

/// @brief Features supported by one CAN driver implementation.
struct CanDriverCapabilities {
    bool supports_fd{false};         ///< Accepts CAN FD data frames.
    bool supports_remote{false};     ///< Delivers remote frames.
    bool hardware_timestamps{false}; ///< Supplies hardware timestamps.
    bool hardware_sequence{false};   ///< Supplies hardware sequence numbers.
    bool error_frames{false};        ///< Delivers error frames.
    bool overload_frames{false};     ///< Delivers overload frames.
    bool heartbeats{false};          ///< Delivers bus-idle heartbeats.
    std::uint16_t max_channels{1};   ///< Maximum simultaneous channels.

    /// @brief Render the capabilities for cold-path diagnostics.
    /// @return Human-readable summary.
    [[nodiscard]] std::string to_string() const;
};

/// @brief Free helper mirroring `CanDriverCapabilities::to_string`.
/// @param capabilities Capabilities to render.
/// @return Human-readable summary.
[[nodiscard]] inline std::string to_string(const CanDriverCapabilities& capabilities) {
    return capabilities.to_string();
}

/// @brief Serialize driver capabilities.
/// @param j Destination.
/// @param capabilities Capabilities to serialize.
void to_json(nlohmann::json& j, const CanDriverCapabilities& capabilities);

/// @brief Parse driver capabilities without throwing.
/// @param j Source.
/// @param capabilities Destination.
void from_json(const nlohmann::json& j, CanDriverCapabilities& capabilities) noexcept;

} // namespace nexigen::hw::can

/// @brief `std::formatter` for `CanDriverCapabilities`.
template <>
struct std::formatter<nexigen::hw::can::CanDriverCapabilities> : std::formatter<std::string_view> {
    /// @brief Format the capabilities through `to_string`.
    /// @tparam Ctx Format context.
    /// @param capabilities Capabilities to render.
    /// @param ctx Context.
    /// @return Iterator past the text.
    template <class Ctx>
    auto format(const nexigen::hw::can::CanDriverCapabilities& capabilities, Ctx& ctx) const {
        const std::string text = capabilities.to_string();
        return std::formatter<std::string_view>::format(text, ctx);
    }
};
