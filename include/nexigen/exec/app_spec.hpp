// nexigen::exec — one application entry in the suite manifest
// (change refactor-execution_manager-structure; spec execution_manager/app-suite).
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include <nexigen/core/thread_management.hpp>
#include <nexigen/core/log_level.hpp>
#include <nexigen/exec/impact_on_app_suite_operability.hpp>

namespace nexigen::exec {

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
    std::optional<core::LogLevel> log_level{};

    [[nodiscard]] std::string to_string() const;
};

[[nodiscard]] inline std::string to_string(const AppSpec& value) {
    return value.to_string();
}

void to_json(nlohmann::json& j, const AppSpec& spec);
void from_json(const nlohmann::json& j, AppSpec& spec) noexcept;

} // namespace nexigen::exec
