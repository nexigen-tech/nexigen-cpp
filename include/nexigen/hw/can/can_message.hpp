#pragma once

#include <array>
#include <cstdint>
#include <format>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

#include <nexigen/core/result.hpp>
#include <nexigen/hw/can/can_error_flags.hpp>
#include <nexigen/hw/can/can_frame_flags.hpp>
#include <nexigen/hw/can/can_message_kind.hpp>
#include <nexigen/hw/can/can_message_origin.hpp>
#include <nexigen/hw/can/can_timestamp_source.hpp>

namespace nexigen::hw::can {

/// @brief One fixed-size CAN driver message.
///
/// The value is trivially copyable and carries the driver's hardware timestamp,
/// hardware sequence, error flags and origin unchanged. `kind` distinguishes
/// classic/FD data, remote, error, overload and bus-idle messages.
struct alignas(8) CanMessage {
    static constexpr std::size_t max_payload = 64U; ///< CAN FD payload capacity.

    std::uint64_t hardware_timestamp_ns{0};           ///< Driver hardware timestamp in ns.
    std::uint64_t hardware_sequence{0};               ///< Driver hardware sequence.
    std::uint64_t idle_duration_ns{0};                ///< Idle duration for `BusIdle`.
    std::uint32_t can_id{0};                          ///< 11-bit or 29-bit identifier.
    CanErrorFlags error_flags{};                      ///< Driver error flags.
    CanFrameFlags frame_flags{};                      ///< BRS/ESI and presence bits.
    std::uint16_t channel_id{0};                      ///< Numeric channel binding.
    CanMessageKind kind{CanMessageKind::ClassicData}; ///< Message kind.
    std::uint8_t dlc{0};                              ///< Data length (0..8 classic, 0..64 FD).
    bool extended{false};                             ///< 29-bit identifier.
    CanTimestampSource timestamp_source{CanTimestampSource::Hardware}; ///< Time provenance.
    CanMessageOrigin origin{CanMessageOrigin::Unknown}; ///< Local or external origin.
    std::array<std::uint8_t, max_payload> data{};       ///< Payload bytes.
    std::array<std::uint8_t, 7> reserved{};             ///< Reserved for future flags.

    /// @brief Validate kind-specific payload and identifier rules.
    /// @return OK, or `InvalidArgument` for an impossible message.
    [[nodiscard]] core::Status validate() const noexcept;

    /// @brief Render the message for cold-path diagnostics.
    /// @return Human-readable summary.
    [[nodiscard]] std::string to_string() const;
};

/// @brief Free helper mirroring `CanMessage::to_string`.
/// @param message Message to render.
/// @return Human-readable summary.
[[nodiscard]] inline std::string to_string(const CanMessage& message) {
    return message.to_string();
}

/// @brief Serialize a message for cold-path diagnostics.
/// @param j Destination.
/// @param message Message to serialize.
void to_json(nlohmann::json& j, const CanMessage& message);

/// @brief Parse a message from JSON without throwing.
/// @param j Source.
/// @param message Destination, unchanged field-by-field on malformed input.
void from_json(const nlohmann::json& j, CanMessage& message) noexcept;

} // namespace nexigen::hw::can

/// @brief `std::formatter` for `CanMessage`.
template <> struct std::formatter<nexigen::hw::can::CanMessage> : std::formatter<std::string_view> {
    /// @brief Format the message through `to_string`.
    /// @tparam Ctx Format context.
    /// @param message Message to render.
    /// @param ctx Context.
    /// @return Iterator past the text.
    template <class Ctx> auto format(const nexigen::hw::can::CanMessage& message, Ctx& ctx) const {
        const std::string text = message.to_string();
        return std::formatter<std::string_view>::format(text, ctx);
    }
};
