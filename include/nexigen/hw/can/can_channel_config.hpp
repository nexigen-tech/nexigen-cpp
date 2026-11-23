#pragma once

#include <cstddef>
#include <cstdint>
#include <format>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

#include <nexigen/core/result.hpp>
#include <nexigen/hw/can/can_channel_name.hpp>

namespace nexigen::hw::can {

/// @brief Configuration of one CAN channel.
struct CanChannelConfig {
    std::string name{};                                    ///< Unique logical channel name.
    std::uint16_t channel_id{0};                           ///< Numeric binding in `CanMessage`.
    std::string driver_channel{};                          ///< Opaque driver channel key.
    std::uint32_t bit_rate{0};                             ///< Nominal bit rate.
    bool fd_enabled{false};                                ///< Whether CAN FD frames are accepted.
    std::size_t rx_ring{4096};                             ///< Receive ring capacity.
    std::size_t tx_queue{1024};                            ///< Transmit queue capacity.
    bool drop_oldest{false};                               ///< Receive ring overflow policy.
    std::uint64_t heartbeat_interval_ns{1'000'000'000ULL}; ///< Bus-idle heartbeat interval.
    std::uint64_t status_interval_ns{1'000'000'000ULL};    ///< Periodic status interval.

    /// @brief Validate names, ids and bounded capacities.
    /// @return OK, or `InvalidArgument` with the first invalid field.
    [[nodiscard]] core::Status validate() const noexcept;

    /// @brief Render the configuration for cold-path diagnostics.
    /// @return Human-readable summary.
    [[nodiscard]] std::string to_string() const;
};

/// @brief Free helper mirroring `CanChannelConfig::to_string`.
/// @param config Configuration to render.
/// @return Human-readable summary.
[[nodiscard]] inline std::string to_string(const CanChannelConfig& config) {
    return config.to_string();
}

/// @brief Serialize a channel configuration.
/// @param j Destination.
/// @param config Configuration to serialize.
void to_json(nlohmann::json& j, const CanChannelConfig& config);

/// @brief Parse a channel configuration without throwing.
/// @param j Source.
/// @param config Destination.
void from_json(const nlohmann::json& j, CanChannelConfig& config) noexcept;

} // namespace nexigen::hw::can

/// @brief `std::formatter` for `CanChannelConfig`.
template <>
struct std::formatter<nexigen::hw::can::CanChannelConfig> : std::formatter<std::string_view> {
    /// @brief Format the configuration through `to_string`.
    /// @tparam Ctx Format context.
    /// @param config Configuration to render.
    /// @param ctx Context.
    /// @return Iterator past the text.
    template <class Ctx>
    auto format(const nexigen::hw::can::CanChannelConfig& config, Ctx& ctx) const {
        const std::string text = config.to_string();
        return std::formatter<std::string_view>::format(text, ctx);
    }
};
