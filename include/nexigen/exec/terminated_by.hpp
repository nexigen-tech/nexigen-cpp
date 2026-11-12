// nexigen::exec — TerminatedBy enum.
#pragma once
#include <array>
#include <cstdint>
#include <string_view>

#include <nlohmann/json.hpp>

#include <nexigen/core/enum_traits.hpp>
namespace nexigen::exec {

/// @brief Attribution of a child process termination.
///
/// `exit.json` records this value so a reader can distinguish a deliberate
/// execution_manager stop from a self-exit and from an unattributable outcome.
/// It is a plain `std::uint8_t` enum with no owning members and is written
/// only on the cold reaping path.
///
/// @note Spec `exec/instance-lifecycle`, requirement "exit.json captures exit
///       metadata".
/// @note Spec `exec/process-supervision`, requirement "Unreapable child errors
///       do not fabricate success".
enum class TerminatedBy : std::uint8_t {
    Self,             ///< The child exited on its own.
    ExecutionManager, ///< The execution_manager/stop command sent the terminating signal.
    Unknown,          ///< No attribution is available.
};

} // namespace nexigen::exec

// Vocabulary table for core::enum_name/enum_value (change refactor-enum-traits-helper).
namespace nexigen::core {

/// @brief `TerminatedBy` vocabulary table used by `core::enum_name`/`enum_value`.
///
/// The table is the single textual authority for the enum and matches the
/// `terminated_by` strings written to `exit.json`.
///
/// @note Spec `exec/instance-lifecycle`, requirement "exit.json captures exit
///       metadata".
template <> struct EnumTraits<nexigen::exec::TerminatedBy> {
    /// @brief One value-to-name row of the vocabulary table.
    struct Entry {
        nexigen::exec::TerminatedBy value; ///< Enum value being named.
        std::string_view name;             ///< Canonical lower-case spelling.
    };
    /// @brief Value/name rows in declaration order.
    static constexpr std::array<Entry, 3> entries{{
        {nexigen::exec::TerminatedBy::Self, "self"},
        {nexigen::exec::TerminatedBy::ExecutionManager, "execution_manager"},
        {nexigen::exec::TerminatedBy::Unknown, "unknown"},
    }};
};

} // namespace nexigen::core

namespace nexigen::exec {

/// @brief Canonical lower-case name used by `exit.json`.
///
/// @param value Attribution to name.
/// @return `self`, `execution_manager`, or `unknown`.
/// @note Spec `exec/instance-lifecycle`, requirement "exit.json captures exit
///       metadata"; project mandates 9/10/11.
[[nodiscard]] constexpr std::string_view to_string(TerminatedBy value) noexcept {
    return core::enum_name(value);
}

/// @brief Parse a canonical termination-attribution name.
///
/// @param name Lower-case spelling as written to `exit.json`.
/// @param out Receives the parsed value only on success.
/// @return `true` when `name` matched an entry; `false` leaves `out`
///         unchanged.
/// @note Spec `exec/instance-lifecycle`, requirement "exit.json captures exit
///       metadata".
[[nodiscard]] constexpr bool from_string(std::string_view name, TerminatedBy& out) noexcept {
    return core::enum_value(name, out);
}

/// @brief Serialize a termination attribution as its canonical name.
///
/// @param j Destination JSON value; replaced with a string.
/// @param value Attribution to serialize.
/// @note Spec `exec/instance-lifecycle`, requirement "exit.json captures exit
///       metadata"; project mandate 10.
void to_json(nlohmann::json& j, TerminatedBy value);

/// @brief Parse a termination attribution from JSON without throwing.
///
/// Hostile or wrong-typed input leaves `value` unchanged.
///
/// @param j Source JSON value.
/// @param value Target attribution; untouched when the document is not a
///        known name.
/// @note Spec `exec/instance-lifecycle`, requirement "exit.json captures exit
///       metadata"; project mandate 10.
void from_json(const nlohmann::json& j, TerminatedBy& value) noexcept;

} // namespace nexigen::exec
