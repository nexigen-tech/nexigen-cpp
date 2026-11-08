// nexigen::runner — suite resource record (`suite.json`)
// (change refactor-runner-structure; spec runner/app-suite).
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include <nexigen/core/thread_management.hpp>
#include <nexigen/runner/suite_app_metadata.hpp>

namespace nexigen::runner {

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

[[nodiscard]] inline std::string to_string(const SuiteMetadata& value) {
    return value.to_string();
}

void to_json(nlohmann::json& j, const SuiteMetadata& metadata);
void from_json(const nlohmann::json& j, SuiteMetadata& metadata) noexcept;

} // namespace nexigen::runner
