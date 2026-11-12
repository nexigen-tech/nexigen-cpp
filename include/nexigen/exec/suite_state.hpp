// nexigen::exec — SuiteState enum.
#pragma once
#include <array>
#include <cstdint>
#include <string_view>

#include <nlohmann/json.hpp>

#include <nexigen/core/enum_traits.hpp>
namespace nexigen::exec {

/// @brief Aggregate lifecycle state of a supervised application suite.
///
/// The value is recorded in `suite-state.json` and `suite-exit.json` and is
/// what the suite `status` command reports. It is a plain `std::uint8_t` enum
/// with no owning members, so it is cheap to copy. Transitions are computed by
/// the detached suite supervisor on the cold path.
///
/// @note Spec `exec/app-suite`, requirement "Suite status reports aggregate and
///       application state".
/// @note Spec `exec/app-suite`, requirement "Suite types follow the
///       serialization contract".
enum class SuiteState : std::uint8_t {
    Running,  ///< Every application is running within its policy.
    Degraded, ///< A `degrade` application is failed while the rest keeps running.
    Failed,   ///< The restart budget is exhausted or a `failure` app failed terminally.
    Stopped,  ///< The suite was stopped on request and will not restart.
    Unknown,  ///< No readable `suite.json` exists or no state has been recorded.
};

} // namespace nexigen::exec

// Vocabulary table for core::enum_name/enum_value (change refactor-enum-traits-helper).
namespace nexigen::core {

/// @brief `SuiteState` vocabulary table used by `core::enum_name`/`enum_value`.
///
/// The table is the single textual authority for the enum and supplies
/// `unknown` as the fallback for an unrecognized value.
///
/// @note Spec `exec/app-suite`, requirement "Suite types follow the
///       serialization contract".
template <> struct EnumTraits<nexigen::exec::SuiteState> {
    /// @brief One value-to-name row of the vocabulary table.
    struct Entry {
        nexigen::exec::SuiteState value; ///< Enum value being named.
        std::string_view name;           ///< Canonical lower-case spelling.
    };
    /// @brief Value/name rows in declaration order.
    static constexpr std::array<Entry, 5> entries{{
        {nexigen::exec::SuiteState::Running, "running"},
        {nexigen::exec::SuiteState::Degraded, "degraded"},
        {nexigen::exec::SuiteState::Failed, "failed"},
        {nexigen::exec::SuiteState::Stopped, "stopped"},
        {nexigen::exec::SuiteState::Unknown, "unknown"},
    }};
    /// @brief Name returned when no entry matches.
    static constexpr std::string_view fallback = "unknown";
};

} // namespace nexigen::core

namespace nexigen::exec {

/// @brief Canonical lower-case name of an aggregate suite state.
///
/// @param value State to name.
/// @return The vocabulary spelling, or `unknown` for an unrecognized value.
/// @note Spec `exec/app-suite`, requirement "Suite types follow the
///       serialization contract"; project mandates 9/10/11.
[[nodiscard]] constexpr std::string_view to_string(SuiteState value) noexcept {
    return core::enum_name(value);
}

/// @brief Parse a canonical aggregate suite-state name.
///
/// @param name Lower-case spelling such as `degraded` or `stopped`.
/// @param out Receives the parsed value only on success.
/// @return `true` when `name` matched an entry; `false` leaves `out`
///         unchanged.
/// @note Spec `exec/app-suite`, requirement "Suite types follow the
///       serialization contract".
[[nodiscard]] constexpr bool from_string(std::string_view name, SuiteState& out) noexcept {
    return core::enum_value(name, out);
}

/// @brief Serialize an aggregate suite state as its canonical name.
///
/// @param j Destination JSON value; replaced with a string.
/// @param value State to serialize.
/// @note Spec `exec/app-suite`, requirement "Suite types follow the
///       serialization contract"; project mandate 10.
void to_json(nlohmann::json& j, SuiteState value);

/// @brief Parse an aggregate suite state from JSON without throwing.
///
/// Hostile or wrong-typed input leaves `value` unchanged.
///
/// @param j Source JSON value.
/// @param value Target state; untouched when the document is not a known name.
/// @note Spec `exec/app-suite`, requirement "Suite types follow the
///       serialization contract"; project mandate 10.
void from_json(const nlohmann::json& j, SuiteState& value) noexcept;

} // namespace nexigen::exec
