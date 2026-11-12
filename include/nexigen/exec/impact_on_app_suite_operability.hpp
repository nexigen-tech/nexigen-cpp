// nexigen::exec — ImpactOnAppSuiteOperability enum.
#pragma once
#include <array>
#include <cstdint>
#include <string_view>

#include <nlohmann/json.hpp>

#include <nexigen/core/enum_traits.hpp>
namespace nexigen::exec {

/// @brief Recovery policy an application failure imposes on the whole suite.
///
/// The value comes from the suite manifest field
/// `impact_on_app_suite_operability` and selects which restart path the
/// supervisor takes: per-application retry, aggregate degradation, or a
/// whole-suite relaunch. It is a plain `std::uint8_t` enum with no owning
/// members and is consulted only on the cold supervision path.
///
/// @note Spec `exec/app-suite`, requirement "Failure impact restarts the whole
///       suite".
/// @note Spec `exec/app-suite`, requirement "Degrade and not-impact levels
///       bound the damage".
enum class ImpactOnAppSuiteOperability : std::uint8_t {
    Failure,    ///< Any abnormal termination restarts the whole suite.
    Degrade,    ///< Only this application is retried; suite becomes degraded.
    Not_impact, ///< Only this application is retried; suite state is unchanged.
};

} // namespace nexigen::exec

// Vocabulary table for core::enum_name/enum_value (change refactor-enum-traits-helper).
namespace nexigen::core {

/// @brief `ImpactOnAppSuiteOperability` vocabulary table for
///        `core::enum_name`/`enum_value`.
///
/// The table preserves the manifest spellings (`failure`, `degrade`,
/// `not_impact`) and supplies `unknown` as the fallback used for an
/// unrecognized value.
///
/// @note Spec `exec/app-suite`, requirement "Suite types follow the
///       serialization contract".
template <> struct EnumTraits<nexigen::exec::ImpactOnAppSuiteOperability> {
    /// @brief One value-to-name row of the vocabulary table.
    struct Entry {
        nexigen::exec::ImpactOnAppSuiteOperability value; ///< Enum value being named.
        std::string_view name;                            ///< Manifest spelling.
    };
    /// @brief Value/name rows in declaration order.
    static constexpr std::array<Entry, 3> entries{{
        {nexigen::exec::ImpactOnAppSuiteOperability::Failure, "failure"},
        {nexigen::exec::ImpactOnAppSuiteOperability::Degrade, "degrade"},
        {nexigen::exec::ImpactOnAppSuiteOperability::Not_impact, "not_impact"},
    }};
    /// @brief Name returned when no entry matches.
    static constexpr std::string_view fallback = "unknown";
};

} // namespace nexigen::core

namespace nexigen::exec {

/// @brief Canonical manifest name of an impact level.
///
/// @param value Impact level to name.
/// @return `failure`, `degrade`, `not_impact`, or `unknown`.
/// @note Spec `exec/app-suite`, requirement "Suite types follow the
///       serialization contract"; project mandates 9/10/11.
[[nodiscard]] constexpr std::string_view to_string(ImpactOnAppSuiteOperability value) noexcept {
    return core::enum_name(value);
}

/// @brief Parse a canonical impact-level name.
///
/// @param name Lower-case manifest spelling.
/// @param out Receives the parsed value only on success.
/// @return `true` when `name` matched an entry; `false` leaves `out`
///         unchanged.
/// @note Spec `exec/app-suite`, scenario "Unknown impact value is rejected"
///       (validation path).
[[nodiscard]] constexpr bool from_string(std::string_view name,
                                         ImpactOnAppSuiteOperability& out) noexcept {
    return core::enum_value(name, out);
}

/// @brief Serialize an impact level as its canonical manifest name.
///
/// @param j Destination JSON value; replaced with a string.
/// @param value Impact level to serialize.
/// @note Spec `exec/app-suite`, requirement "Suite types follow the
///       serialization contract"; project mandate 10.
void to_json(nlohmann::json& j, ImpactOnAppSuiteOperability value);

/// @brief Parse an impact level from JSON without throwing.
///
/// Hostile or wrong-typed input leaves `value` unchanged.
///
/// @param j Source JSON value.
/// @param value Target impact level; untouched when the document is not a
///        known name.
/// @note Spec `exec/app-suite`, requirement "Suite types follow the
///       serialization contract"; project mandate 10.
void from_json(const nlohmann::json& j, ImpactOnAppSuiteOperability& value) noexcept;

} // namespace nexigen::exec
