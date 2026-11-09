// nexigen::exec — parsed and validated suite manifest
// (change refactor-execution_manager-structure; spec execution_manager/app-suite).
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

#include <nexigen/core/result.hpp>
#include <nexigen/core/thread_management.hpp>
#include <nexigen/core/log_level.hpp>
#include <nexigen/exec/app_spec.hpp>

namespace nexigen::exec {

/// Parsed and validated suite manifest.
struct SuiteManifest {
    std::uint32_t version{0};
    std::string suite_id{};
    std::string execution_manager_name{};
    std::string run_id{};
    std::string run_dir{};
    std::string global_settings_path{};
    std::string artifacts_path{};
    std::optional<core::LogLevel> log_level{};
    std::uint32_t suite_restart_attempts{0};
    std::uint64_t suite_restart_delay_s{5};
    std::string manifest_path{};
    core::ThreadSpec execution_manager_control_thread{};
    core::ThreadSpec execution_manager_logger_thread{};
    std::vector<AppSpec> apps{};

    [[nodiscard]] std::string to_string() const;
};

[[nodiscard]] inline std::string to_string(const SuiteManifest& value) {
    return value.to_string();
}

void to_json(nlohmann::json& j, const SuiteManifest& manifest);
void from_json(const nlohmann::json& j, SuiteManifest& manifest) noexcept;

/// Read, parse, and validate one suite manifest. `manifest_path` is resolved
/// to an absolute path so relative local settings resolve deterministically.
[[nodiscard]] core::Status load_suite_manifest(std::string_view path, SuiteManifest& out) noexcept;

/// Pure validation over an already parsed manifest.
[[nodiscard]] core::Status validate_suite_manifest(const SuiteManifest& manifest) noexcept;

} // namespace nexigen::exec
