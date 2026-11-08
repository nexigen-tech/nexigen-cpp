// nexigen::logging — scheduled directory rotation schedule selector (design
// D11, spec logging/file-logger "Hitch-free scheduled directory rotation").
//
// Public vocabulary enum consumed by RotationConfig and FileLoggerConfig; the
// rotation machine (detail/rotation.hpp) consumes the same enumerators.
#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

#include <nexigen/core/enum_traits.hpp>
namespace nexigen::logging {

/// Schedule selector (None, Hourly, Daily, or Custom epoch-anchored windows).
enum class RotationSchedule : std::uint8_t { None, Hourly, Daily, Custom };

} // namespace nexigen::logging

namespace nexigen::logging {

/// Compile-time lowercase name — the spelling used by from_string/JSON.
[[nodiscard]] constexpr std::string_view to_string(RotationSchedule kind) noexcept {
    return core::enum_name(kind);
}

/// Parse a canonical lowercase schedule name; false leaves `out` unchanged.
[[nodiscard]] constexpr bool from_string(std::string_view name, RotationSchedule& out) noexcept {
    return core::enum_value(name, out);
}

/// ADL JSON writer (cold path only): the JSON value is exactly to_string(kind).
inline void to_json(nlohmann::json& j, const RotationSchedule kind) {
    core::mandate::enum_to_json(j, kind);
}

/// ADL JSON reader, strictly non-throwing under JSON_NOEXCEPTION=1.
inline void from_json(const nlohmann::json& j, RotationSchedule& out) noexcept {
    core::mandate::enum_from_json(j, out);
}

} // namespace nexigen::logging

// Vocabulary table for core::enum_name/enum_value (change refactor-enum-traits-helper).
namespace nexigen::core {

template <> struct EnumTraits<nexigen::logging::RotationSchedule> {
    struct Entry {
        nexigen::logging::RotationSchedule value;
        std::string_view name;
    };
    static constexpr std::array<Entry, 4> entries{{
        {nexigen::logging::RotationSchedule::None, "none"},
        {nexigen::logging::RotationSchedule::Hourly, "hourly"},
        {nexigen::logging::RotationSchedule::Daily, "daily"},
        {nexigen::logging::RotationSchedule::Custom, "custom"},
    }};
};

} // namespace nexigen::core
