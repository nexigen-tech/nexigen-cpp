// nexigen::com — one hardware event handed from an ISR to the gateway.
#pragma once

#include <cstdint>
#include <format>
#include <string>
#include <string_view>

#include <nexigen/com/concepts/event_payload_concept.hpp>

namespace nexigen::com {

/// @brief Fixed-size ring element for FPGA-to-gateway handoff.
///
/// The ISR fills the metadata and payload without calling iceoryx2; the
/// gateway thread converts the descriptor into an `EventProducer` publication.
///
/// @tparam T Event payload; constrained by `concepts::EventPayload`.
/// @note Spec `com/event-system`, requirement "Hardware gateway ingress".
template <concepts::EventPayload T> struct alignas(64) HardwareEvent {
    std::uint64_t hardware_sequence{0};     ///< Raw FPGA sequence.
    std::uint64_t hardware_timestamp_ns{0}; ///< Driver-converted nanoseconds.
    std::uint32_t hardware_event_code{0};   ///< Raw FPGA event code.
    bool has_sequence{false};               ///< Enables `hardware_sequence`.
    bool has_timestamp{false};              ///< Enables `hardware_timestamp_ns`.
    bool has_event_code{false};             ///< Enables `hardware_event_code`.
    T payload{};                            ///< Event payload.

    /// @brief Render the cold-path diagnostic string.
    /// @return Human-readable hardware-event summary.
    [[nodiscard]] std::string to_string() const {
        return std::format("HardwareEvent(sequence={}, timestamp_ns={}, event_code={})",
                           hardware_sequence, hardware_timestamp_ns, hardware_event_code);
    }
};

/// @brief Free helper mirroring `HardwareEvent::to_string`.
/// @tparam T Event payload type.
/// @param event Event to render.
/// @return Human-readable summary.
template <concepts::EventPayload T>
[[nodiscard]] inline std::string to_string(const HardwareEvent<T>& event) {
    return event.to_string();
}

} // namespace nexigen::com

/// @brief Formats a hardware event as its diagnostic string.
template <nexigen::com::concepts::EventPayload T>
struct std::formatter<nexigen::com::HardwareEvent<T>> : std::formatter<std::string_view> {
    /// @brief Write the hardware-event diagnostic into the format context.
    /// @tparam Ctx Format context type.
    /// @param event Event to format.
    /// @param ctx Output context.
    /// @return Iterator past the last written character.
    template <class Ctx>
    auto format(const nexigen::com::HardwareEvent<T>& event, Ctx& ctx) const {
        return std::formatter<std::string_view>::format(event.to_string(), ctx);
    }
};
