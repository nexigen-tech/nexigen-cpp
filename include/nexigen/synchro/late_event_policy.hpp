// nexigen::synchro — late-event policy vocabulary
// (change add-synchro-event-sync; spec synchro/protocol).
#pragma once

#include <array>
#include <cstdint>
#include <string_view>

#include <nlohmann/json.hpp>

#include <nexigen/core/enum_traits.hpp>

namespace nexigen::synchro {

/// @brief What a consumer does with an event that missed its latency budget.
///
/// Both policies still update continuity tracking and telemetry; only delivery
/// to application handlers differs.
///
/// @note Spec `synchro/protocol`, requirement "Late-event policy".
enum class LateEventPolicy : std::uint8_t {
    DropAndWarn,     ///< Suppress delivery, count the drop, log a warning.
    DeliverWithFlag, ///< Deliver with `SyncContext::is_late` set.
};

/// @brief Canonical name of a late-event policy.
/// @param value Policy to render.
/// @return Static-storage canonical spelling. Never allocates.
/// @note Spec `synchro/protocol`, requirement "Late-event policy"; mandate 9.
[[nodiscard]] constexpr std::string_view to_string(LateEventPolicy value) noexcept {
    return core::enum_name(value);
}

/// @brief Parse a canonical late-event policy name.
/// @param name Candidate spelling.
/// @param out Destination, untouched on a miss.
/// @return `true` on an exact match.
/// @note Spec `synchro/protocol`, requirement "Late-event policy".
[[nodiscard]] constexpr bool from_string(std::string_view name, LateEventPolicy& out) noexcept {
    return core::enum_value(name, out);
}

/// @brief ADL JSON writer for a late-event policy.
/// @param j Destination JSON value.
/// @param value Policy to serialize.
/// @note Mandate 10; cold path only.
inline void to_json(nlohmann::json& j, const LateEventPolicy value) {
    core::mandate::enum_to_json(j, value);
}

/// @brief ADL JSON reader for a late-event policy, strictly non-throwing.
/// @param j Source JSON value.
/// @param value Destination, unchanged on failure.
/// @note Mandate 10; cold path only.
inline void from_json(const nlohmann::json& j, LateEventPolicy& value) noexcept {
    core::mandate::enum_from_json(j, value);
}

} // namespace nexigen::synchro

namespace nexigen::core {

/// @brief Vocabulary table binding `LateEventPolicy` to canonical names.
template <> struct EnumTraits<nexigen::synchro::LateEventPolicy> {
    /// @brief One value/name pair.
    struct Entry {
        nexigen::synchro::LateEventPolicy value; ///< Enumerator.
        std::string_view name;                   ///< Canonical name.
    };
    /// @brief Canonical table.
    static constexpr std::array<Entry, 2> entries{{
        {nexigen::synchro::LateEventPolicy::DropAndWarn, "DropAndWarn"},
        {nexigen::synchro::LateEventPolicy::DeliverWithFlag, "DeliverWithFlag"},
    }};
};

} // namespace nexigen::core
