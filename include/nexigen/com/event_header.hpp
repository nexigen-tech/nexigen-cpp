// nexigen::com — fixed-size event metadata header.
//
// The header is the one value every transport consumer sees. Event time is kept
// in the domain that produced it: native hardware ticks plus a domain id and
// epoch are authoritative, while `timestamp_ns` is only a canonical projection
// and is meaningful just when `has_canonical_projection()` is true. Host events
// are published in the canonical domain, so they always carry the projection.
//
// Layout (64 bytes, pinned):
//   0  u64 sequence_id
//   8  u64 timestamp_ns (canonical projection, optional)
//  16  u64 producer_epoch
//  24  u64 ingest_timestamp_ns (host CLOCK_MONOTONIC_RAW)
//  32  u64 domain_epoch
//  40  u64 native_ticks
//  48  u32 event_code
//  52  u32 hardware_event_code
//  56  u32 hardware_sequence
//  60  u16 domain_id
//  62  u16 flags
#pragma once

#include <cstddef>
#include <cstdint>
#include <format>
#include <string>
#include <string_view>
#include <type_traits>

#include <nlohmann/json.hpp>

namespace nexigen::com {

/// @brief Fixed-size metadata attached to every event.
///
/// Carries the transport sequence, the event instant in its native time domain
/// (raw ticks plus domain id/epoch), an optional canonical projection, the
/// producer lifecycle epoch, host ingest time, optional hardware sequence and
/// raw event code, and the canonical platform event code.
///
/// @note Spec `com/event-system`, requirement "Event header and timestamp
///       provenance".
struct alignas(8) EventHeader {
    static constexpr std::uint16_t has_payload_bit = 1U << 0U; ///< Payload is present.
    static constexpr std::uint16_t has_hardware_sequence_bit = 1U
                                                               << 1U; ///< FPGA sequence is valid.
    static constexpr std::uint16_t has_hardware_timestamp_bit = 1U
                                                                << 2U; ///< Native ticks are valid.
    static constexpr std::uint16_t has_hardware_event_code_bit = 1U << 3U; ///< FPGA code is valid.
    /// @brief `timestamp_ns` carries a resolved canonical projection.
    static constexpr std::uint16_t has_canonical_projection_bit = 1U << 4U;

    std::uint64_t sequence_id{0};         ///< Transport sequence.
    std::uint64_t timestamp_ns{0};        ///< Optional canonical projection.
    std::uint64_t producer_epoch{0};      ///< Gateway/source lifecycle.
    std::uint64_t ingest_timestamp_ns{0}; ///< Host monotonic raw at publish.
    std::uint64_t domain_epoch{0};        ///< Epoch of the native domain.
    std::uint64_t native_ticks{0};        ///< Event instant in its native domain.
    std::uint32_t event_code{0};          ///< Canonical platform code.
    std::uint32_t hardware_event_code{0}; ///< Optional raw FPGA code.
    std::uint32_t hardware_sequence{0};   ///< Optional FPGA sequence (trace-only).
    std::uint16_t domain_id{0};           ///< Time-base domain id (0 = canonical).
    std::uint16_t flags{0};               ///< Presence flags.

    /// @brief Whether the event carries a meaningful payload.
    /// @return `true` when `has_payload_bit` is set.
    [[nodiscard]] bool has_payload() const noexcept { return (flags & has_payload_bit) != 0U; }

    /// @brief Whether `hardware_sequence` is valid.
    /// @return `true` when the hardware-sequence flag is set.
    [[nodiscard]] bool has_hardware_sequence() const noexcept {
        return (flags & has_hardware_sequence_bit) != 0U;
    }

    /// @brief Whether `native_ticks` is valid.
    /// @return `true` when the native-timestamp flag is set.
    [[nodiscard]] bool has_native_ticks() const noexcept {
        return (flags & has_hardware_timestamp_bit) != 0U;
    }

    /// @brief Backwards-compatible alias of `has_native_ticks()`.
    /// @return `true` when `native_ticks` is valid.
    [[nodiscard]] bool has_hardware_timestamp() const noexcept { return has_native_ticks(); }

    /// @brief Whether `timestamp_ns` holds a resolved canonical projection.
    /// @return `true` when the canonical-projection flag is set.
    [[nodiscard]] bool has_canonical_projection() const noexcept {
        return (flags & has_canonical_projection_bit) != 0U;
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
static_assert(std::is_trivially_copyable_v<EventHeader>, "EventHeader must be trivially copyable");
static_assert(offsetof(EventHeader, timestamp_ns) == 8U);
static_assert(offsetof(EventHeader, domain_epoch) == 32U);
static_assert(offsetof(EventHeader, native_ticks) == 40U);
static_assert(offsetof(EventHeader, domain_id) == 60U);
static_assert(offsetof(EventHeader, flags) == 62U);

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
