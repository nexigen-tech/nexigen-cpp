// nexigen::app — identity of one application instance inside one run (spec app/telemetry-layout).
#pragma once
#include <string>
#include <nlohmann/json.hpp>
namespace nexigen::app {

/// @brief Identity of one application instance inside one run.
///
/// Populated from the fixed `NEXIGEN_*` environment contract, never from the
/// working directory, hostname or process id. The application name is not part
/// of run identity and is supplied separately. All fields are independent: a
/// partial environment yields a partially populated identity without failing.
///
/// @note Spec `app/telemetry-layout`, requirement "Run identity is read from a
///       fixed environment contract".
/// @note Rendering, JSON and formatting follow project mandates 9, 10 and 11.
struct RunIdentity {
    std::string run_id{};       ///< `NEXIGEN_RUN_ID` ("" = absent).
    std::string run_dir{};      ///< Run directory from `NEXIGEN_EXEC_DIR` ("" = absent).
    std::string instance_id{};  ///< `NEXIGEN_INSTANCE_ID` ("" = absent).
    std::string instance_dir{}; ///< `NEXIGEN_INSTANCE_DIR` or the derived directory.

    /// @brief Render the identity as a cold-path diagnostic string.
    ///
    /// @return Text naming every identity field.
    /// @note Spec `app/telemetry-layout`, requirement "Identity and resolved
    ///       paths are serializable"; diagnostic only (project mandate 9).
    [[nodiscard]] std::string to_string() const;
};
/// @brief Free diagnostic helper mirroring `RunIdentity::to_string()`.
///
/// @param identity Identity to render.
/// @return The same text the member returns.
/// @note Diagnostic only (project mandate 9).
[[nodiscard]] inline std::string to_string(const RunIdentity& identity) {
    return identity.to_string();
}
/// @brief Write the identity as JSON (cold path).
///
/// @param j Output JSON object with one field per identity member.
/// @param identity Identity to serialize.
/// @note Spec `app/telemetry-layout`, requirement "Identity and resolved paths
///       are serializable"; JSON only (project mandate 10).
void to_json(nlohmann::json& j, const RunIdentity& identity);
/// @brief Read the identity from JSON (cold path).
///
/// @param j Input JSON; a non-object, wrong-typed field or extra field leaves
///        the target's previous value intact.
/// @param identity Destination, never throwing under `JSON_NOEXCEPTION=1`.
/// @note Spec `app/telemetry-layout`, requirement "Identity and resolved paths
///       are serializable".
void from_json(const nlohmann::json& j, RunIdentity& identity) noexcept;
} // namespace nexigen::app
