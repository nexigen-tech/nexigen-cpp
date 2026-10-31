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
struct Run_identity {
    std::string run_id{};
    std::string run_dir{};
    std::string app_name{};
    std::string instance_id{};
    std::string instance_dir{};

    [[nodiscard]] std::string to_string() const;
};
[[nodiscard]] inline std::string to_string(const Run_identity& identity) {
    return identity.to_string();
}

/// Resolved output locations. An empty path means the resolver did not
/// configure that stream; the application keeps its own policy.
struct Telemetry_paths {
    std::string metrics{};
    std::string traces{};
    std::string chrono{};

    [[nodiscard]] std::string to_string() const;
};
[[nodiscard]] inline std::string to_string(const Telemetry_paths& paths) {
    return paths.to_string();
}

/// Read NEXIGEN_RUN_ID, NEXIGEN_RUN_DIR, NEXIGEN_APP_NAME,
/// NEXIGEN_INSTANCE_ID, and NEXIGEN_INSTANCE_DIR. When the instance directory
/// is absent and the three derivation inputs are present, derive it as
/// `<run_dir>/<app_name>/<instance_id>`. Never consults CWD, hostname, or PID.
[[nodiscard]] Run_identity load_run_identity() noexcept;

/// Validate present ids (1..128 bytes, [A-Za-z0-9._-], no "."/".."/"..")
/// and present directories (POSIX absolute). A completely empty identity is
/// valid. Returns InvalidArgument on any violation.
[[nodiscard]] core::Status validate_run_identity(const Run_identity& identity) noexcept;

/// Resolve each stream independently: explicit non-empty path, otherwise
/// `<instance_dir>/<metrics|traces|chrono>.jsonl`, otherwise empty. The
/// resolver performs no I/O and leaves `out` untouched on invalid identity.
[[nodiscard]] core::Status resolve_telemetry_paths(
    const Run_identity& identity, std::string_view explicit_metrics_path,
    std::string_view explicit_traces_path, std::string_view explicit_chrono_path,
    Telemetry_paths& out) noexcept;

/// Settings convenience overload: reads `sink.path` from each subsystem.
[[nodiscard]] inline core::Status resolve_telemetry_paths(
    const Run_identity& identity, const Metrics_settings& metrics,
    const Tracing_settings& tracing, const chrono::Chrono_settings& chrono,
    Telemetry_paths& out) noexcept {
    return resolve_telemetry_paths(identity, metrics.sink.path, tracing.sink.path,
                                   chrono.sink.path, out);
}

// --- cold-path ADL serialization (bodies in src/app/telemetry_layout.cpp)
void to_json(nlohmann::json& j, const Run_identity& identity);
void from_json(const nlohmann::json& j, Run_identity& identity) noexcept;
void to_json(nlohmann::json& j, const Telemetry_paths& paths);
void from_json(const nlohmann::json& j, Telemetry_paths& paths) noexcept;

} // namespace nexigen::app

template <>
struct std::formatter<nexigen::app::Run_identity> : std::formatter<std::string_view> {
    template <class Ctx>
    constexpr auto format(const nexigen::app::Run_identity& identity, Ctx& ctx) const {
        return std::formatter<std::string_view>::format(identity.to_string(), ctx);
    }
};

template <>
struct std::formatter<nexigen::app::Telemetry_paths> : std::formatter<std::string_view> {
    template <class Ctx>
    constexpr auto format(const nexigen::app::Telemetry_paths& paths, Ctx& ctx) const {
        return std::formatter<std::string_view>::format(paths.to_string(), ctx);
    }
};
