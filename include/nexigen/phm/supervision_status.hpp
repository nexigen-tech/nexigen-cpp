// nexigen::phm — supervision status machine (change add-phm; spec
// phm/supervision).
//
// The per-process status the supervision engine derives from Alive, Logical,
// and Deadline results. Own enum with the full project conversion set.
#pragma once

#include <array>
#include <cstdint>
#include <string_view>

#include <nlohmann/json.hpp>

#include <nexigen/core/enum_traits.hpp>

namespace nexigen::phm {

/// Aggregated supervision status of one process.
enum class SupervisionStatus : std::uint8_t {
    Ok,       ///< All checks pass.
    Failed,   ///< A single failure within the tolerance.
    Expired,  ///< Failures exceeded the tolerance; recovery is triggered.
    Stopped,  ///< The process stopped deliberately.
};

/// Canonical name of a supervision status; allocation-free, constexpr.
[[nodiscard]] constexpr std::string_view to_string(SupervisionStatus value) noexcept {
    return core::enum_name(value);
}

/// Parse a canonical supervision-status name; false leaves the out-parameter UNCHANGED.
[[nodiscard]] constexpr bool from_string(std::string_view name, SupervisionStatus& out) noexcept {
    return core::enum_value(name, out);
}

/// ADL JSON writer (cold path only): the value is exactly to_string(status).
inline void to_json(nlohmann::json& j, const SupervisionStatus value) {
    core::mandate::enum_to_json(j, value);
}

/// ADL JSON reader, strictly non-throwing under JSON_NOEXCEPTION=1.
inline void from_json(const nlohmann::json& j, SupervisionStatus& out) noexcept {
    core::mandate::enum_from_json(j, out);
}

} // namespace nexigen::phm

// Vocabulary table for core::enum_name/enum_value (change refactor-enum-traits-helper).
namespace nexigen::core {

template <> struct EnumTraits<nexigen::phm::SupervisionStatus> {
    struct Entry {
        nexigen::phm::SupervisionStatus value;
        std::string_view name;
    };
    static constexpr std::array<Entry, 4> entries{{
        {nexigen::phm::SupervisionStatus::Ok, "Ok"},
        {nexigen::phm::SupervisionStatus::Failed, "Failed"},
        {nexigen::phm::SupervisionStatus::Expired, "Expired"},
        {nexigen::phm::SupervisionStatus::Stopped, "Stopped"},
    }};
};

} // namespace nexigen::core
