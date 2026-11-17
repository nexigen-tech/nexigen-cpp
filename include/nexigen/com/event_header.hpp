// nexigen::com — fixed-size event metadata header.
#pragma once

#include <cstdint>
#include <format>
#include <string>
#include <string_view>
#include <type_traits>

#include <nlohmann/json.hpp>

#include <nexigen/com/timestamp_domain.hpp>

namespace nexigen::com {

/// @brief Fixed-size metadata attached to every event.
///
/// The header is the custom user header of the event publish-subscribe
/// channel. It carries the transport sequence, canonical event time and
/// domain, producer lifecycle epoch, host ingest time, optional FPGA metadata,
/// the platform event code, and the raw hardware event code.
///
/// @note Spec `com/event-system`, requirement "Event header and timestamp
///       provenance".
struct alignas(8) EventHeader {
    static constexpr std::uint16_t has_payload_bit = 1U << 0U; ///< Payload is present.
    static constexpr std::uint16_t has_hardware_sequence_bit = 1U << 1U; ///< FPGA sequence is valid.
    static constexpr std::uint16_t has_hardware_timestamp_bit = 1U << 2U; ///< FPGA timestamp is valid.
    static constexpr std::uint16_t has_hardware_event_code_bit = 1U << 3U; ///< FPGA code is valid.

    std::uint64_t sequence_id{0};              ///< Transport sequence.
    std::uint64_t timestamp_ns{0};             ///< Canonical event time.
    std::uint64_t producer_epoch{0};           ///< Gateway/source lifecycle.
    std::uint64_t ingest_timestamp_ns{0};      ///< Host monotonic raw at publish.
    std::uint64_t hardware_sequence{0};        ///< Optional FPGA sequence.
    std::uint64_t hardware_timestamp_ns{0};    ///< Optional FPGA timestamp, ns.
    std::uint32_t event_code{0};               ///< Canonical platform code.
    std::uint32_t hardware_event_code{0};      ///< Optional raw FPGA code.
    TimestampDomain timestamp_domain{TimestampDomain::Unknown}; ///< Domain of `timestamp_ns`.
    std::uint16_t flags{0};                    ///< Presence flags.
    std::uint32_t reserved{0};                 ///< Reserved; zero in v1.

    /// @brief Whether the event carries a meaningful payload.
    /// @return `true` when `has_payload_bit` is set.
    [[nodiscard]] bool has_payload() const noexcept { return (flags & has_payload_bit) != 0U; }

    /// @brief Whether `hardware_sequence` is valid.
    /// @return `true` when the hardware-sequence flag is set.
    [[nodiscard]] bool has_hardware_sequence() const noexcept {
        return (flags & has_hardware_sequence_bit) != 0U;
    }

    /// @brief Whether `hardware_timestamp_ns` is valid.
    /// @return `true` when the hardware-timestamp flag is set.
    [[nodiscard]] bool has_hardware_timestamp() const noexcept {
        return (flags & has_hardware_timestamp_bit) != 0U;
    }

    /// @brief Whether `hardware_event_code` is valid.
    /// @return `true` when the hardware-event-code flag is set.
    [[nodiscard]] bool has_hardware_event_code() const noexcept {
        return (flags & has_hardware_event_code_bit) != 0U;
    }

    /// @brief Render the cold-path diagnostic string.
    /// @return Human-readable header summary.
    [[nodiscard]] std::string to_string() const;
};

static_assert(sizeof(EventHeader) == 64U, "EventHeader must stay 64 bytes");
static_assert(alignof(EventHeader) == 8U, "EventHeader must stay 8-byte aligned");
static_assert(std::is_trivially_copyable_v<EventHeader>,
              "EventHeader must be trivially copyable");

/// @brief Free helper mirroring `EventHeader::to_string`.
/// @param header Header to render.
/// @return Human-readable summary.
[[nodiscard]] inline std::string to_string(const EventHeader& header) {
    return header.to_string();
}

/// @brief Serialize an event header to diagnostic JSON.
/// @param j Destination JSON object.
/// @param header Header to serialize.
void to_json(nlohmann::json& j, const EventHeader& header);

/// @brief Parse an event header from diagnostic JSON, strictly non-throwing.
/// @param j Source JSON object; malformed values leave `header` unchanged.
/// @param header Destination header.
void from_json(const nlohmann::json& j, EventHeader& header) noexcept;

} // namespace nexigen::com

/// @brief Formats an event header as its diagnostic string.
template <> struct std::formatter<nexigen::com::EventHeader> : std::formatter<std::string_view> {
    /// @brief Write the header diagnostic into the format context.
    /// @tparam Ctx Format context type.
    /// @param header Header to format.
    /// @param ctx Output context.
    /// @return Iterator past the last written character.
    template <class Ctx> auto format(const nexigen::com::EventHeader& header, Ctx& ctx) const {
        return std::formatter<std::string_view>::format(header.to_string(), ctx);
    }
};
