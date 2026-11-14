// nexigen::exec — ProcessState enum (change
// align-exec-process-model-with-autosar; spec exec/process-types).
//
// The EM internal process lifecycle, mirroring the AUTOSAR Adaptive Platform
// process states. The suite/status JSON vocabulary remains AppState; ProcessState
// is the control-plane state used by dependency resolution, transition
// confirmation, and timeout monitoring.
#pragma once

#include <array>
#include <cstdint>
#include <format>
#include <string_view>

#include <nlohmann/json.hpp>

#include <nexigen/core/enum_traits.hpp>
#include <nexigen/core/serialization_mandate.hpp>

namespace nexigen::exec {

/// @brief Execution Manager internal process lifecycle state.
///
/// `Idle` is the state before creation; `Starting` covers allocated resources
/// and a reporting process waiting for `Run`; `Running` is serving; `Terminating`
/// means a stop request was sent; `Terminated` means the process was reaped.
///
/// @note Spec `exec/process-types`, requirement "Process lifecycle states".
enum class ProcessState : std::uint8_t {
    Idle,        ///< Before creation and resource allocation.
    Starting,    ///< Created; a reporting process has not reported Run yet.
    Running,     ///< Serving; readiness reached (explicitly or implicitly).
    Terminating, ///< A stop request was sent.
    Terminated,  ///< Reaped; resources released.
};

} // namespace nexigen::exec

// Vocabulary table for core::enum_name/enum_value.
namespace nexigen::core {

/// @brief `ProcessState` vocabulary table.
///
/// @note Spec `exec/process-types`, requirement "Process lifecycle states".
template <> struct EnumTraits<nexigen::exec::ProcessState> {
    /// @brief One value-to-name row.
    struct Entry {
        nexigen::exec::ProcessState value; ///< Enum value.
        std::string_view name;             ///< Canonical lower-case spelling.
    };
    /// @brief Value/name rows in declaration order.
    static constexpr std::array<Entry, 5> entries{{
        {nexigen::exec::ProcessState::Idle, "idle"},
        {nexigen::exec::ProcessState::Starting, "starting"},
        {nexigen::exec::ProcessState::Running, "running"},
        {nexigen::exec::ProcessState::Terminating, "terminating"},
        {nexigen::exec::ProcessState::Terminated, "terminated"},
    }};
    /// @brief Name returned when no entry matches.
    static constexpr std::string_view fallback = "unknown";
};

} // namespace nexigen::core

namespace nexigen::exec {

/// @brief Canonical name of a process state.
///
/// @param value State to name.
/// @return Lower-case state name, or `unknown`.
/// @note Spec `exec/process-types`, requirement "Process lifecycle states".
[[nodiscard]] constexpr std::string_view to_string(ProcessState value) noexcept {
    return core::enum_name(value);
}

/// @brief Parse a canonical process-state name.
///
/// @param name Lower-case name.
/// @param out Receives the value only on success.
/// @return `true` when parsed; `false` leaves `out` unchanged.
/// @note Spec `exec/process-types`, requirement "Process lifecycle states".
[[nodiscard]] constexpr bool from_string(std::string_view name, ProcessState& out) noexcept {
    return core::enum_value(name, out);
}

/// @brief Serialize a process state as its canonical name.
///
/// @param j Destination JSON value.
/// @param value State to serialize.
/// @note Spec `exec/process-types`, requirement "Process lifecycle states".
void to_json(nlohmann::json& j, ProcessState value);

/// @brief Parse a process state from JSON without throwing.
///
/// @param j Source JSON value.
/// @param value Target; unchanged on failure.
/// @note Spec `exec/process-types`, requirement "Process lifecycle states".
void from_json(const nlohmann::json& j, ProcessState& value) noexcept;

} // namespace nexigen::exec

/// @brief `std::formatter` for `ProcessState`.
template <> struct std::formatter<nexigen::exec::ProcessState> : std::formatter<std::string_view> {
    /// @brief Format through the canonical name.
    template <class Ctx> auto format(nexigen::exec::ProcessState value, Ctx& ctx) const {
        return std::formatter<std::string_view>::format(nexigen::exec::to_string(value), ctx);
    }
};
