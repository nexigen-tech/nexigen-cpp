// nexigen::exec — aggregate + per-application suite state
// (change refactor-execution_manager-structure; spec execution_manager/app-suite).
#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

#include <nexigen/core/result.hpp>
#include <nexigen/exec/suite_app_status.hpp>
#include <nexigen/exec/suite_state.hpp>

namespace nexigen::exec {

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

[[nodiscard]] inline std::string to_string(const SuiteStatus& value) {
    return value.to_string();
}

void to_json(nlohmann::json& j, const SuiteStatus& status);
void from_json(const nlohmann::json& j, SuiteStatus& status) noexcept;

/// Derive aggregate and per-application state from the suite directory.
[[nodiscard]] core::Status suite_status(std::string_view suite_dir, SuiteStatus& out) noexcept;

} // namespace nexigen::exec
