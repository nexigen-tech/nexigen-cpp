// nexigen::com — event loss classification.
#pragma once

#include <array>
#include <cstdint>
#include <format>
#include <string_view>

#include <nlohmann/json.hpp>

#include <nexigen/core/enum_traits.hpp>
#include <nexigen/core/serialization_mandate.hpp>

namespace nexigen::com {

/// @brief Why an event consumer reported a discontinuity.
///
/// @note Spec `com/event-system`, requirement "Transport loss detection and
///       callbacks".
enum class LossReason : std::uint8_t {
    TransportGap,            ///< The transport sequence skipped values.
    ProducerRestart,         ///< The producer epoch changed.
    Duplicate,               ///< A sequence below the expected value arrived.
    UnexpectedSequenceReset, ///< Sequence zero arrived without an epoch change.
};

} // namespace nexigen::com

namespace nexigen::core {

/// @brief Vocabulary table binding `LossReason` to canonical names.
template <> struct EnumTraits<nexigen::com::LossReason> {
    /// @brief One value/name pair.
    struct Entry {
        nexigen::com::LossReason value; ///< Enumerator.
        std::string_view name;          ///< Canonical name.
    };
    /// @brief Canonical table.
    static constexpr std::array<Entry, 4> entries{{
        {nexigen::com::LossReason::TransportGap, "TransportGap"},
        {nexigen::com::LossReason::ProducerRestart, "ProducerRestart"},
        {nexigen::com::LossReason::Duplicate, "Duplicate"},
        {nexigen::com::LossReason::UnexpectedSequenceReset, "UnexpectedSequenceReset"},
    }};
};

} // namespace nexigen::core

namespace nexigen::com {

/// @brief Canonical name of a loss reason.
/// @param value Reason to render.
/// @return Canonical name.
[[nodiscard]] constexpr std::string_view to_string(LossReason value) noexcept {
    return core::enum_name(value);
}

/// @brief Parse a canonical loss-reason name.
/// @param name Candidate name.
/// @param out Destination, unchanged on failure.
/// @return `true` when the name matched.
[[nodiscard]] constexpr bool from_string(std::string_view name, LossReason& out) noexcept {
    return core::enum_value(name, out);
}

/// @brief ADL JSON writer for a loss reason.
/// @param j Destination JSON value.
/// @param value Reason to serialize.
inline void to_json(nlohmann::json& j, const LossReason value) {
    core::mandate::enum_to_json(j, value);
}

/// @brief ADL JSON reader for a loss reason, strictly non-throwing.
/// @param j Source JSON value.
/// @param value Destination, unchanged on failure.
inline void from_json(const nlohmann::json& j, LossReason& value) noexcept {
    core::mandate::enum_from_json(j, value);
}

} // namespace nexigen::com

/// @brief Formats a loss reason as its canonical name.
template <> struct std::formatter<nexigen::com::LossReason> : std::formatter<std::string_view> {
    /// @brief Write the reason name into the format context.
    /// @tparam Ctx Format context type.
    /// @param value Reason to format.
    /// @param ctx Output context.
    /// @return Iterator past the last written character.
    template <class Ctx> auto format(const nexigen::com::LossReason value, Ctx& ctx) const {
        return std::formatter<std::string_view>::format(nexigen::com::to_string(value), ctx);
    }
};
