// nexigen::runner — application suite manifest and supervision
// (change nexigen-run-app-suite; spec runner/app-suite).
//
// COLD PATH ONLY. Manifest parsing, validation, suite metadata, and the
// supervisor loop run outside every application hot path.
#pragma once

#include <cstdint>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include <nexigen/core/result.hpp>
#include <nexigen/runner/app_state.hpp>
#include <nexigen/runner/impact_on_app_suite_operability.hpp>
#include <nexigen/runner/suite_state.hpp>
#include <nexigen/core/thread_management.hpp>
#include <nexigen/logging/log_level.hpp>

namespace nexigen::runner {

/// How one application affects the operability of the whole suite.

/// Aggregate state of a supervised suite.

/// State of one application inside a suite.

/// One application entry in the suite manifest.
struct AppSpec {
    std::string app{};
    std::string instance{};
    std::string command{};
    std::vector<std::string> args{};
    std::vector<std::pair<std::string, std::string>> env{};
    core::CpuSet cpus{};
    std::uint64_t idle_timeout_s{0};
    std::uint64_t stop_grace_s{5};
    std::uint32_t restart_attempts{0};
    std::uint64_t restart_delay_s{0};
    ImpactOnAppSuiteOperability impact{ImpactOnAppSuiteOperability::Degrade};
    std::string global_settings_path{};
    std::string local_settings_path{};
    std::optional<logging::LogLevel> log_level{};

    [[nodiscard]] std::string to_string() const;
};

/// Parsed and validated suite manifest.
struct SuiteManifest {
    std::uint32_t version{0};
    std::string suite_id{};
    std::string runner_name{};
    std::string run_id{};
    std::string run_dir{};
    std::string global_settings_path{};
    std::string artifacts_path{};
    std::optional<logging::LogLevel> log_level{};
    std::uint32_t suite_restart_attempts{0};
    std::uint64_t suite_restart_delay_s{5};
    std::string manifest_path{};
    core::ThreadSpec runner_control_thread{};
    core::ThreadSpec runner_logger_thread{};
    std::vector<AppSpec> apps{};

    [[nodiscard]] std::string to_string() const;
};

/// One application record inside suite.json.
struct SuiteAppMetadata {
    std::string app{};
    std::string instance{};
    std::string instance_dir{};
    std::uint64_t stop_grace_s{5};

    [[nodiscard]] std::string to_string() const;
};

/// Suite resource record (`suite.json`).
struct SuiteMetadata {
    std::string suite_id{};
    std::string runner_name{};
    std::string run_id{};
    std::string run_dir{};
    std::string global_settings_path{};
    std::string artifacts_path{};
    std::int32_t pid{0};
    std::string start_time_rfc3339{};
    std::uint64_t start_time_mono_ns{0};
    /// Linux `/proc/<pid>/stat` starttime ticks of the suite supervisor,
    /// recorded at spawn (PID-recycling identity anchor for suite stop).
    /// 0 = unknown (legacy pid-only checks).
    std::uint64_t proc_start_ticks{0};
    std::string manifest_path{};
    core::ThreadSpec runner_control_thread{};
    core::ThreadSpec runner_logger_thread{};
    std::vector<SuiteAppMetadata> apps{};

    [[nodiscard]] std::string to_string() const;
};

/// Final suite record (`suite-exit.json`).
struct SuiteExit {
    SuiteState state{SuiteState::Unknown};
    std::string reason{};
    std::uint64_t uptime_s{0};

    [[nodiscard]] std::string to_string() const;
};

/// One application state in `suite-state.json` and `status`.
struct SuiteAppStatus {
    std::string app{};
    std::string instance{};
    std::string instance_dir{};
    std::string artifact_dir{};
    std::uint32_t attempt{0};
    AppState state{AppState::Unknown};
    std::uint32_t restarts_used{0};

    [[nodiscard]] std::string to_string() const;
};

/// Aggregate + per-application suite state.
struct SuiteStatus {
    SuiteState state{SuiteState::Unknown};
    std::string suite_id{};
    std::string run_dir{};
    std::string artifacts_path{};
    std::uint32_t suite_restarts_used{0};
    std::vector<SuiteAppStatus> apps{};

    [[nodiscard]] std::string to_string() const;
};

// --- free to_string helpers
[[nodiscard]] inline std::string to_string(const AppSpec& value) {
    return value.to_string();
}
[[nodiscard]] inline std::string to_string(const SuiteManifest& value) {
    return value.to_string();
}
[[nodiscard]] inline std::string to_string(const SuiteAppMetadata& value) {
    return value.to_string();
}
[[nodiscard]] inline std::string to_string(const SuiteMetadata& value) {
    return value.to_string();
}
[[nodiscard]] inline std::string to_string(const SuiteExit& value) {
    return value.to_string();
}
[[nodiscard]] inline std::string to_string(const SuiteAppStatus& value) {
    return value.to_string();
}
[[nodiscard]] inline std::string to_string(const SuiteStatus& value) {
    return value.to_string();
}

// --- cold-path ADL serialization (bodies in src/runner/app_suite.cpp)
void to_json(nlohmann::json& j, ImpactOnAppSuiteOperability value);
void from_json(const nlohmann::json& j, ImpactOnAppSuiteOperability& value) noexcept;
void to_json(nlohmann::json& j, SuiteState value);
void from_json(const nlohmann::json& j, SuiteState& value) noexcept;
void to_json(nlohmann::json& j, AppState value);
void from_json(const nlohmann::json& j, AppState& value) noexcept;

void to_json(nlohmann::json& j, const AppSpec& spec);
void from_json(const nlohmann::json& j, AppSpec& spec) noexcept;
void to_json(nlohmann::json& j, const SuiteManifest& manifest);
void from_json(const nlohmann::json& j, SuiteManifest& manifest) noexcept;
void to_json(nlohmann::json& j, const SuiteAppMetadata& app);
void from_json(const nlohmann::json& j, SuiteAppMetadata& app) noexcept;
void to_json(nlohmann::json& j, const SuiteMetadata& metadata);
void from_json(const nlohmann::json& j, SuiteMetadata& metadata) noexcept;
void to_json(nlohmann::json& j, const SuiteExit& exit);
void from_json(const nlohmann::json& j, SuiteExit& exit) noexcept;
void to_json(nlohmann::json& j, const SuiteAppStatus& status);
void from_json(const nlohmann::json& j, SuiteAppStatus& status) noexcept;
void to_json(nlohmann::json& j, const SuiteStatus& status);
void from_json(const nlohmann::json& j, SuiteStatus& status) noexcept;

/// `<run_dir>/.suite/<suite_id>`.
[[nodiscard]] std::string suite_dir_path(std::string_view run_dir, std::string_view suite_id);

/// `<artifacts_path>/<suite_id>/<app>/<instance>/attempt-<NNNN>`.
[[nodiscard]] std::string attempt_dir_path(std::string_view artifacts_path,
                                           std::string_view suite_id, std::string_view app,
                                           std::string_view instance, std::uint32_t attempt);

/// Read, parse, and validate one suite manifest. `manifest_path` is resolved
/// to an absolute path so relative local settings resolve deterministically.
[[nodiscard]] core::Status load_suite_manifest(std::string_view path, SuiteManifest& out) noexcept;

/// Pure validation over an already parsed manifest.
[[nodiscard]] core::Status validate_suite_manifest(const SuiteManifest& manifest) noexcept;

/// Start a detached suite supervisor. Returns once the supervisor owns the
/// suite and wrote `suite.json`. Per-application launch failures are recorded
/// in suite state and handled by policy, not by this status.
[[nodiscard]] core::Status start_suite(const SuiteManifest& manifest, std::string& suite_dir_out);

/// Request a final suite stop and wait for the recorded outcome. Idempotent.
[[nodiscard]] core::Status
stop_suite(std::string_view suite_dir,
           std::optional<std::uint64_t> grace_override = std::nullopt) noexcept;

/// Derive aggregate and per-application state from the suite directory.
[[nodiscard]] core::Status suite_status(std::string_view suite_dir, SuiteStatus& out) noexcept;

} // namespace nexigen::runner
