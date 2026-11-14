// nexigen::exec — ProcessType enum (change introduce-platformfg-process-model;
// spec exec/process-types).
//
// A process declares how it terminates, mirroring the AUTOSAR Adaptive
// `terminationBehavior` (`processIsSelfTerminating` /
// `processIsNotSelfTerminating`). A `daemon` stays up for the whole duration of
// every phase it is bound to and reports readiness; a `oneshot` self-terminates
// and exiting with status 0 is success.
#pragma once

#include <array>
#include <cstdint>
#include <string_view>

#include <nlohmann/json.hpp>

#include <nexigen/core/enum_traits.hpp>
#include <nexigen/core/serialization_mandate.hpp>

namespace nexigen::exec {

/// @brief How a manifested process terminates.
///
/// The value comes from the suite manifest field `type`. It selects the
/// termination classification in the supervision loop and the readiness
/// contract: `Daemon` must report `Run` and is a failure if it exits without a
/// stop request; `Oneshot` is expected to exit and does not report readiness.
///
/// @note Spec `exec/process-types`, requirement "Process termination behavior
///       is declared".
enum class ProcessType : std::uint8_t {
    Daemon,  ///< Continuous process; runs for the whole bound phase.
    Oneshot, ///< Transient process; self-terminates, exit 0 is success.
};

} // namespace nexigen::exec

// Vocabulary table for core::enum_name/enum_value.
namespace nexigen::core {

/// @brief `ProcessType` vocabulary table for `core::enum_name`/`enum_value`.
///
/// The table preserves the manifest spellings (`daemon`, `oneshot`) and
/// supplies `unknown` as the fallback used for an unrecognized value.
///
/// @note Spec `exec/process-types`, requirement "Process termination behavior
///       is declared".
template <> struct EnumTraits<nexigen::exec::ProcessType> {
    /// @brief One value-to-name row of the vocabulary table.
    struct Entry {
        nexigen::exec::ProcessType value; ///< Enum value being named.
        std::string_view name;            ///< Manifest spelling.
    };
    /// @brief Value/name rows in declaration order.
    static constexpr std::array<Entry, 2> entries{{
        {nexigen::exec::ProcessType::Daemon, "daemon"},
        {nexigen::exec::ProcessType::Oneshot, "oneshot"},
    }};
    /// @brief Name returned when no entry matches.
    static constexpr std::string_view fallback = "unknown";
};

} // namespace nexigen::core

namespace nexigen::exec {

/// @brief Canonical manifest name of a process type.
///
/// @param value Process type to name.
/// @return `daemon`, `oneshot`, or `unknown`.
/// @note Spec `exec/process-types`, requirement "Process termination behavior
///       is declared"; project mandates 9/10/11.
[[nodiscard]] constexpr std::string_view to_string(ProcessType value) noexcept {
    return core::enum_name(value);
}

/// @brief Parse a canonical process-type name.
///
/// @param name Lower-case manifest spelling.
/// @param out Receives the parsed value only on success.
/// @return `true` when `name` matched an entry; `false` leaves `out`
///         unchanged.
/// @note Spec `exec/process-types`, requirement "Process termination behavior
///       is declared".
[[nodiscard]] constexpr bool from_string(std::string_view name, ProcessType& out) noexcept {
    return core::enum_value(name, out);
}

/// @brief Serialize a process type as its canonical manifest name.
///
/// @param j Destination JSON value; replaced with a string.
/// @param value Process type to serialize.
/// @note Spec `exec/process-types`, requirement "Process termination behavior
///       is declared"; project mandate 10.
void to_json(nlohmann::json& j, ProcessType value);

/// @brief Parse a process type from JSON without throwing.
///
/// Hostile or wrong-typed input leaves `value` unchanged.
///
/// @param j Source JSON value.
/// @param value Target process type; untouched when the document is not a
///        known name.
/// @note Spec `exec/process-types`, requirement "Process termination behavior
///       is declared"; project mandate 10.
void from_json(const nlohmann::json& j, ProcessType& value) noexcept;

} // namespace nexigen::exec
