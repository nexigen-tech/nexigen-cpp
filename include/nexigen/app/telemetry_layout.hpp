// nexigen::app — run/instance identity and telemetry path resolution
// (change add-telemetry-output-contract; spec app/telemetry-layout).
//
// COLD PATH ONLY. `load_run_identity` reads the NEXIGEN_* environment contract,
// `validate_run_identity` checks it, and `resolve_telemetry_paths` maps it to
// the four JSONL stream locations. None of this may be called from a tick,
// producer, snapshot, or sink path.
//
// The contract is intentionally language-neutral: the same variables and field
// names are used by the C# and Python implementations.
#pragma once

#include <format>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

#include <nexigen/app/app_settings.hpp>
#include <nexigen/core/result.hpp>

namespace nexigen::app {

/// Identity of one application instance inside one run. Empty fields mean the
/// corresponding variable was unset; a completely empty value is valid and
/// means "this application does not use the identity contract".
struct RunIdentity {
    std::string run_id{};      ///< NEXIGEN_RUN_ID
    std::string run_dir{};     ///< NEXIGEN_RUN_DIR
    std::string instance_id{}; ///< NEXIGEN_INSTANCE_ID
    std::string
        instance_dir{}; ///< NEXIGEN_INSTANCE_DIR or derived <run_dir>/<app_name>/<instance_id>

    [[nodiscard]] std::string to_string() const;
};
[[nodiscard]] inline std::string to_string(const RunIdentity& identity) {
    return identity.to_string();
}

/// Resolved output locations. An empty path means the resolver did not
/// configure that stream; the application keeps its own policy.
struct TelemetryPaths {
    std::string metrics{}; ///< Resolved path for metrics.jsonl, or empty.
    std::string traces{};  ///< Resolved path for traces.jsonl, or empty.
    std::string chrono{};  ///< Resolved path for chrono.jsonl, or empty.

    [[nodiscard]] std::string to_string() const;
};
[[nodiscard]] inline std::string to_string(const TelemetryPaths& paths) {
    return paths.to_string();
}

/// Read NEXIGEN_RUN_ID, NEXIGEN_RUN_DIR,
/// NEXIGEN_INSTANCE_ID, and NEXIGEN_INSTANCE_DIR. The application name is not
/// part of run identity and is supplied separately. Never consults CWD,
/// hostname, or PID.
[[nodiscard]] RunIdentity load_run_identity() noexcept;

/// Derive the instance directory as `<run_dir>/<app_name>/<instance_id>` from a
/// separately supplied application name.
[[nodiscard]] std::string derive_instance_dir(std::string_view run_dir, std::string_view app_name,
                                              std::string_view instance_id);

/// Validate present ids (1..128 bytes, [A-Za-z0-9._-], no "."/".."/"..")
/// and present directories (POSIX absolute). A completely empty identity is
/// valid. Returns InvalidArgument on any violation.
[[nodiscard]] core::Status validate_run_identity(const RunIdentity& identity) noexcept;

/// Resolve each stream independently: explicit non-empty path, otherwise
/// `<instance_dir>/<metrics|traces|chrono>.jsonl`, otherwise empty. The
/// resolver performs no I/O and leaves `out` untouched on invalid identity.
[[nodiscard]] core::Status resolve_telemetry_paths(const RunIdentity& identity,
                                                   std::string_view explicit_metrics_path,
                                                   std::string_view explicit_traces_path,
                                                   std::string_view explicit_chrono_path,
                                                   TelemetryPaths& out) noexcept;

/// Settings convenience overload: reads `sink.path` from each subsystem.
[[nodiscard]] inline core::Status resolve_telemetry_paths(const RunIdentity& identity,
                                                          const metrics::Settings& metrics,
                                                          const tracing::Settings& tracing,
                                                          const chrono::Settings& chrono,
                                                          TelemetryPaths& out) noexcept {
    return resolve_telemetry_paths(identity, metrics.sink.path, tracing.sink.path, chrono.sink.path,
                                   out);
}

// --- cold-path ADL serialization (bodies in src/app/telemetry_layout.cpp)
/// Serialise a RunIdentity into JSON. Field names match the variable names.
void to_json(nlohmann::json& j, const RunIdentity& identity);
/// Deserialise a RunIdentity from JSON. Non-object, wrong types, and extra
/// fields leave the target unchanged when compiled with JSON_NOEXCEPTION=1.
void from_json(const nlohmann::json& j, RunIdentity& identity) noexcept;
/// Serialise a TelemetryPaths into JSON.
void to_json(nlohmann::json& j, const TelemetryPaths& paths);
/// Deserialise a TelemetryPaths from JSON. Same hostile-JSON guarantees as
/// from_json(RunIdentity).
void from_json(const nlohmann::json& j, TelemetryPaths& paths) noexcept;

} // namespace nexigen::app
