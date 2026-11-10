// nexigen::phm — fault kind vocabulary (change add-phm; spec phm/recovery
// "Fault storage").
//
// The supervision domain a recorded violation belongs to. Own enum with the
// full project conversion set.
#pragma once

#include <array>
#include <cstdint>
#include <string_view>

#include <nlohmann/json.hpp>

#include <nexigen/core/enum_traits.hpp>

namespace nexigen::phm {

/// The supervision domain that detected a fault.
enum class FaultKind : std::uint8_t {
    AliveFailure,    ///< Missed aliveness window.
    LogicalFailure,  ///< Checkpoint order violated the transition graph.
    DeadlineFailure, ///< Source->target elapsed outside min/max.
};

/// Canonical name of a fault kind; allocation-free, constexpr.
[[nodiscard]] constexpr std::string_view to_string(FaultKind value) noexcept {
    return core::enum_name(value);
}

/// Parse a canonical fault-kind name; false leaves the out-parameter UNCHANGED.
[[nodiscard]] constexpr bool from_string(std::string_view name, FaultKind& out) noexcept {
    return core::enum_value(name, out);
}

/// ADL JSON writer (cold path only): the value is exactly to_string(kind).
inline void to_json(nlohmann::json& j, const FaultKind value) {
    core::mandate::enum_to_json(j, value);
}

/// ADL JSON reader, strictly non-throwing under JSON_NOEXCEPTION=1.
inline void from_json(const nlohmann::json& j, FaultKind& out) noexcept {
    core::mandate::enum_from_json(j, out);
}

} // namespace nexigen::phm

// Vocabulary table for core::enum_name/enum_value (change refactor-enum-traits-helper).
namespace nexigen::core {

template <> struct EnumTraits<nexigen::phm::FaultKind> {
    struct Entry {
        nexigen::phm::FaultKind value;
        std::string_view name;
    };
    static constexpr std::array<Entry, 3> entries{{
        {nexigen::phm::FaultKind::AliveFailure, "AliveFailure"},
        {nexigen::phm::FaultKind::LogicalFailure, "LogicalFailure"},
        {nexigen::phm::FaultKind::DeadlineFailure, "DeadlineFailure"},
    }};
};

} // namespace nexigen::core
