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
#include <nexigen/logging/log_level.hpp>

namespace nexigen::runner {

/// How one application affects the operability of the whole suite.
enum class Impact_on_app_suite_operability : std::uint8_t {
    Failure,    ///< Any abnormal termination restarts the whole suite.
    Degrade,    ///< Only this application is retried; suite becomes degraded.
    Not_impact, ///< Only this application is retried; suite state is unchanged.
};

[[nodiscard]] constexpr std::string_view to_string(
    Impact_on_app_suite_operability value) noexcept {
    switch (value) {
        case Impact_on_app_suite_operability::Failure:    return "failure";
        case Impact_on_app_suite_operability::Degrade:    return "degrade";
        case Impact_on_app_suite_operability::Not_impact: return "not_impact";
        default:                                          return "unknown";
    }
}

[[nodiscard]] constexpr bool from_string(
    std::string_view name, Impact_on_app_suite_operability& out) noexcept {
    if (name == "failure")    { out = Impact_on_app_suite_operability::Failure;    return true; }
    if (name == "degrade")    { out = Impact_on_app_suite_operability::Degrade;    return true; }
    if (name == "not_impact") { out = Impact_on_app_suite_operability::Not_impact; return true; }
    return false;
}

/// Aggregate state of a supervised suite.
enum class Suite_state : std::uint8_t {
    Running,
    Degraded,
    Failed,
    Stopped,
    Unknown,
};

[[nodiscard]] constexpr std::string_view to_string(Suite_state value) noexcept {
    switch (value) {
        case Suite_state::Running:  return "running";
        case Suite_state::Degraded: return "degraded";
        case Suite_state::Failed:   return "failed";
        case Suite_state::Stopped:  return "stopped";
        case Suite_state::Unknown:  return "unknown";
        default:                    return "unknown";
    }
}

[[nodiscard]] constexpr bool from_string(std::string_view name,
                                         Suite_state& out) noexcept {
    if (name == "running")  { out = Suite_state::Running;  return true; }
    if (name == "degraded") { out = Suite_state::Degraded; return true; }
    if (name == "failed")   { out = Suite_state::Failed;   return true; }
    if (name == "stopped")  { out = Suite_state::Stopped;  return true; }
    if (name == "unknown")  { out = Suite_state::Unknown;  return true; }
    return false;
}

/// State of one application inside a suite.
enum class App_state : std::uint8_t {
    Running,
    Restarting,
    Exited,
    Stopped,
    Failed,
    Unknown,
};

[[nodiscard]] constexpr std::string_view to_string(App_state value) noexcept {
    switch (value) {
        case App_state::Running:    return "running";
        case App_state::Restarting: return "restarting";
        case App_state::Exited:     return "exited";
        case App_state::Stopped:    return "stopped";
        case App_state::Failed:     return "failed";
        case App_state::Unknown:    return "unknown";
        default:                    return "unknown";
    }
}

[[nodiscard]] constexpr bool from_string(std::string_view name,
                                         App_state& out) noexcept {
    if (name == "running")    { out = App_state::Running;    return true; }
    if (name == "restarting") { out = App_state::Restarting; return true; }
    if (name == "exited")     { out = App_state::Exited;     return true; }
    if (name == "stopped")    { out = App_state::Stopped;    return true; }
    if (name == "failed")     { out = App_state::Failed;     return true; }
    if (name == "unknown")    { out = App_state::Unknown;    return true; }
    return false;
}

/// One application entry in the suite manifest.
struct App_spec {
    std::string app{};
    std::string instance{};
    std::string command{};
    std::vector<std::string> args{};
    std::vector<std::pair<std::string, std::string>> env{};
    std::vector<std::uint32_t> cpus{};
    std::uint64_t idle_timeout_s{0};
    std::uint64_t stop_grace_s{5};
    std::uint32_t restart_attempts{0};
    std::uint64_t restart_delay_s{0};
    Impact_on_app_suite_operability impact{Impact_on_app_suite_operability::Degrade};
    std::string global_settings_path{};
    std::string local_settings_path{};
    std::optional<logging::LogLevel> log_level{};

    [[nodiscard]] std::string to_string() const;
};

/// Parsed and validated suite manifest.
struct Suite_manifest {
    std::uint32_t version{0};
    std::string suite_id{};
    std::string run_id{};
    std::string run_dir{};
    std::string global_settings_path{};
    std::string artifacts_path{};
    std::optional<logging::LogLevel> log_level{};
    std::uint32_t suite_restart_attempts{0};
    std::uint64_t suite_restart_delay_s{5};
    std::string manifest_path{};
    std::vector<std::uint32_t> runner_control_cpus{};
    std::vector<std::uint32_t> runner_logger_cpus{};
    std::vector<App_spec> apps{};

    [[nodiscard]] std::string to_string() const;
};

/// One application record inside suite.json.
struct Suite_app_metadata {
    std::string app{};
    std::string instance{};
    std::string instance_dir{};
    std::uint64_t stop_grace_s{5};

    [[nodiscard]] std::string to_string() const;
};

/// Suite resource record (`suite.json`).
struct Suite_metadata {
    std::string suite_id{};
    std::string run_id{};
    std::string run_dir{};
    std::string global_settings_path{};
    std::string artifacts_path{};
    std::int32_t pid{0};
    std::string start_time_rfc3339{};
    std::uint64_t start_time_mono_ns{0};
    std::string manifest_path{};
    std::vector<std::uint32_t> runner_control_cpus{};
    std::vector<std::uint32_t> runner_logger_cpus{};
    std::vector<Suite_app_metadata> apps{};

    [[nodiscard]] std::string to_string() const;
};

/// Final suite record (`suite-exit.json`).
struct Suite_exit {
    Suite_state state{Suite_state::Unknown};
    std::string reason{};
    std::uint64_t uptime_s{0};

    [[nodiscard]] std::string to_string() const;
};

/// One application state in `suite-state.json` and `status`.
struct Suite_app_status {
    std::string app{};
    std::string instance{};
    std::string instance_dir{};
    std::string artifact_dir{};
    std::uint32_t attempt{0};
    App_state state{App_state::Unknown};
    std::uint32_t restarts_used{0};

    [[nodiscard]] std::string to_string() const;
};

/// Aggregate + per-application suite state.
struct Suite_status {
    Suite_state state{Suite_state::Unknown};
    std::string suite_id{};
    std::string run_dir{};
    std::string artifacts_path{};
    std::uint32_t suite_restarts_used{0};
    std::vector<Suite_app_status> apps{};

    [[nodiscard]] std::string to_string() const;
};

// --- free to_string helpers
[[nodiscard]] inline std::string to_string(const App_spec& value) { return value.to_string(); }
[[nodiscard]] inline std::string to_string(const Suite_manifest& value) { return value.to_string(); }
[[nodiscard]] inline std::string to_string(const Suite_app_metadata& value) { return value.to_string(); }
[[nodiscard]] inline std::string to_string(const Suite_metadata& value) { return value.to_string(); }
[[nodiscard]] inline std::string to_string(const Suite_exit& value) { return value.to_string(); }
[[nodiscard]] inline std::string to_string(const Suite_app_status& value) { return value.to_string(); }
[[nodiscard]] inline std::string to_string(const Suite_status& value) { return value.to_string(); }

// --- cold-path ADL serialization (bodies in src/runner/app_suite.cpp)
void to_json(nlohmann::json& j, Impact_on_app_suite_operability value);
void from_json(const nlohmann::json& j, Impact_on_app_suite_operability& value) noexcept;
void to_json(nlohmann::json& j, Suite_state value);
void from_json(const nlohmann::json& j, Suite_state& value) noexcept;
void to_json(nlohmann::json& j, App_state value);
void from_json(const nlohmann::json& j, App_state& value) noexcept;

void to_json(nlohmann::json& j, const App_spec& spec);
void from_json(const nlohmann::json& j, App_spec& spec) noexcept;
void to_json(nlohmann::json& j, const Suite_manifest& manifest);
void from_json(const nlohmann::json& j, Suite_manifest& manifest) noexcept;
void to_json(nlohmann::json& j, const Suite_app_metadata& app);
void from_json(const nlohmann::json& j, Suite_app_metadata& app) noexcept;
void to_json(nlohmann::json& j, const Suite_metadata& metadata);
void from_json(const nlohmann::json& j, Suite_metadata& metadata) noexcept;
void to_json(nlohmann::json& j, const Suite_exit& exit);
void from_json(const nlohmann::json& j, Suite_exit& exit) noexcept;
void to_json(nlohmann::json& j, const Suite_app_status& status);
void from_json(const nlohmann::json& j, Suite_app_status& status) noexcept;
void to_json(nlohmann::json& j, const Suite_status& status);
void from_json(const nlohmann::json& j, Suite_status& status) noexcept;

/// `<run_dir>/.suite/<suite_id>`.
[[nodiscard]] std::string suite_dir_path(std::string_view run_dir,
                                         std::string_view suite_id);

/// `<artifacts_path>/<suite_id>/<app>/<instance>/attempt-<NNNN>`.
[[nodiscard]] std::string attempt_dir_path(std::string_view artifacts_path,
                                           std::string_view suite_id,
                                           std::string_view app,
                                           std::string_view instance,
                                           std::uint32_t attempt);

/// Read, parse, and validate one suite manifest. `manifest_path` is resolved
/// to an absolute path so relative local settings resolve deterministically.
[[nodiscard]] core::Status load_suite_manifest(std::string_view path,
                                               Suite_manifest& out) noexcept;

/// Pure validation over an already parsed manifest.
[[nodiscard]] core::Status validate_suite_manifest(
    const Suite_manifest& manifest) noexcept;

/// Start a detached suite supervisor. Returns once the supervisor owns the
/// suite and wrote `suite.json`. Per-application launch failures are recorded
/// in suite state and handled by policy, not by this status.
[[nodiscard]] core::Status start_suite(const Suite_manifest& manifest,
                                       std::string& suite_dir_out);

/// Request a final suite stop and wait for the recorded outcome. Idempotent.
[[nodiscard]] core::Status stop_suite(
    std::string_view suite_dir,
    std::optional<std::uint64_t> grace_override = std::nullopt) noexcept;

/// Derive aggregate and per-application state from the suite directory.
[[nodiscard]] core::Status suite_status(std::string_view suite_dir,
                                        Suite_status& out) noexcept;

} // namespace nexigen::runner

template <>
struct std::formatter<nexigen::runner::Impact_on_app_suite_operability>
    : std::formatter<std::string_view> {
    template <class Ctx>
    constexpr auto format(const nexigen::runner::Impact_on_app_suite_operability value,
                          Ctx& ctx) const {
        return std::formatter<std::string_view>::format(
            nexigen::runner::to_string(value), ctx);
    }
};

template <>
struct std::formatter<nexigen::runner::Suite_state>
    : std::formatter<std::string_view> {
    template <class Ctx>
    constexpr auto format(const nexigen::runner::Suite_state value, Ctx& ctx) const {
        return std::formatter<std::string_view>::format(
            nexigen::runner::to_string(value), ctx);
    }
};

template <>
struct std::formatter<nexigen::runner::App_state>
    : std::formatter<std::string_view> {
    template <class Ctx>
    constexpr auto format(const nexigen::runner::App_state value, Ctx& ctx) const {
        return std::formatter<std::string_view>::format(
            nexigen::runner::to_string(value), ctx);
    }
};

template <>
struct std::formatter<nexigen::runner::App_spec>
    : std::formatter<std::string_view> {
    template <class Ctx>
    constexpr auto format(const nexigen::runner::App_spec& value, Ctx& ctx) const {
        return std::formatter<std::string_view>::format(value.to_string(), ctx);
    }
};

template <>
struct std::formatter<nexigen::runner::Suite_manifest>
    : std::formatter<std::string_view> {
    template <class Ctx>
    constexpr auto format(const nexigen::runner::Suite_manifest& value, Ctx& ctx) const {
        return std::formatter<std::string_view>::format(value.to_string(), ctx);
    }
};

template <>
struct std::formatter<nexigen::runner::Suite_app_metadata>
    : std::formatter<std::string_view> {
    template <class Ctx>
    constexpr auto format(const nexigen::runner::Suite_app_metadata& value, Ctx& ctx) const {
        return std::formatter<std::string_view>::format(value.to_string(), ctx);
    }
};

template <>
struct std::formatter<nexigen::runner::Suite_metadata>
    : std::formatter<std::string_view> {
    template <class Ctx>
    constexpr auto format(const nexigen::runner::Suite_metadata& value, Ctx& ctx) const {
        return std::formatter<std::string_view>::format(value.to_string(), ctx);
    }
};

template <>
struct std::formatter<nexigen::runner::Suite_exit>
    : std::formatter<std::string_view> {
    template <class Ctx>
    constexpr auto format(const nexigen::runner::Suite_exit& value, Ctx& ctx) const {
        return std::formatter<std::string_view>::format(value.to_string(), ctx);
    }
};

template <>
struct std::formatter<nexigen::runner::Suite_app_status>
    : std::formatter<std::string_view> {
    template <class Ctx>
    constexpr auto format(const nexigen::runner::Suite_app_status& value, Ctx& ctx) const {
        return std::formatter<std::string_view>::format(value.to_string(), ctx);
    }
};

template <>
struct std::formatter<nexigen::runner::Suite_status>
    : std::formatter<std::string_view> {
    template <class Ctx>
    constexpr auto format(const nexigen::runner::Suite_status& value, Ctx& ctx) const {
        return std::formatter<std::string_view>::format(value.to_string(), ctx);
    }
};
