// nexigen::exec — final suite record (`suite-exit.json`)
// (change refactor-execution_manager-structure; spec execution_manager/app-suite).
#pragma once

#include <cstdint>
#include <string>

#include <nlohmann/json.hpp>

#include <nexigen/exec/suite_state.hpp>

namespace nexigen::exec {

/// @brief Final suite record (`suite-exit.json`).
///
/// Written once when the suite reaches a terminal outcome, either normally or
/// through the forced-stop fallback. The record is the durable evidence that
/// `stop_suite` waits for and the source of the aggregate state reported by
/// `suite_status`. It is a value type consumed on the cold path only.
///
/// @note Spec `exec/app-suite`, requirement "Suite metadata lives in a
///       deterministic suite directory".
/// @note Spec `exec/app-suite`, requirement "`stop_suite` is split into
///       request, wait, and finalize phases".
/// @note Spec `exec/app-suite`, requirement "Suite types follow the
///       serialization contract".
struct SuiteExit {
    SuiteState state{SuiteState::Unknown}; ///< Terminal aggregate state.
    std::string reason{};                  ///< Stop or failure reason, e.g. `forced_stop`.
    std::uint64_t uptime_s{0};             ///< Whole seconds from suite start to exit.

    /// @brief Cold-path diagnostic rendering of the final record.
    ///
    /// @return Human-readable text naming the state, reason, and uptime.
    /// @note Project mandate 9.
    [[nodiscard]] std::string to_string() const;
};

/// @brief Free helper (project mandate 9): mirrors the member.
///
/// @param value Final record to render.
/// @return The same text as `value.to_string()`.
[[nodiscard]] inline std::string to_string(const SuiteExit& value) {
    return value.to_string();
}

/// @brief Serialize the final suite record into `suite-exit.json`.
///
/// @param j Destination JSON value.
/// @param exit Record to serialize.
/// @note Spec `exec/app-suite`, requirement "Suite types follow the
///       serialization contract"; project mandate 10.
void to_json(nlohmann::json& j, const SuiteExit& exit);

/// @brief Parse the final suite record from JSON without throwing.
///
/// Hostile or wrong-typed input leaves `exit` unchanged.
///
/// @param j Source JSON value.
/// @param exit Target record.
/// @note Spec `exec/app-suite`, requirement "Suite types follow the
///       serialization contract"; project mandate 10.
void from_json(const nlohmann::json& j, SuiteExit& exit) noexcept;

} // namespace nexigen::exec
