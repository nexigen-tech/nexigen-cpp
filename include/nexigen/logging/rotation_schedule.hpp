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

/// @brief Schedule selector: no rotation, hourly, daily, or custom epoch-anchored windows.
///
/// Cold-path vocabulary consumed by `RotationConfig` and `FileLoggerConfig`; the
/// rotation machine in `detail/rotation.hpp` maps each enumerator to the next
/// boundary computation performed exclusively on the writer thread.
///
/// @note Spec `logging/file-logger`, requirement "Rotation schedule vocabulary".
enum class RotationSchedule : std::uint8_t {
    /// @brief Never rotate; every record goes to one directory and one file series.
    None,
    /// @brief Rotate on each calendar-hour boundary (UTC).
    Hourly,
    /// @brief Rotate on each calendar-day boundary (UTC).
    Daily,
    /// @brief Rotate on epoch-anchored windows of `RotationConfig::custom_ns`.
    Custom
};

} // namespace nexigen::logging

// Vocabulary table for core::enum_name/enum_value (change refactor-enum-traits-helper).
namespace nexigen::core {

/// @brief Enum-traits table mapping `RotationSchedule` enumerators to canonical names.
///
/// Internal vocabulary plumbing for `core::enum_name`/`core::enum_value`; the
/// names are the spellings used by `to_string`, `from_string` and JSON.
/// @note Spec `logging/file-logger`, requirement "Rotation schedule vocabulary".
template <> struct EnumTraits<nexigen::logging::RotationSchedule> {
    /// @brief One enumerator-to-name pair in the table.
    struct Entry {
        /// @brief Enumerator being named.
        nexigen::logging::RotationSchedule value;
        /// @brief Canonical lowercase spelling of `value`.
        std::string_view name;
    };
    /// @brief Table of every `RotationSchedule` enumerator, in declaration order.
    static constexpr std::array<Entry, 4> entries{{
        {nexigen::logging::RotationSchedule::None, "none"},
        {nexigen::logging::RotationSchedule::Hourly, "hourly"},
        {nexigen::logging::RotationSchedule::Daily, "daily"},
        {nexigen::logging::RotationSchedule::Custom, "custom"},
    }};
};

} // namespace nexigen::core

namespace nexigen::logging {

/// @brief Compile-time lowercase name — the spelling used by `from_string` and JSON.
///
/// @param kind Schedule to name.
/// @return `"none"`, `"hourly"`, `"daily"` or `"custom"`; never throws.
/// @note Project mandate 9 (enum `to_string` vocabulary).
[[nodiscard]] constexpr std::string_view to_string(RotationSchedule kind) noexcept {
    return core::enum_name(kind);
}

/// @brief Parse a canonical lowercase schedule name.
///
/// @param name Canonical lowercase spelling as produced by `to_string`.
/// @param out Destination; left unchanged when `name` is not recognized.
/// @return `true` when `name` matched an enumerator, `false` otherwise.
/// @note Project mandate 9 (enum `from_string` round-trip).
[[nodiscard]] constexpr bool from_string(std::string_view name, RotationSchedule& out) noexcept {
    return core::enum_value(name, out);
}

/// @brief ADL JSON writer (cold path only): the JSON value is exactly `to_string(kind)`.
///
/// @param j Destination JSON value; overwritten.
/// @param kind Schedule to serialize.
/// @note Project mandate 10 (JSON vocabulary for configuration enums).
inline void to_json(nlohmann::json& j, const RotationSchedule kind) {
    core::mandate::enum_to_json(j, kind);
}

/// @brief ADL JSON reader, strictly non-throwing under `JSON_NOEXCEPTION=1`.
///
/// An unknown or ill-typed value leaves `out` unchanged.
///
/// @param j Source JSON value.
/// @param out Destination schedule.
/// @note Project mandate 10.
inline void from_json(const nlohmann::json& j, RotationSchedule& out) noexcept {
    core::mandate::enum_from_json(j, out);
}

} // namespace nexigen::logging
