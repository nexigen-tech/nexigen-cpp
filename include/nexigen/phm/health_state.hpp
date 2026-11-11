// nexigen::phm — health-channel state (change add-phm; spec phm/health-channel).
//
// The state a supervised process reports on its health channel. Own enum so
// business code never names an iox2 type; carries the full project conversion
// set (mandates 9-11).
#pragma once

#include <array>
#include <cstdint>
#include <string_view>

#include <nlohmann/json.hpp>

#include <nexigen/core/enum_traits.hpp>

namespace nexigen::phm {

/// Health state of one supervised channel, worst-first semantics.
enum class HealthState : std::uint8_t {
    Ok,       ///< Nominal operation.
    Warning,  ///< Degraded but tolerable.
    Error,    ///< A fault was detected.
    Critical, ///< Safety-relevant fault.
};

/// Canonical name of a health state; allocation-free, constexpr.
[[nodiscard]] constexpr std::string_view to_string(HealthState value) noexcept {
    return core::enum_name(value);
}

/// Parse a canonical health-state name; false leaves the out-parameter UNCHANGED.
[[nodiscard]] constexpr bool from_string(std::string_view name, HealthState& out) noexcept {
    return core::enum_value(name, out);
}

/// ADL JSON writer (cold path only): the value is exactly to_string(state).
inline void to_json(nlohmann::json& j, const HealthState value) {
    core::mandate::enum_to_json(j, value);
}

/// ADL JSON reader, strictly non-throwing under JSON_NOEXCEPTION=1.
inline void from_json(const nlohmann::json& j, HealthState& out) noexcept {
    core::mandate::enum_from_json(j, out);
}

} // namespace nexigen::phm

// Vocabulary table for core::enum_name/enum_value (change refactor-enum-traits-helper).
namespace nexigen::core {

template <> struct EnumTraits<nexigen::phm::HealthState> {
    struct Entry {
        nexigen::phm::HealthState value;
        std::string_view name;
    };
    static constexpr std::array<Entry, 4> entries{{
        {nexigen::phm::HealthState::Ok, "Ok"},
        {nexigen::phm::HealthState::Warning, "Warning"},
        {nexigen::phm::HealthState::Error, "Error"},
        {nexigen::phm::HealthState::Critical, "Critical"},
    }};
};

} // namespace nexigen::core
