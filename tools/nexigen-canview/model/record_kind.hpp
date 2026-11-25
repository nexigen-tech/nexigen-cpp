// nexigen-canview — record identity inside the history ring.
#pragma once

#include <array>
#include <cstdint>
#include <string_view>

#include <nlohmann/json.hpp>

#include <nexigen/core/enum_traits.hpp>
#include <nexigen/core/serialization_mandate.hpp>

namespace nexigen::canview {

/// @brief What one history slot represents.
///
/// Received frames, bus-idle heartbeats and synthetic loss records share the
/// same ring, but only `Frame` counts as a received frame.
enum class CanRecordKind : std::uint8_t {
    Frame,            ///< A CAN frame received from the transport.
    BusIdleHeartbeat, ///< Driver bus-idle heartbeat.
    TransportGap,     ///< Transport or hardware-sequence loss.
    IngressOverflow,  ///< Records the capture hand-off queue could not retain.
    DriverDrop,       ///< Records the driver reported as dropped.
};

} // namespace nexigen::canview

namespace nexigen::core {

/// @brief Vocabulary table for `CanRecordKind`.
template <> struct EnumTraits<nexigen::canview::CanRecordKind> {
    /// @brief One value/name row.
    /// @brief One value/name row.

    struct Entry {
        nexigen::canview::CanRecordKind value; ///< Enumerator. ///< Enumerator.
        std::string_view name;                 ///< Canonical name.
    };
    /// @brief Canonical table.
    /// @brief Canonical table.
    static constexpr std::array<Entry, 5> entries{{
        {nexigen::canview::CanRecordKind::Frame, "Frame"},
        {nexigen::canview::CanRecordKind::BusIdleHeartbeat, "BusIdleHeartbeat"},
        {nexigen::canview::CanRecordKind::TransportGap, "TransportGap"},
        {nexigen::canview::CanRecordKind::IngressOverflow, "IngressOverflow"},
        {nexigen::canview::CanRecordKind::DriverDrop, "DriverDrop"},
    }};
};

} // namespace nexigen::core

namespace nexigen::canview {

/// @brief Canonical name of a record kind.
/// @param kind Kind to render.
/// @return Canonical name or `Unknown`.
[[nodiscard]] constexpr std::string_view to_string(CanRecordKind kind) noexcept {
    return core::enum_name(kind);
}

/// @brief Parse a canonical record-kind name.
/// @param name Candidate name.
/// @param out Destination, unchanged on a miss.
/// @return `true` when a name matched.
[[nodiscard]] constexpr bool from_string(std::string_view name, CanRecordKind& out) noexcept {
    return core::enum_value(name, out);
}

/// @brief Write a record kind as JSON.
/// @param j Destination.
/// @param kind Kind to serialize.
inline void to_json(nlohmann::json& j, const CanRecordKind kind) {
    core::mandate::enum_to_json(j, kind);
}

/// @brief Read a record kind from JSON without throwing.
/// @param j Source.
/// @param kind Destination, unchanged on failure.
inline void from_json(const nlohmann::json& j, CanRecordKind& kind) noexcept {
    core::mandate::enum_from_json(j, kind);
}

/// @brief Whether a kind is synthetic rather than a received frame.
/// @param kind Kind to classify.
/// @return `true` for every non-frame kind.
[[nodiscard]] constexpr bool is_synthetic(CanRecordKind kind) noexcept {
    return kind != CanRecordKind::Frame;
}

/// @brief Whether a kind is a synthetic loss record.
/// @param kind Kind to classify.
/// @return `true` for `TransportGap`, `IngressOverflow` and `DriverDrop`.
[[nodiscard]] constexpr bool is_loss(CanRecordKind kind) noexcept {
    return kind == CanRecordKind::TransportGap || kind == CanRecordKind::IngressOverflow ||
           kind == CanRecordKind::DriverDrop;
}

} // namespace nexigen::canview
