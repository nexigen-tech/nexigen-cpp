// nexigen::synchro — past-target policy vocabulary
// (change add-synchro-event-sync; spec synchro/protocol).
#pragma once

#include <array>
#include <cstdint>
#include <string_view>

#include <nlohmann/json.hpp>

#include <nexigen/core/enum_traits.hpp>

namespace nexigen::synchro {

/// @brief What an `AtTarget` start does when the target is not in the future.
///
/// @note Spec `synchro/protocol`, requirement "Start policy".
enum class PastTargetPolicy : std::uint8_t {
    Reject,             ///< Fail the start with `InvalidConfig`.
    RealignToNextCycle, ///< Warn and start on the next cycle boundary after now.
};

/// @brief Canonical name of a past-target policy.
/// @param value Policy to render.
/// @return Static-storage canonical spelling. Never allocates.
/// @note Spec `synchro/protocol`, requirement "Start policy"; mandate 9.
[[nodiscard]] constexpr std::string_view to_string(PastTargetPolicy value) noexcept {
    return core::enum_name(value);
}

/// @brief Parse a canonical past-target policy name.
/// @param name Candidate spelling.
/// @param out Destination, untouched on a miss.
/// @return `true` on an exact match.
/// @note Spec `synchro/protocol`, requirement "Start policy".
[[nodiscard]] constexpr bool from_string(std::string_view name, PastTargetPolicy& out) noexcept {
    return core::enum_value(name, out);
}

/// @brief ADL JSON writer for a past-target policy.
/// @param j Destination JSON value.
/// @param value Policy to serialize.
/// @note Mandate 10; cold path only.
inline void to_json(nlohmann::json& j, const PastTargetPolicy value) {
    core::mandate::enum_to_json(j, value);
}

/// @brief ADL JSON reader for a past-target policy, strictly non-throwing.
/// @param j Source JSON value.
/// @param value Destination, unchanged on failure.
/// @note Mandate 10; cold path only.
inline void from_json(const nlohmann::json& j, PastTargetPolicy& value) noexcept {
    core::mandate::enum_from_json(j, value);
}

} // namespace nexigen::synchro

namespace nexigen::core {

/// @brief Vocabulary table binding `PastTargetPolicy` to canonical names.
template <> struct EnumTraits<nexigen::synchro::PastTargetPolicy> {
    /// @brief One value/name pair.
    struct Entry {
        nexigen::synchro::PastTargetPolicy value; ///< Enumerator.
        std::string_view name;                    ///< Canonical name.
    };
    /// @brief Canonical table.
    static constexpr std::array<Entry, 2> entries{{
        {nexigen::synchro::PastTargetPolicy::Reject, "Reject"},
        {nexigen::synchro::PastTargetPolicy::RealignToNextCycle, "RealignToNextCycle"},
    }};
};

} // namespace nexigen::core
