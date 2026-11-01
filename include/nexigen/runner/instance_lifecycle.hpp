// nexigen::runner — instance lifecycle metadata (resource.json / exit.json)
// (change nexigen-run; spec runner/instance-lifecycle).
//
// COLD PATH ONLY. All functions here run before a child process starts or
// after it has been reaped. None of them may be called from a tick, producer,
// snapshot, or sink callback.
#pragma once

#include <cstdint>
#include <format>
#include <optional>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

#include <nexigen/app/telemetry_layout.hpp>
#include <nexigen/core/result.hpp>

namespace nexigen::runner {

/// Why the child process ended.
enum class Terminated_by : std::uint8_t {
    Self,    ///< The child exited on its own.
    Runner,  ///< The runner/stop command sent the terminating signal.
    Unknown, ///< No attribution is available.
};

/// Canonical lower-case name used by exit.json.
[[nodiscard]] constexpr std::string_view to_string(Terminated_by value) noexcept {
    switch (value) {
        case Terminated_by::Self:    return "self";
        case Terminated_by::Runner:  return "runner";
        case Terminated_by::Unknown: return "unknown";
        default:                     return "Unknown";
    }
}

/// Parse a canonical name; false leaves `out` unchanged.
[[nodiscard]] constexpr bool from_string(std::string_view name,
                                         Terminated_by& out) noexcept {
    if (name == "self")    { out = Terminated_by::Self;    return true; }
    if (name == "runner")  { out = Terminated_by::Runner;  return true; }
    if (name == "unknown") { out = Terminated_by::Unknown; return true; }
    return false;
}

/// Start-time metadata written to resource.json.
struct Resource_metadata {
    std::string run_id{};              ///< NEXIGEN_RUN_ID
    std::string app_name{};            ///< NEXIGEN_APP_NAME
    std::string instance_id{};         ///< NEXIGEN_INSTANCE_ID
    std::string instance_dir{};        ///< NEXIGEN_INSTANCE_DIR
    std::int32_t pid{0};               ///< Child process id (session/group leader).
    std::int32_t ppid{0};              ///< Supervisor process id (the child's parent).
    std::string start_time_rfc3339{};  ///< UTC start instant, ISO-8601.
    std::uint64_t start_time_mono_ns{0}; ///< CLOCK_MONOTONIC_RAW at start.
    std::string version{"nexigen-cpp"};  ///< nexigen version tag.

    [[nodiscard]] std::string to_string() const;

    /// Build start metadata from an identity. `pid` is the child, `ppid` the
    /// supervisor; pass 0/0 before the fork if the pids are not known yet.
    [[nodiscard]] static Resource_metadata from_identity(
        const app::Run_identity& identity, std::int32_t pid,
        std::int32_t ppid = 0);
};

[[nodiscard]] inline std::string to_string(const Resource_metadata& metadata) {
    return metadata.to_string();
}

/// Exit metadata written to exit.json.
struct Exit_metadata {
    std::optional<std::int32_t> exit_code{}; ///< null when killed by a signal.
    std::optional<std::int32_t> signal{};    ///< null on a normal exit.
    std::uint64_t uptime_s{0};               ///< Whole seconds from start to exit.
    Terminated_by terminated_by{Terminated_by::Unknown};

    [[nodiscard]] std::string to_string() const;

    /// Build from a waitpid status word (WIFEXITED/WIFSIGNALED are honoured).
    [[nodiscard]] static Exit_metadata from_child_status(
        std::int32_t pid, std::int32_t status, std::uint64_t uptime_s,
        Terminated_by terminated_by) noexcept;
};

[[nodiscard]] inline std::string to_string(const Exit_metadata& metadata) {
    return metadata.to_string();
}

// --- cold-path ADL serialisation (bodies in src/runner/instance_lifecycle.cpp)
void to_json(nlohmann::json& j, const Resource_metadata& metadata);
void from_json(const nlohmann::json& j, Resource_metadata& metadata) noexcept;
void to_json(nlohmann::json& j, const Exit_metadata& metadata);
void from_json(const nlohmann::json& j, Exit_metadata& metadata) noexcept;
void to_json(nlohmann::json& j, Terminated_by value);
void from_json(const nlohmann::json& j, Terminated_by& value) noexcept;

/// Write a JSON document atomically (temporary file in the same directory,
/// then rename). Returns SystemError when any step fails.
[[nodiscard]] core::Status write_json_file(std::string_view path,
                                           const nlohmann::json& document) noexcept;

/// Write `<instance_dir>/resource.json` (start-time metadata).
[[nodiscard]] core::Status write_resource_json(
    std::string_view instance_dir,
    const Resource_metadata& metadata) noexcept;

/// Write `<instance_dir>/exit.json` (completion metadata).
[[nodiscard]] core::Status write_exit_json(
    std::string_view instance_dir, const Exit_metadata& metadata) noexcept;

/// Read and parse a JSON document. Returns SystemError when the file cannot
/// be read and InvalidArgument when it is not valid JSON.
[[nodiscard]] core::Result<nlohmann::json> read_json_file(
    std::string_view path) noexcept;

/// Create `<instance_dir>/` and `<instance_dir>/settings/`, including any
/// missing parents. `instance_dir` must be an absolute path of at most 511
/// bytes. Returns InvalidArgument for a bad path, SystemError on mkdir
/// failure (for example a permission error).
[[nodiscard]] core::Status create_instance_dir(
    std::string_view instance_dir) noexcept;

/// Create an absolute directory tree (mkdir -p) for runtime artifacts.
[[nodiscard]] core::Status create_artifact_dir(
    std::string_view artifact_dir) noexcept;

/// Write the four generated settings files under `<instance_dir>/settings/`
/// with the instance directory as the stream root.
[[nodiscard]] core::Status generate_settings(
    std::string_view instance_dir,
    const app::Telemetry_paths& resolved) noexcept;

/// Write the four generated settings files under `<instance_dir>/settings/`.
///
/// `stream_root` is the directory that receives `app.jsonl`, `metrics.jsonl`,
/// `traces.jsonl`, `chrono.jsonl`, and `stdout.log`/`stderr.log`. Each
/// `sink.path` is an absolute path inside `stream_root`. A path in `resolved`
/// is used only when it is absolute and lies inside `stream_root`; otherwise
/// the deterministic `<stream_root>/<stream>.jsonl` default is written.
/// `log_root_directory` is exactly `stream_root`.
[[nodiscard]] core::Status generate_settings(
    std::string_view instance_dir,
    std::string_view stream_root,
    const app::Telemetry_paths& resolved) noexcept;

/// Deterministic stream path `<instance_dir>/<file_name>`.
[[nodiscard]] std::string instance_file_path(std::string_view instance_dir,
                                             std::string_view file_name);

} // namespace nexigen::runner

template <>
struct std::formatter<nexigen::runner::Terminated_by>
    : std::formatter<std::string_view> {
    template <class Ctx>
    constexpr auto format(const nexigen::runner::Terminated_by value,
                          Ctx& ctx) const {
        return std::formatter<std::string_view>::format(
            nexigen::runner::to_string(value), ctx);
    }
};

template <>
struct std::formatter<nexigen::runner::Resource_metadata>
    : std::formatter<std::string_view> {
    template <class Ctx>
    constexpr auto format(const nexigen::runner::Resource_metadata& metadata,
                          Ctx& ctx) const {
        return std::formatter<std::string_view>::format(metadata.to_string(), ctx);
    }
};

template <>
struct std::formatter<nexigen::runner::Exit_metadata>
    : std::formatter<std::string_view> {
    template <class Ctx>
    constexpr auto format(const nexigen::runner::Exit_metadata& metadata,
                          Ctx& ctx) const {
        return std::formatter<std::string_view>::format(metadata.to_string(), ctx);
    }
};
