// nexigen::exec — one application record inside suite.json
// (change refactor-execution_manager-structure; spec execution_manager/app-suite).
#pragma once

#include <cstdint>
#include <string>

#include <nlohmann/json.hpp>

namespace nexigen::exec {

/// @brief One application record inside `suite.json`.
///
/// Records the immutable parts of an application's placement: identity,
/// control instance directory, and stop grace period. `SuiteMetadata` owns a
/// vector of these; the record is written once per suite start on the cold
/// path and is otherwise read-only.
///
/// @note Spec `exec/app-suite`, requirement "Suite metadata lives in a
///       deterministic suite directory".
/// @note Spec `exec/app-suite`, requirement "Suite types follow the
///       serialization contract".
struct SuiteAppMetadata {
    std::string app{};             ///< Application name.
    std::string instance{};        ///< Instance id.
    std::string instance_dir{};    ///< Control directory `<run_dir>/<app>/<instance>`.
    std::uint64_t stop_grace_s{5}; ///< SIGTERM grace before SIGKILL.

    /// @brief Cold-path diagnostic rendering of the application record.
    ///
    /// @return Human-readable text naming the application and instance.
    /// @note Project mandate 9.
    [[nodiscard]] std::string to_string() const;
};

/// @brief Free helper (project mandate 9): mirrors the member.
///
/// @param value Application record to render.
/// @return The same text as `value.to_string()`.
[[nodiscard]] inline std::string to_string(const SuiteAppMetadata& value) {
    return value.to_string();
}

/// @brief Serialize an application record into the `suite.json` document.
///
/// @param j Destination JSON value.
/// @param app Record to serialize.
/// @note Spec `exec/app-suite`, requirement "Suite types follow the
///       serialization contract"; project mandate 10.
void to_json(nlohmann::json& j, const SuiteAppMetadata& app);

/// @brief Parse an application record from JSON without throwing.
///
/// Hostile or wrong-typed input leaves `app` unchanged.
///
/// @param j Source JSON value.
/// @param app Target record.
/// @note Spec `exec/app-suite`, requirement "Suite types follow the
///       serialization contract"; project mandate 10.
void from_json(const nlohmann::json& j, SuiteAppMetadata& app) noexcept;

} // namespace nexigen::exec
