// nexigen::exec — start-time metadata written to resource.json
// (change refactor-execution_manager-structure; spec execution_manager/instance-lifecycle).
#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

#include <nexigen/app/run_identity.hpp>
#include <nexigen/core/result.hpp>

namespace nexigen::exec {

/// @brief Start-time metadata written to `resource.json`.
///
/// Records the instance identity, the child and supervisor pids, the wall
/// clock and monotonic start instants, and the Linux start-ticks identity
/// anchor used by the stop/status signal gate. Written once before the child
/// is considered started and read later by stop/status from another process.
/// Cold path only.
///
/// @note Spec `exec/instance-lifecycle`, requirement "resource.json captures
///       start-time metadata".
/// @note Spec `exec/process-supervision`, requirement "Identity is verified
///       before signals are sent".
/// @note Spec `exec/instance-lifecycle`, requirement "resource.json and
///       exit.json are serializable".
struct ResourceMetadata {
    std::string run_id{};                ///< NEXIGEN_RUN_ID
    std::string app_name{};              ///< NEXIGEN_APP_NAME
    std::string instance_id{};           ///< NEXIGEN_INSTANCE_ID
    std::string instance_dir{};          ///< NEXIGEN_INSTANCE_DIR
    std::int32_t pid{0};                 ///< Child process id (session/group leader).
    std::int32_t ppid{0};                ///< Supervisor process id (the child's parent).
    std::string start_time_rfc3339{};    ///< UTC start instant, ISO-8601.
    std::uint64_t start_time_mono_ns{0}; ///< CLOCK_MONOTONIC_RAW at start.
    /// Linux `/proc/<pid>/stat` starttime ticks recorded by the execution_manager while
    /// it owned the child — the stop/status identity anchor (PID-recycling
    /// defense). 0 = unknown (pre-anchor writers keep the legacy pid check).
    std::uint64_t proc_start_ticks{0};
    std::string version{"nexigen-cpp"}; ///< nexigen version tag.

    /// @brief Cold-path diagnostic rendering of the start metadata.
    ///
    /// @return Human-readable text naming the application, instance, and pid.
    /// @note Project mandate 9.
    [[nodiscard]] std::string to_string() const;

    /// @brief Build start metadata from an identity and the application name.
    ///
    /// @param identity Run identity supplying `run_id`, `instance_id`, and
    ///        `instance_dir`.
    /// @param app_name Application name recorded in `app_name`.
    /// @param pid Child process id; 0 before the fork when it is not known yet.
    /// @param ppid Supervisor process id; 0 before the fork when it is not
    ///        known yet.
    /// @return A populated record; the caller fills the time and ticks fields
    ///         once the child exists.
    /// @note Spec `exec/instance-lifecycle`, requirement "resource.json captures
    ///       start-time metadata".
    [[nodiscard]] static ResourceMetadata from_identity(const app::RunIdentity& identity,
                                                        std::string_view app_name, std::int32_t pid,
                                                        std::int32_t ppid = 0);
};

/// @brief Free helper (project mandate 9): mirrors the member.
///
/// @param value Start metadata to render.
/// @return The same text as `value.to_string()`.
[[nodiscard]] inline std::string to_string(const ResourceMetadata& value) {
    return value.to_string();
}

/// @brief Serialize start metadata into the `resource.json` document.
///
/// @param j Destination JSON value.
/// @param metadata Record to serialize.
/// @note Spec `exec/instance-lifecycle`, requirement "resource.json and
///       exit.json are serializable"; project mandate 10.
void to_json(nlohmann::json& j, const ResourceMetadata& metadata);

/// @brief Parse start metadata from JSON without throwing.
///
/// Hostile or wrong-typed input, including an array, leaves `metadata`
/// unchanged.
///
/// @param j Source JSON value.
/// @param metadata Target record.
/// @note Spec `exec/instance-lifecycle`, scenario "Hostile JSON leaves
///       metadata unchanged"; project mandate 10.
void from_json(const nlohmann::json& j, ResourceMetadata& metadata) noexcept;

/// @brief Write `<instance_dir>/resource.json` (start-time metadata).
///
/// The write is atomic: a temporary file in the instance directory is renamed
/// into place so readers see either the previous or the complete document.
///
/// @param instance_dir Control instance directory.
/// @param metadata Record to write.
/// @return Success, `InvalidArgument` for a bad path, or `SystemError` when the
///         file cannot be written.
/// @note Spec `exec/instance-lifecycle`, requirement "resource.json captures
///       start-time metadata".
/// @warning Must be called only after a successful `exec` handshake.
[[nodiscard]] core::Status write_resource_json(std::string_view instance_dir,
                                               const ResourceMetadata& metadata) noexcept;

} // namespace nexigen::exec
