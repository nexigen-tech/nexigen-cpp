// nexigen::exec — AppState enum.
#pragma once
#include <array>
#include <cstdint>
#include <string_view>

#include <nlohmann/json.hpp>

#include <nexigen/core/enum_traits.hpp>
namespace nexigen::exec {

/// @brief Lifecycle state of one supervised application instance.
///
/// The value is the vocabulary shared by `suite-state.json`, the suite
/// `status` output, and the per-application readiness notification. It is a
/// plain `std::uint8_t` enum with no owning members, so it is cheap to copy
/// and safe to embed in status records. Recording and transition logic live on
/// the cold supervision path only.
///
/// @note Spec `exec/app-suite`, requirement "Suite status reports aggregate and
///       application state".
/// @note Spec `exec/app-suite`, requirement "Suite types follow the
///       serialization contract".
enum class AppState : std::uint8_t {
    Running,    ///< The child is alive and has reported `Running`.
    Restarting, ///< A retry is scheduled or in progress after abnormal termination.
    Exited,     ///< The child terminated on its own with exit code 0.
    Stopped,    ///< The execution_manager stopped the child deliberately.
    Failed,     ///< Launch or restart attempts are exhausted, or launch failed.
    Unknown,    ///< No trustworthy state is available.
};

} // namespace nexigen::exec

// Vocabulary table for core::enum_name/enum_value (change refactor-enum-traits-helper).
namespace nexigen::core {

/// @brief `AppState` vocabulary table used by `core::enum_name`/`enum_value`.
///
/// The table is the single textual authority for the enum: it preserves the
/// manifest and JSON spellings (`running`, `restarting`, `exited`, `stopped`,
/// `failed`, `unknown`) and supplies `unknown` as the fallback for an
/// unrecognized value.
///
/// @note Spec `exec/app-suite`, requirement "Suite types follow the
///       serialization contract".
template <> struct EnumTraits<nexigen::exec::AppState> {
    /// @brief One value-to-name row of the vocabulary table.
    struct Entry {
        nexigen::exec::AppState value; ///< Enum value being named.
        std::string_view name;         ///< Canonical lower-case spelling.
    };
    /// @brief Value/name rows in declaration order.
    static constexpr std::array<Entry, 6> entries{{
        {nexigen::exec::AppState::Running, "running"},
        {nexigen::exec::AppState::Restarting, "restarting"},
        {nexigen::exec::AppState::Exited, "exited"},
        {nexigen::exec::AppState::Stopped, "stopped"},
        {nexigen::exec::AppState::Failed, "failed"},
        {nexigen::exec::AppState::Unknown, "unknown"},
    }};
    /// @brief Name returned when no entry matches.
    static constexpr std::string_view fallback = "unknown";
};

} // namespace nexigen::core

namespace nexigen::exec {

/// @brief Canonical lower-case name of an application state.
///
/// @param value State to name.
/// @return The vocabulary spelling, or `unknown` for an unrecognized value.
/// @note Spec `exec/app-suite`, requirement "Suite types follow the
///       serialization contract"; project mandates 9/10/11.
[[nodiscard]] constexpr std::string_view to_string(AppState value) noexcept {
    return core::enum_name(value);
}

/// @brief Parse a canonical application-state name.
///
/// @param name Lower-case spelling such as `running` or `failed`.
/// @param out Receives the parsed value only on success.
/// @return `true` when `name` matched an entry; `false` leaves `out`
///         unchanged.
/// @note Spec `exec/app-suite`, requirement "Suite types follow the
///       serialization contract".
[[nodiscard]] constexpr bool from_string(std::string_view name, AppState& out) noexcept {
    return core::enum_value(name, out);
}

/// @brief Serialize an application state as its canonical name.
///
/// @param j Destination JSON value; replaced with a string.
/// @param value State to serialize.
/// @note Spec `exec/app-suite`, requirement "Suite types follow the
///       serialization contract"; project mandate 10.
void to_json(nlohmann::json& j, AppState value);

/// @brief Parse an application state from JSON without throwing.
///
/// Hostile or wrong-typed input leaves `value` unchanged.
///
/// @param j Source JSON value.
/// @param value Target state; untouched when the document is not a known name.
/// @note Spec `exec/app-suite`, requirement "Suite types follow the
///       serialization contract"; project mandate 10.
void from_json(const nlohmann::json& j, AppState& value) noexcept;

} // namespace nexigen::exec
