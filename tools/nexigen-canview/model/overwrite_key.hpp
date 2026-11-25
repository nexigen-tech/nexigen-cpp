// nexigen-canview — identity of one Overwrite row (change add-can-monitor-view).
#pragma once

#include <cstdint>
#include <functional>
#include <string>

#include <nlohmann/json.hpp>

#include <nexigen/hw/can/can_message_kind.hpp>

namespace nexigen::canview {

/// @brief Key of one Overwrite row: `(channel_id, can_id, extended, kind)`.
///
/// Selection is key-based, so the key is a value type with total ordering and
/// a hash usable by the open-addressing table.
struct OverwriteKey {
    std::uint16_t channel_id{0}; ///< Channel identity.
    std::uint32_t can_id{0};     ///< CAN identifier.
    bool extended{false};        ///< 29-bit identifier.
    nexigen::hw::can::CanMessageKind kind{
        nexigen::hw::can::CanMessageKind::ClassicData}; ///< Message kind.

    /// @brief Total ordering for stable table and selection output.
    /// @return Lexicographic comparison.
    [[nodiscard]] auto operator<=>(const OverwriteKey&) const noexcept = default;

    /// @brief Cold-path diagnostic rendering.
    /// @return Text with the four key parts.
    [[nodiscard]] std::string to_string() const;
};

/// @brief Write an overwrite key as JSON.
/// @param j Destination.
/// @param key Key to serialize.
void to_json(nlohmann::json& j, const OverwriteKey& key);

/// @brief Read an overwrite key from JSON without throwing.
/// @param j Source.
/// @param key Destination, unchanged field-by-field on malformed input.
void from_json(const nlohmann::json& j, OverwriteKey& key) noexcept;

} // namespace nexigen::canview

/// @brief `std::hash` support for the open-addressing Overwrite table.
template <> struct std::hash<nexigen::canview::OverwriteKey> {
    /// @brief Combine the four key fields.
    /// @param key Key to hash.
    /// @return 64-bit hash.
    [[nodiscard]] std::size_t operator()(const nexigen::canview::OverwriteKey& key) const noexcept {
        std::size_t seed = static_cast<std::size_t>(key.channel_id);
        seed ^= static_cast<std::size_t>(key.can_id) + 0x9e3779b97f4a7c15ULL + (seed << 6U) +
                (seed >> 2U);
        seed ^= static_cast<std::size_t>(key.extended ? 1U : 0U) + 0x9e3779b97f4a7c15ULL +
                (seed << 6U) + (seed >> 2U);
        seed ^= static_cast<std::size_t>(key.kind) + 0x9e3779b97f4a7c15ULL + (seed << 6U) +
                (seed >> 2U);
        return seed;
    }
};
