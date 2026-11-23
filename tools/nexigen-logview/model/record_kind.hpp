// nexigen-logview — record identity inside the history ring.
#pragma once

#include <array>
#include <cstdint>
#include <format>
#include <string_view>

#include <nlohmann/json.hpp>

#include <nexigen/core/enum_traits.hpp>

namespace nexigen::logview {

/// @brief What one history slot represents.
///
/// Received frames and synthetic loss records share the same ring, but only
/// `Frame` is counted as a received frame.
enum class RecordKind : std::uint8_t {
    Frame,           ///< A `LogFrame` received from the transport.
    TransportGap,    ///< Frames the transport reported missing before this one.
    IngressOverflow, ///< Records the capture hand-off queue could not retain.
    PublisherDrop,   ///< Records the publisher reported in `dropped_counter`.
};

} // namespace nexigen::logview

namespace nexigen::core {

/// @brief Vocabulary table for `RecordKind` (mandate 9).
template <> struct EnumTraits<nexigen::logview::RecordKind> {
    /// @brief One table row.
    struct Entry {
        nexigen::logview::RecordKind value; ///< Enumerator.
        std::string_view name;              ///< Canonical lowercase wire name.
    };
    /// @brief Table covering every enumerator.
    static constexpr std::array<Entry, 4> entries{{
        {nexigen::logview::RecordKind::Frame, "frame"},
        {nexigen::logview::RecordKind::TransportGap, "transport_gap"},
        {nexigen::logview::RecordKind::IngressOverflow, "ingress_overflow"},
        {nexigen::logview::RecordKind::PublisherDrop, "publisher_drop"},
    }};
};
} // namespace nexigen::core

namespace nexigen::logview {

/// @brief Canonical lowercase name of a record kind.
/// @param kind Kind to render.
/// @return Static-storage name.
[[nodiscard]] constexpr std::string_view to_string(RecordKind kind) noexcept {
    return nexigen::core::enum_name(kind);
}

/// @brief Parse a canonical record-kind name.
/// @param name Candidate spelling.
/// @param out Destination, untouched on a miss.
/// @return `true` on an exact match.
[[nodiscard]] constexpr bool from_string(std::string_view name, RecordKind& out) noexcept {
    return nexigen::core::enum_value(name, out);
}

/// @brief ADL JSON writer (mandate 10).
/// @param j Destination JSON value.
/// @param kind Kind to serialize.
inline void to_json(nlohmann::json& j, RecordKind kind) {
    j = to_string(kind);
}

/// @brief Non-throwing ADL JSON reader (mandate 10).
/// @param j Source JSON value.
/// @param out Destination, untouched on a non-string or unknown name.
inline void from_json(const nlohmann::json& j, RecordKind& out) noexcept {
    if (!j.is_string()) {
        return;
    }
    static_cast<void>(from_string(j.get_ref<const nlohmann::json::string_t&>(), out));
}

/// @brief Whether a kind is synthetic rather than a received frame.
/// @param kind Kind to classify.
/// @return `true` for every non-frame kind.
[[nodiscard]] constexpr bool is_synthetic(RecordKind kind) noexcept {
    return kind != RecordKind::Frame;
}

} // namespace nexigen::logview

/// @brief `std::formatter` for `RecordKind` (mandate 11).
template <> struct std::formatter<nexigen::logview::RecordKind> : std::formatter<std::string_view> {
    /// @brief Format the canonical name.
    template <class Ctx> auto format(nexigen::logview::RecordKind kind, Ctx& ctx) const {
        return std::formatter<std::string_view>::format(nexigen::logview::to_string(kind), ctx);
    }
};
