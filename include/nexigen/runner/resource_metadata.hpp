// nexigen::runner — start-time metadata written to resource.json
// (change refactor-runner-structure; spec runner/instance-lifecycle).
#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

#include <nexigen/app/run_identity.hpp>
#include <nexigen/core/result.hpp>

namespace nexigen::runner {

/// Start-time metadata written to resource.json.
struct ResourceMetadata {
    std::string run_id{};                ///< NEXIGEN_RUN_ID
    std::string app_name{};              ///< NEXIGEN_APP_NAME
    std::string instance_id{};           ///< NEXIGEN_INSTANCE_ID
    std::string instance_dir{};          ///< NEXIGEN_INSTANCE_DIR
    std::int32_t pid{0};                 ///< Child process id (session/group leader).
    std::int32_t ppid{0};                ///< Supervisor process id (the child's parent).
    std::string start_time_rfc3339{};    ///< UTC start instant, ISO-8601.
    std::uint64_t start_time_mono_ns{0}; ///< CLOCK_MONOTONIC_RAW at start.
    /// Linux `/proc/<pid>/stat` starttime ticks recorded by the runner while
    /// it owned the child — the stop/status identity anchor (PID-recycling
    /// defense). 0 = unknown (pre-anchor writers keep the legacy pid check).
    std::uint64_t proc_start_ticks{0};
    std::string version{"nexigen-cpp"}; ///< nexigen version tag.

    [[nodiscard]] std::string to_string() const;

    /// Build start metadata from an identity and the application name. `pid` is
    /// the child, `ppid` the supervisor; pass 0/0 before the fork if the pids
    /// are not known yet.
    [[nodiscard]] static ResourceMetadata from_identity(const app::RunIdentity& identity,
                                                        std::string_view app_name, std::int32_t pid,
                                                        std::int32_t ppid = 0);
};

[[nodiscard]] inline std::string to_string(const ResourceMetadata& value) {
    return value.to_string();
}

void to_json(nlohmann::json& j, const ResourceMetadata& metadata);
void from_json(const nlohmann::json& j, ResourceMetadata& metadata) noexcept;

/// Write `<instance_dir>/resource.json` (start-time metadata).
[[nodiscard]] core::Status write_resource_json(std::string_view instance_dir,
                                               const ResourceMetadata& metadata) noexcept;

} // namespace nexigen::runner
