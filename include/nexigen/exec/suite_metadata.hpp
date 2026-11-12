// nexigen::exec — suite resource record (`suite.json`)
// (change refactor-execution_manager-structure; spec execution_manager/app-suite).
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include <nexigen/core/thread_management.hpp>
#include <nexigen/exec/suite_app_metadata.hpp>

namespace nexigen::exec {

/// @brief Suite resource record (`suite.json`).
///
/// Captures suite identity, settings and artifact roots, the supervisor
/// process identity, the start instant, and every application's control
/// instance directory. The record is written once by a fresh supervisor
/// before it starts reaping and is the input for `stop`/`status` run from
/// another process. It is a cold-path value type.
///
/// @note Spec `exec/app-suite`, requirement "Suite metadata lives in a
///       deterministic suite directory".
/// @note Spec `exec/logging`, requirement "Suite metadata preserves
///       execution_manager affinity".
/// @note Spec `exec/app-suite`, requirement "Suite types follow the
///       serialization contract".
struct SuiteMetadata {
    std::string suite_id{};               ///< Suite id; also the suite directory name.
    std::string execution_manager_name{}; ///< EM identity for its own telemetry.
    std::string run_id{};                 ///< Run id shared by all applications.
    std::string run_dir{};                ///< Absolute run directory.
    std::string global_settings_path{};   ///< Absolute suite settings directory or empty.
    std::string artifacts_path{};         ///< Absolute artifact root.
    std::int32_t pid{0};                  ///< Suite supervisor pid.
    std::string start_time_rfc3339{};     ///< UTC start instant, ISO-8601.
    std::uint64_t start_time_mono_ns{0};  ///< Monotonic start instant in nanoseconds.
    /// Linux `/proc/<pid>/stat` starttime ticks of the suite supervisor,
    /// recorded at spawn (PID-recycling identity anchor for suite stop).
    /// 0 = unknown (legacy pid-only checks).
    std::uint64_t proc_start_ticks{0};
    std::string manifest_path{};                         ///< Absolute path of the source manifest.
    core::ThreadSpec execution_manager_control_thread{}; ///< Recorded control-thread spec.
    core::ThreadSpec execution_manager_logger_thread{};  ///< Recorded logger-thread spec.
    std::vector<SuiteAppMetadata> apps{};                ///< Per-application placement records.

    /// @brief Cold-path diagnostic rendering of the suite record.
    ///
    /// @return Human-readable text naming the suite, supervisor pid, and
    ///         application count.
    /// @note Project mandate 9.
    [[nodiscard]] std::string to_string() const;
};

/// @brief Free helper (project mandate 9): mirrors the member.
///
/// @param value Suite record to render.
/// @return The same text as `value.to_string()`.
[[nodiscard]] inline std::string to_string(const SuiteMetadata& value) {
    return value.to_string();
}

/// @brief Serialize the suite record into the `suite.json` document.
///
/// @param j Destination JSON value.
/// @param metadata Record to serialize.
/// @note Spec `exec/app-suite`, requirement "Suite types follow the
///       serialization contract"; project mandate 10.
void to_json(nlohmann::json& j, const SuiteMetadata& metadata);

/// @brief Parse the suite record from JSON without throwing.
///
/// Hostile or wrong-typed input leaves `metadata` unchanged.
///
/// @param j Source JSON value.
/// @param metadata Target record.
/// @note Spec `exec/app-suite`, requirement "Suite types follow the
///       serialization contract"; project mandate 10.
void from_json(const nlohmann::json& j, SuiteMetadata& metadata) noexcept;

} // namespace nexigen::exec
