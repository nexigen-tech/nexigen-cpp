// nexigen::app — why the infrastructure stopped (change
// rework-app-as-infrastructure; spec app/application).
//
// Full enum contract: constexpr to_string/from_string, non-throwing ADL
// to_json/from_json under JSON_NOEXCEPTION=1, and std::formatter. The values
// are stable and append-only.
#pragma once

#include <array>
#include <cstdint>
#include <format>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

#include <nexigen/core/enum_traits.hpp>
namespace nexigen::app {

/// Why the infrastructure run loop stopped.
enum class InfrastructureStopReason : std::uint8_t {
    None,      ///< No stop requested yet.
    Requested, ///< Host code requested a stop (explicit call or false tick).
    Sigint,    ///< SIGINT received.
    Sigterm,   ///< SIGTERM received.
};

} // namespace nexigen::app

namespace nexigen::app {

[[nodiscard]] constexpr std::string_view to_string(InfrastructureStopReason reason) noexcept {
    return core::enum_name(reason);
}

[[nodiscard]] constexpr bool from_string(std::string_view name,
                                         InfrastructureStopReason& out) noexcept {
    return core::enum_value(name, out);
}

inline void to_json(nlohmann::json& j, const InfrastructureStopReason reason) {
    core::mandate::enum_to_json(j, reason);
}

inline void from_json(const nlohmann::json& j, InfrastructureStopReason& out) noexcept {
    core::mandate::enum_from_json(j, out);
}

} // namespace nexigen::app

// Vocabulary table for core::enum_name/enum_value (change refactor-enum-traits-helper).
namespace nexigen::core {

template <> struct EnumTraits<nexigen::app::InfrastructureStopReason> {
    struct Entry {
        nexigen::app::InfrastructureStopReason value;
        std::string_view name;
    };
    static constexpr std::array<Entry, 4> entries{{
        {nexigen::app::InfrastructureStopReason::None, "none"},
        {nexigen::app::InfrastructureStopReason::Requested, "requested"},
        {nexigen::app::InfrastructureStopReason::Sigint, "sigint"},
        {nexigen::app::InfrastructureStopReason::Sigterm, "sigterm"},
    }};
};

} // namespace nexigen::core
