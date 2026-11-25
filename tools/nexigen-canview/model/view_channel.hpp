// nexigen-canview — one configured CAN channel (change add-can-monitor-view).
#pragma once

#include <cstdint>
#include <string>

#include <nlohmann/json.hpp>

namespace nexigen::canview {

/// @brief Configuration of one CAN channel profile.
///
/// `name` derives the `rx`/`status` service names; `channel_id` is the record
/// key and must be unique across the profile set. Bitrates are zero when the
/// configuration does not know them (the heatmap then reports `n/a`).
struct CanViewChannel {
    std::string name{};               ///< Service-name segment, e.g. `powertrain`.
    std::uint16_t channel_id{0};      ///< Numeric record key.
    std::uint32_t nominal_bitrate{0}; ///< Nominal bitrate in bit/s; 0 = unknown.
    std::uint32_t data_bitrate{0};    ///< CAN FD data-phase bitrate; 0 = unknown.
    bool fd{false};                   ///< Whether the channel carries CAN FD frames.
    bool enabled{false};              ///< Whether the channel is enabled for live capture.

    /// @brief Cold-path diagnostic rendering.
    /// @return Text with the identity, bitrates and FD flag.
    [[nodiscard]] std::string to_string() const;
};

/// @brief Write a channel profile as JSON.
/// @param j Destination.
/// @param channel Channel to serialize.
void to_json(nlohmann::json& j, const CanViewChannel& channel);

/// @brief Read a channel profile from JSON without throwing.
/// @param j Source; missing or malformed fields leave the destination unchanged.
/// @param channel Destination.
void from_json(const nlohmann::json& j, CanViewChannel& channel) noexcept;

} // namespace nexigen::canview
